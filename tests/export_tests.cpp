#include "app/audio_exporter.hpp"
#include "app/recording_bank.hpp"
#include "core/recording.hpp"
#include "core/render.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {
using namespace esl;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

QByteArray read(const QString& path) {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot read exported file");
    return f.readAll();
}

QJsonArray clips(const QString& path) {
    return QJsonDocument::fromJson(read(QDir(path).filePath("manifest.json"))).object().value("clips").toArray();
}

template <typename T>
T le(const QByteArray& data, int offset) { return qFromLittleEndian<T>(data.constData() + offset); }

double pcm(const QByteArray& data, int offset, int bytes) {
    if (bytes == 2) return le<qint16>(data, offset) / 32768.0;
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + offset);
    const int code = p[0] + (p[1] << 8) + (p[2] << 16);
    return (code < 0x800000 ? code : code - 0x1000000) / 8388608.0;
}

struct Bandpass {
    Bandpass(double hz, double q) {
        const double omega = 2 * pi * hz / RecordingBank::sampleRate;
        const double alpha = std::sin(omega) / (2 * q);
        feed = alpha / (1 + alpha);
        a = 2 * std::cos(omega) / (1 + alpha);
        b = (alpha - 1) / (1 + alpha);
    }

    double tick(double x) {
        const double y = feed * (x - x2) + a * y1 + b * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    double feed, a, b;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

void checkSteadyWhistle(const RecordingBank& bank, double rpm, double bandHz) {
    constexpr int rate = RecordingBank::sampleRate;
    constexpr int window = rate / 4;
    RecordingPlayer player;
    player.prepare(&bank, rate);

    // Listen through a bandpass centred on the whistle and measure 250 ms levels
    // across several loop boundaries. Acceleration baked into a take fails here.
    Bandpass band(bandHz, 1.65);
    double total = 0;
    std::array<double, 48> energies{};
    for (auto& bandEnergy : energies) {
        for (int i = 0; i < window; ++i) {
            const double x = player.tick(rpm);
            const double y = band.tick(x);
            total += x * x;
            bandEnergy += y * y;
        }
    }
    std::sort(energies.begin(), energies.end());
    require(energies[energies.size() / 2] > total / energies.size() * 1e-4,
            "T-90M steady field recording lost its upper band");
    require(energies[energies.size() * 9 / 10] < 2.0 * energies[energies.size() / 10],
            "T-90M whistle swells repeatedly at a fixed RPM");
}

void checkBradleyAtFixedRpm(const Settings& settings) {
    for (const auto& loop : settings.model.recording->loops) {
        const double pulseHz = loop.rpm * 4 / 60;
        Bandpass pulse(pulseHz, 18), whine(5.28 * pulseHz, 18);
        double firingEnergy = 0, whineEnergy = 0;
        for (auto sample : loop.pcm) {
            const double fire = pulse.tick(sample), gear = whine.tick(sample);
            firingEnergy += fire * fire;
            whineEnergy += gear * gear;
        }
        require(firingEnergy > 0 && whineEnergy < 0.02 * firingEnergy,
                "Bradley's recorded transmission tone masks the V8 at steady RPM");
    }

    ClipRequest clip;
    clip.rpm = 1830;
    clip.seconds = 0.55;
    clip.load = 0.6;
    auto onStand = settings;
    auto& mechanics = onStand.layers[static_cast<std::size_t>(LayerId::Mechanical)];
    mechanics.solo = true;
    const auto gearChangesSound = [&] {
        onStand.controls.gear = 0;
        auto neutral = renderClip(onStand, clip, 48000).samples;
        onStand.controls.gear = 1;
        auto first = renderClip(onStand, clip, 48000).samples;
        return !std::equal(neutral.begin(), neutral.end(), first.begin(), first.end(),
            [](auto a, auto b) { return a.left == b.left && a.right == b.right; });
    };
    require(!gearChangesSound(), "Bradley adds synthetic gear mesh at fixed RPM");
    mechanics.shape = 0.5;
    require(gearChangesSound(), "Gear-noise fader failed to restore gear mesh");
}

void checkForTonalBursts(const RecordingBank& bank, std::initializer_list<double> frequencies, const char* error) {
    // The sources have short, narrow metal tones here. An average over a whole
    // loop can hide them. Compare against the surrounding band every 80 ms.
    constexpr int window = RecordingBank::sampleRate * 80 / 1000;
    for (const auto& loop : bank.loops) {
        for (double hz : frequencies) {
            Bandpass whistle(hz, 30), surrounding(hz, 4);
            double narrow = 0.0, broad = 0.0;
            int count = 0;
            for (auto sample : loop.pcm) {
                const double s = whistle.tick(sample), b = surrounding.tick(sample);
                narrow += s * s;
                broad += b * b;
                if (++count == window) {
                    require(narrow < 0.40 * broad, error);
                    count = 0;
                    narrow = broad = 0.0;
                }
            }
        }
    }
}

void checkWave(const QString& path, const QJsonObject& clip, const ExportOptions& o) {
    const auto data = read(QDir(path).filePath(clip.value("file").toString()));
    const int channels = o.channels;
    const int bytes = o.bits / 8;
    const int stride = channels * bytes;
    const int frames = clip.value("frames").toInt();
    const bool loop = clip.value("loop").isObject();
    require(data.startsWith("RIFF") && data.mid(8, 8) == "WAVEfmt ", "Missing WAV identification or fmt chunk");
    require(le<quint32>(data, 4) == data.size() - 8, "RIFF size wrong");
    require(le<quint16>(data, 20) == 1 && le<quint16>(data, 22) == channels && le<quint32>(data, 24) == o.sampleRate
        && le<quint16>(data, 32) == stride && le<quint16>(data, 34) == o.bits, "Wrong PCM encoding");
    const int start = loop ? 112 : 44;
    if (loop) {
        require(data.mid(36, 4) == "smpl" && le<quint32>(data, 40) == 60 && le<quint32>(data, 72) == 1, "Wrong WAV loop chunk");
        require(le<quint32>(data, 88) == 0 && le<quint32>(data, 92) == frames - 1, "Wrong inclusive loop end");
        require(clip.value("loop").toObject().value("endFrameExclusive").toInt() == frames, "WAV/manifest loop mismatch");
    }
    require(data.mid(start - 8, 4) == "data" && le<quint32>(data, start - 4) == frames * stride, "Wrong data length");
    for (int c = 0; c < channels; ++c) {
        const auto sample = [&](int frame) { return pcm(data, start + frame * stride + c * bytes, bytes); };
        double maxSample = 0.0, maxStep = 0.0;
        for (int i = 1; i < frames; ++i) {
            maxStep = std::max(maxStep, std::abs(sample(i) - sample(i - 1)));
            maxSample = std::max(maxSample, std::abs(sample(i)));
        }
        require(maxSample > 0.009 && maxSample < 0.99, "WAV is silent or clipped");
        if (loop) require(std::abs(sample(0) - sample(frames - 1)) <= maxStep + 1e-5, "Loop boundary contains a discontinuity");
        else require(sample(0) == 0 && sample(frames - 1) == 0, "One-shot edges were not faded");
    }
}

void checkFiles(const QString& base) {
    QTemporaryDir test;
    require(test.isValid(), "No temporary directory");
    const QDir presets(base);
    constexpr auto transmission = static_cast<std::size_t>(LayerId::Transmission);
    QSet<QString> factory;
    for (const auto& name : presets.entryList({"*.json"}, QDir::Files)) {
        Preset p, copy;
        const auto issue = loadPreset(presets.filePath(name), p);
        if (!issue.isEmpty()) std::cerr << name.toStdString() << " " << issue.toStdString() << '\n';
        require(issue.isEmpty(), "Invalid built-in preset");
        const auto path = QDir(test.path()).filePath(name);
        require(savePreset(path, p).isEmpty() && loadPreset(path, copy).isEmpty(), "Preset round-trip failed");
        require(p.name == copy.name && p.settings.model.seed == copy.settings.model.seed &&
            p.settings.model.firingOrder == copy.settings.model.firingOrder && p.settings.controls.program == copy.settings.controls.program,
            "Preset round-trip changed parameters");
        require(p.settings.listener == copy.settings.listener && p.settings.model.recording == copy.settings.model.recording &&
            p.settings.model.overrunDrag == copy.settings.model.overrunDrag &&
            pistonStrokeMm(p.settings.model) == pistonStrokeMm(copy.settings.model) &&
            compressionRatio(p.settings.model) == compressionRatio(copy.settings.model) &&
            p.settings.limiter.enabled == copy.settings.limiter.enabled && p.settings.limiter.cutRpm == copy.settings.limiter.cutRpm &&
            p.settings.limiter.resumeRpm == copy.settings.limiter.resumeRpm, "Acoustics, geometry or limiter lost on save");
        require(p.settings.layers[transmission].gain == copy.settings.layers[transmission].gain &&
                (p.settings.layers[transmission].gain > 0) == (p.settings.controls.program == Program::Drive),
                "Factory engine or drive mix changed on preset load/save");
        factory.insert(p.name);
    }
    require(factory.size() == 64 && factory.contains("Oshkosh M1070A1 HET") && factory.contains("KamAZ-5320") &&
            factory.contains("BTR-82A") && factory.contains("BTR-90"), "Missing factory engine or duplicate name");
    const auto vehicles = QJsonDocument::fromJson(read(":/library/fleet.json")).array();
    int covered = 0;
    for (const auto& vehicle : vehicles) {
        const auto row = vehicle.toObject();
        const auto id = row.value("id").toString();
        const bool humvee = id == "west_hmmwv_mk19" || id == "west_m1025_javelin";
        require(row.value("filesPresent").toBool() && row.value("preset").isNull() == humvee,
                "Incorrect game audio or coverage in fleet index");
        if (!humvee) {
            require(factory.contains(row.value("preset").toString()), "Unresolvable game alias");
            ++covered;
        }
    }
    require(vehicles.size() == 60 && covered == 58, "Incomplete game catalog");
    require(recordingKeys().size() == 44 && recordingKeys().size() == recordingLabels().size(), "Missing field banks");
    for (int i = 1; i < recordingKeys().size(); ++i)
        require(builtinRecording(static_cast<RecordingId>(i)) != nullptr, "Unplayable field bank");
    Preset p;
    require(loadPreset(presets.filePath("01_t72b3.json"), p).isEmpty(), "No initial preset");
    require(p.settings.model.recording && p.settings.model.recording->loops.size() == 8 &&
            p.settings.listener.position == ListeningPosition::ThirdPerson, "Missing recorded RPM anchors");
    Preset other;
    require(loadPreset(presets.filePath("02_t90m.json"), other).isEmpty() && other.settings.model.recording &&
            other.settings.model.recording != p.settings.model.recording, "T-90M did not load its own bank");
    const auto& t90 = other.settings.model;
    require(t90.recording->loops.size() == 5 && t90.recording->loops.front().rpm == t90.idleRpm &&
            t90.recording->loops.back().rpm == t90.maxRpm, "T-90M recording bank must span idle to maximum RPM");
    for (const double rpm : {680., 950., 1200., 1450., 1700., 1950., 2230.}) {
        for (const double hz : {2000., 4000., 8000.}) checkSteadyWhistle(*t90.recording, rpm, hz);
    }
    Preset leopard1, leopard2;
    require(loadPreset(presets.filePath("04c_leopard1.json"), leopard1).isEmpty() &&
            loadPreset(presets.filePath("04b_leopard2_field.json"), leopard2).isEmpty(), "Cannot load Leopard presets");
    const auto& v10 = leopard1.settings.model;
    const auto& v12 = leopard2.settings.model;
    require(v10.cylinders == 10 && v10.boost == Boost::Supercharger && v10.recording &&
            v10.recording->id == RecordingId::Leopard1 && v10.recording->loops.size() == 4 &&
            v12.cylinders == 12 && v12.boost == Boost::Turbocharger && v12.recording &&
            v12.recording->id == RecordingId::Leopard2 && v12.recording->loops.size() == 4,
            "The two Leopard models lost their distinct engines or field recordings");
    Preset canonical;
    require(loadPreset(presets.filePath("04_leopard2.json"), canonical).isEmpty() &&
            canonical.settings.model.recording == v12.recording && canonical.settings.model.overrunDrag == v12.overrunDrag &&
            canonical.settings.listener.position == ListeningPosition::ThirdPerson &&
            leopard2.settings.listener.position == ListeningPosition::Near, "Leopard 2 menu presets lost their calibrated audio or camera");
    for (const auto* model : {&v10, &v12}) {
        RecordingPlayer power, lift;
        power.prepare(model->recording, 24000);
        lift.prepare(model->recording, 24000);
        double driveEnergy = 0, liftEnergy = 0;
        for (int n = 0; n < 48000; ++n) {
            const double drive = power.tick(1600, 0.0), coast = lift.tick(1600, 1.0);
            driveEnergy += drive * drive;
            liftEnergy += coast * coast;
        }
        require(liftEnergy > 0.06 * driveEnergy && liftEnergy < 0.59 * driveEnergy,
                "The loaded and released microphone takes do not respond to the pedal at the same RPM");
    }
    Preset puma, bradley, hummer;
    require(loadPreset(presets.filePath("04e_puma.json"), puma).isEmpty() && puma.settings.model.recording &&
            puma.settings.model.recording->id == RecordingId::Puma && puma.settings.model.recording->loops.size() == 3 &&
            puma.settings.model.cylinders == 10, "Missing embedded Puma V10");
    require(loadPreset(presets.filePath("04f_bradley.json"), bradley).isEmpty() && bradley.settings.model.recording &&
            bradley.settings.model.recording->id == RecordingId::Bradley && bradley.settings.model.recording->loops.size() == 4 &&
            bradley.settings.model.cylinders == 8, "Missing embedded Bradley V8");
    checkBradleyAtFixedRpm(bradley.settings);
    Preset bmp12, bmp3, dci, uaz, stryker, ariete, m113;
    const auto hasBank = [&](const QString& file, Preset& preset, RecordingId id, int cylinders, std::size_t count) {
        if (!loadPreset(presets.filePath(file), preset).isEmpty()) return false;
        const auto& model = preset.settings.model;
        return model.cylinders == cylinders && model.recording && model.recording->id == id &&
               model.recording->loops.size() == count && preset.settings.listener.position == ListeningPosition::ThirdPerson;
    };
    require(hasBank("03b_bmp12.json", bmp12, RecordingId::Bmp12, 6, 3) &&
            hasBank("03c_bmp3.json", bmp3, RecordingId::Bmp3, 10, 3), "Missing recorded BMP engine");
    checkForTonalBursts(*bmp12.settings.model.recording, {2650., 2950., 3450., 5450., 6050., 6650.},
                        "Track squeal leaked into the BMP-1/2 engine loop");
    require(hasBank("10_dci23.json", dci, RecordingId::Dci23, 4, 4) &&
            dci.settings.model.boost == Boost::Turbocharger, "Missing recorded 2.3 dCi");
    for (const auto& loop : dci.settings.model.recording->loops) {
        if (loop.rpm > dci.settings.model.idleRpm)
            require(!loop.coast.empty() && loop.pcm != loop.coast, "2.3 dCi lost its recorded lift-off");
    }
    require(hasBank("11_uaz.json", uaz, RecordingId::Uaz, 4, 5) && uaz.settings.model.kind == EngineKind::Gasoline &&
            uaz.settings.model.firingOrder == FiringOrder{1, 2, 4, 3}, "UAZ-469 lost its recorded gasoline engine or firing order");
    require(hasBank("04g_stryker.json", stryker, RecordingId::Stryker, 6, 4) &&
            stryker.settings.model.layout == Layout::Inline && stryker.settings.model.boost == Boost::Turbocharger &&
            stryker.settings.layers[static_cast<std::size_t>(LayerId::Mechanical)].shape == 0,
            "Stryker lost its recorded inline-six engine or gearbox setting");
    checkForTonalBursts(*stryker.settings.model.recording, {510., 585., 655., 810., 1050.},
                        "Transmission whine leaked into the Stryker engine loop");
    require(hasBank("04h_ariete.json", ariete, RecordingId::Ariete, 12, 3) &&
            ariete.settings.model.boost == Boost::Turbocharger, "Missing recorded C1 Ariete V12");
    require(hasBank("04i_m113.json", m113, RecordingId::M113, 6, 4) && m113.settings.model.strokes == 2 &&
            m113.settings.model.boost == Boost::Supercharger, "M113 lost its two-stroke 6V53");
    require(loadPreset(presets.filePath("09_hummer_h2.json"), hummer).isEmpty() && hummer.settings.model.recording &&
            hummer.settings.model.recording->id == RecordingId::HummerH2 && hummer.settings.model.recording->loops.size() == 5 &&
            hummer.settings.model.kind == EngineKind::Gasoline, "Missing embedded gasoline Hummer H2");
    auto h2Mic = hummer.settings;
    h2Mic.layers[static_cast<std::size_t>(LayerId::Recording)].solo = true;
    const auto h2Audio = renderClip(h2Mic, {}, 44100);
    require(std::any_of(h2Audio.samples.begin(), h2Audio.samples.end(), [](auto frame) { return std::abs(frame.left) > 0.035; }),
            "A gasoline recording bank is silent");
    h2Mic.layers[static_cast<std::size_t>(LayerId::Recording)].mute = true;
    const auto h2Muted = renderClip(h2Mic, {}, 44100);
    require(std::all_of(h2Muted.samples.begin(), h2Muted.samples.end(), [](auto frame) { return frame.left == 0 && frame.right == 0; }),
            "A gasoline recording solo did not isolate its microphone");
    Preset marder;
    require(loadPreset(presets.filePath("04d_marder1.json"), marder).isEmpty() &&
            marder.settings.model.cylinders == 6 && marder.settings.model.recording &&
            marder.settings.model.recording->id == RecordingId::Marder &&
            marder.settings.model.recording->loops.size() == 3, "Marder did not load its V6 field recording");
    const auto recordingIndex = static_cast<std::size_t>(LayerId::Recording);
    auto micOnly = p.settings;
    micOnly.layers[recordingIndex].solo = true;
    const auto recorded = renderClip(micOnly, {}, 44100);
    require(std::any_of(recorded.samples.begin(), recorded.samples.end(), [](auto frame) { return std::abs(frame.left) > 0.04; }),
            "Recording solo is silent");
    micOnly.layers[recordingIndex].mute = true;
    const auto silence = renderClip(micOnly, {}, 44100);
    require(std::all_of(silence.samples.begin(), silence.samples.end(), [](auto frame) { return frame.left == 0 && frame.right == 0; }),
            "Muted solo recording leaked into other layers");
    const auto damagedPath = QDir(test.path()).filePath("invalid.json");
    QFile damaged(damagedPath);
    require(damaged.open(QIODevice::WriteOnly), "Cannot prepare bad input");
    damaged.write(R"({"version":1,"name":"Bad","model":{"cylinders":0}})");
    damaged.close();
    require(!loadPreset(damagedPath, p).isEmpty() && p.name == "T-72B3", "Bad preset replaced valid state");
    require(damaged.open(QIODevice::WriteOnly), "Cannot prepare legacy input");
    damaged.write(R"({"version":1,"name":"Legacy","model":{"kind":"gasoline","liters":2.0,"cylinders":4}})");
    damaged.close();
    require(loadPreset(damagedPath, other).isEmpty() && !other.settings.model.recording && !other.settings.limiter.enabled &&
            other.settings.listener.position == ListeningPosition::Direct &&
            std::abs(pistonStrokeMm(other.settings.model) - cylinderBoreMm(other.settings.model)) < 1e-8,
            "Legacy preset defaults are inconsistent");

    const auto checkExport = [&](const Preset& preset, const ExportOptions& o, int count) {
        const auto result = exportAudio(preset, o, test.path());
        if (!result.error.isEmpty()) std::cerr << result.error.toStdString() << '\n';
        require(result.error.isEmpty() && result.wavCount == count && !result.directory.isEmpty(), "Export failed");
        const auto manifest = clips(result.directory);
        require(manifest.size() == count, "Wrong clip count in manifest");
        for (const auto& item : manifest) checkWave(result.directory, item.toObject(), o);
        return result;
    };

    ExportOptions o;
    Preset ride;
    require(loadPreset(presets.filePath("12_oshkosh_drive.json"), ride).isEmpty(), "No drive demo");
    ride.settings.layers[transmission].solo = true;
    o.mode = ExportMode::Drive;
    o.bits = 16;
    o.normalize = false;
    const auto drive = checkExport(ride, o, 1);
    const auto driveClip = clips(drive.directory)[0].toObject();
    require(driveClip.value("file") == "drive.wav" && driveClip.value("frames").toInt() == 15 * o.sampleRate &&
            driveClip.value("rpmPeak").toDouble() > driveClip.value("rpmStart").toDouble() + 350, "Invalid drive timeline");
    const auto stem = read(QDir(drive.directory).filePath("drive.wav"));
    require(stem.mid(44, 4 * o.sampleRate) == QByteArray(4 * o.sampleRate, '\0'), "Neutral intro contaminated in gearbox stem");
    o = ExportOptions{};
    o.seconds = 1.0;
    for (const int rate : {44100, 48000}) {
        for (const int bits : {16, 24}) {
            for (const int channels : {1, 2}) {
                o.sampleRate = rate;
                o.bits = bits;
                o.channels = channels;
                checkExport(p, o, 1);
            }
        }
    }
    const auto a = checkExport(p, o, 1);
    const auto b = checkExport(p, o, 1);
    const auto manifest = QJsonDocument::fromJson(read(QDir(a.directory).filePath("manifest.json"))).object();
    require(manifest.value("listener").toObject() == listenerJson(p.settings.listener), "Listener absent from WAV manifest");
    require(a.directory != b.directory && read(QDir(a.directory).filePath("steady.wav")) == read(QDir(b.directory).filePath("steady.wav")),
            "Repeat export overwrote a directory or changed seeded samples");
    auto impaired = p;
    impaired.settings.faults = {3, 0.0, 0.24, 0.81, 0.27, 0.37};
    const auto bad = checkExport(impaired, o, 1);
    Preset recovered;
    require(loadPreset(QDir(bad.directory).filePath("preset.json"), recovered).isEmpty() &&
        recovered.settings.faults.cylinder == 3 && recovered.settings.faults.injectorWear == 0.81 &&
        recovered.settings.faults.fuelBias == 0.24, "Fault settings were lost on preset round-trip");
    const auto rerender = checkExport(recovered, o, 1);
    require(read(QDir(bad.directory).filePath("steady.wav")) != read(QDir(a.directory).filePath("steady.wav")) &&
        read(QDir(bad.directory).filePath("steady.wav")) == read(QDir(rerender.directory).filePath("steady.wav")),
        "Fault audio did not change output reproducibly");

    o.mode = ExportMode::Grid;
    o.rpmMin = 1000;
    o.rpmMax = 2000;
    o.rpmCount = 2;
    const auto grid = checkExport(p, o, 4);
    const auto points = clips(grid.directory);
    require(points[0].toObject().value("rpm").toInt() == 1000 && points[1].toObject().value("load").toDouble() == 0.8 &&
        points[3].toObject().value("rpm").toInt() == 2000, "Grid metadata mismatch");
    o.rpmMax = o.rpmMin;
    require(!exportAudio(p, o, test.path()).error.isEmpty(), "Invalid grid accepted");
    std::atomic_bool stop(true);
    require(exportAudio(p, o, test.path(), &stop).cancelled, "Export cancellation did not propagate");

    Preset turbine;
    require(loadPreset(presets.filePath("05_m1_abrams.json"), turbine).isEmpty(), "No turbine preset");
    o.mode = ExportMode::Pack;
    const auto pack = checkExport(turbine, o, 9);
    const auto transitions = clips(pack.directory);
    const auto rpm = [&](int index, const char* key) { return transitions[index].toObject().value(QLatin1String(key)).toDouble(); };
    require(rpm(0, "rpmStart") == 0 && rpm(0, "rpmEnd") > 500, "Start clip did not reach idle");
    require(rpm(6, "rpmEnd") > rpm(6, "rpmStart") + 350, "Acceleration clip did not gain RPM");
    require(rpm(7, "rpmEnd") < rpm(7, "rpmStart") - 350, "Deceleration clip did not lose RPM");
    require(rpm(8, "rpmEnd") < 100, "Shutdown clip did not reach a stop");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    require(app.arguments().size() == 2, "Supply preset directory");
    checkFiles(app.arguments().at(1));
    std::cout << "Presets, WAV formats, normalized packs and loop metadata verified\n";
}
