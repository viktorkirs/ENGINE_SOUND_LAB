#include "app/audio_exporter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdlib>
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
    int n = 0;
    for (const auto& name : presets.entryList({"*.json"}, QDir::Files)) {
        Preset p, copy;
        require(loadPreset(presets.filePath(name), p).isEmpty(), "Invalid built-in preset");
        const auto path = QDir(test.path()).filePath(name);
        require(savePreset(path, p).isEmpty() && loadPreset(path, copy).isEmpty(), "Preset round-trip failed");
        require(p.name == copy.name && p.settings.model.seed == copy.settings.model.seed &&
            p.settings.model.firingOrder == copy.settings.model.firingOrder && p.settings.controls.program == copy.settings.controls.program,
            "Preset round-trip changed parameters");
        ++n;
    }
    require(n == 9, "Missing demonstration presets");
    Preset p;
    require(loadPreset(presets.filePath("01_t72b3.json"), p).isEmpty(), "No initial preset");
    const auto damagedPath = QDir(test.path()).filePath("invalid.json");
    QFile damaged(damagedPath);
    require(damaged.open(QIODevice::WriteOnly), "Cannot prepare bad input");
    damaged.write(R"({"version":1,"name":"Bad","model":{"cylinders":0}})");
    damaged.close();
    require(!loadPreset(damagedPath, p).isEmpty() && p.name == "T-72B3", "Bad preset replaced valid state");

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
