#include "app/audio_exporter.hpp"

#include "core/render.hpp"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>

namespace esl {
namespace {

struct Job {
    QString name;
    ClipRequest clip;
    QJsonObject meta;
};

double throttleAt(const Model& model, double rpm) {
    return std::pow(std::clamp((rpm - model.idleRpm) / (model.maxRpm - model.idleRpm), 0.0, 1.0), 1.08);
}

std::vector<Job> schedule(const Model& model, const ExportOptions& o) {
    auto at = [&](double fraction) { return std::lerp(model.idleRpm, model.maxRpm, fraction); };
    auto loop = [&](QString name, double rpm, double load) {
        return Job{std::move(name), {Motion::Steady, o.seconds, rpm, load, throttleAt(model, rpm), true}, {}};
    };
    if (o.mode == ExportMode::Single) {
        return {{QStringLiteral("steady"), {Motion::Steady, o.seconds, o.rpm, o.load, o.throttle, o.singleLoop}, {}}};
    }
    if (o.mode == ExportMode::Drive) {
        return {{QStringLiteral("drive"), {Motion::Drive, programDuration(Program::Drive), model.idleRpm, 0.12, 0.04, false}, {}}};
    }
    if (o.mode == ExportMode::Pack) {
        const bool turbine = model.kind == EngineKind::Turbine;
        return {
            {"start", {Motion::Start, turbine ? 3.2 + 2.3 * model.coreLag : 3.6, 0, 0.12, 0, false}, {}},
            loop("idle", at(0.0), 0.12), loop("low_rpm", at(0.24), 0.26), loop("medium_rpm", at(0.53), 0.49),
            loop("high_rpm", at(0.80), 0.72), loop("maximum_rpm", at(1.0), 0.90),
            {"acceleration", {Motion::Acceleration, turbine ? 7.0 : 6.0, at(0.04), 0.75, 0.04, false}, {}},
            {"deceleration", {Motion::Deceleration, turbine ? 7.0 : 6.0, at(0.84), 0.52, 0.87, false}, {}},
            {"shutdown", {Motion::Shutdown, turbine ? std::max(6.7, model.coreLag * 5.55) : 5.1, at(0.0), 0.12, 0, false}, {}}
        };
    }

    std::vector<Job> jobs;
    for (int r = 0; r < o.rpmCount; ++r) {
        const double rpm = std::lerp(o.rpmMin, o.rpmMax, r / static_cast<double>(std::max(1, o.rpmCount - 1)));
        for (int l = 0; l < o.loadCount; ++l) {
            const double load = std::lerp(o.loadMin, o.loadMax, l / static_cast<double>(std::max(1, o.loadCount - 1)));
            auto name = QStringLiteral("%1_%2_rpm_%3_load_%4").arg(r + 1, 2, 10, QLatin1Char('0'))
                .arg(l + 1, 2, 10, QLatin1Char('0')).arg(qRound(rpm)).arg(qRound(load * 100));
            jobs.push_back(loop(std::move(name), rpm, load));
        }
    }
    return jobs;
}

QString check(const Model& model, const ExportOptions& o) {
    if ((o.sampleRate != 44100 && o.sampleRate != 48000) || (o.bits != 16 && o.bits != 24)
        || (o.channels != 1 && o.channels != 2) || !std::isfinite(o.seconds) || o.seconds < 1.0 || o.seconds > 15.0)
        return QStringLiteral("Проверьте формат WAV и длительность (1–15 с).");
    if (o.mode == ExportMode::Grid &&
        (!(model.idleRpm <= o.rpmMin && o.rpmMin <= o.rpmMax && o.rpmMax <= model.maxRpm &&
           0.0 <= o.loadMin && o.loadMin <= o.loadMax && o.loadMax <= 1.0) ||
         o.rpmCount < 1 || o.rpmCount > 16 || o.loadCount < 1 || o.loadCount > 10 ||
         (o.rpmCount > 1 && o.rpmMin == o.rpmMax) || (o.loadCount > 1 && o.loadMin == o.loadMax)))
        return QStringLiteral("Проверьте диапазоны и число точек сетки. Для нескольких точек границы должны различаться.");
    if (o.mode == ExportMode::Single && !(std::isfinite(o.rpm) && 0.0 <= o.load && o.load <= 1.0 &&
                                          0.0 <= o.throttle && o.throttle <= 1.0))
        return QStringLiteral("Проверьте обороты, газ и нагрузку.");
    return {};
}

QString slug(QString input) {
    static const QRegularExpression other(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression edges(QStringLiteral("^-+|-+$"));
    input = input.toLower().replace(other, QStringLiteral("-")).left(48).remove(edges);
    return input.isEmpty() ? QStringLiteral("engine") : input;
}

double samplePeak(std::span<const StereoFrame> signal, int channels) {
    double peak = 0.0;
    for (const auto& frame : signal) {
        const double level = channels == 1 ? std::abs((frame.left + frame.right) * 0.5) :
            std::max(std::abs(frame.left), std::abs(frame.right));
        peak = std::max(peak, level);
    }
    return peak;
}

QString makeWav(const QString& fileName, const ExportOptions& o, std::span<const StereoFrame> signal, bool loop, double gain) {
    const quint32 frameCount = static_cast<quint32>(signal.size());
    const quint16 alignment = o.channels * (o.bits / 8);
    const quint32 audioBytes = frameCount * alignment;
    const quint32 padding = audioBytes % 2;
    QByteArray header;
    QDataStream out(&header, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + audioBytes + padding + (loop ? 68 : 0));
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(o.channels) << quint32(o.sampleRate)
        << quint32(o.sampleRate * alignment) << alignment << quint16(o.bits);
    if (loop) {
        out.writeRawData("smpl", 4);
        out << quint32(60) << quint32(0) << quint32(0) << quint32(std::lround(1.0e9 / o.sampleRate))
            << quint32(60) << quint32(0) << quint32(0) << quint32(0) << quint32(1) << quint32(0)
            << quint32(0) << quint32(0) << quint32(0) << quint32(frameCount - 1) << quint32(0) << quint32(0);
    }
    out.writeRawData("data", 4);
    out << audioBytes;

    QByteArray data(audioBytes + padding, Qt::Uninitialized);
    char* dest = data.data();
    const double ceiling = o.bits == 16 ? 32767.0 : 8388607.0;
    auto writeSample = [&](double x) {
        const auto pcm = static_cast<quint32>(std::lround(std::clamp(x * gain, -1.0, 1.0) * ceiling));
        *dest++ = static_cast<char>(pcm & 0xffu);
        *dest++ = static_cast<char>((pcm >> 8) & 0xffu);
        if (o.bits == 24) *dest++ = static_cast<char>((pcm >> 16) & 0xffu);
    };
    for (const auto& frame : signal) {
        if (o.channels == 1) writeSample((frame.left + frame.right) * 0.5);
        else {
            writeSample(frame.left);
            writeSample(frame.right);
        }
    }
    if (padding) *dest = 0;
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly) || file.write(header) != header.size() ||
        file.write(data) != data.size() || !file.commit()) return file.errorString();
    return {};
}

} // namespace

ExportResult exportAudio(const Preset& preset, const ExportOptions& o, const QString& destination,
                         const std::atomic_bool* cancel, const ExportProgress& progress) {
    const auto error = [](QString message) { return ExportResult{{}, std::move(message)}; };
    const auto stopped = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
    const auto cancelled = [] { return ExportResult{{}, {}, 0, true}; };
    if (stopped()) return cancelled();
    const auto issue = check(preset.settings.model, o);
    if (!issue.isEmpty()) return error(issue);
    if (destination.isEmpty()) return error(QStringLiteral("Выберите папку экспорта."));
    QDir dest(QDir(destination).absolutePath());
    if (!dest.exists() && !QDir().mkpath(dest.path())) return error(QStringLiteral("Невозможно создать папку %1.").arg(dest.path()));

    QTemporaryDir working(dest.filePath(QStringLiteral(".engine-export-XXXXXX")));
    QTemporaryDir scratch;
    if (!working.isValid() || !scratch.isValid()) return error(QStringLiteral("Невозможно создать временную папку для экспорта."));
    const QDir stage(working.path());
    QDir raw(scratch.path());
    auto jobs = schedule(preset.settings.model, o);
    const int steps = static_cast<int>(jobs.size() * 2);
    int completed = 0;
    if (progress) progress(completed, steps);
    double peak = 0.0;
    for (auto& job : jobs) {
        if (stopped()) return cancelled();
        auto rendered = renderClip(preset.settings, job.clip, o.sampleRate, cancel);
        if (stopped()) return cancelled();
        if (rendered.samples.empty()) return error(QStringLiteral("При рендере не получено ни одного аудиокадра."));
        peak = std::max(peak, samplePeak(rendered.samples, o.channels));
        QFile buffer(raw.filePath(job.name));
        const auto bytes = static_cast<qint64>(std::span(rendered.samples).size_bytes());
        if (!buffer.open(QIODevice::WriteOnly) || buffer.write(reinterpret_cast<const char*>(rendered.samples.data()), bytes) != bytes ||
            !buffer.flush()) return error(buffer.errorString());

        const auto frames = static_cast<qint64>(rendered.samples.size());
        const bool steady = job.clip.motion == Motion::Steady;
        job.meta = {{"file", job.name + QStringLiteral(".wav")}, {"engine", preset.name}, {"role", job.name},
                    {"rpm", steady ? std::clamp(job.clip.rpm, preset.settings.model.idleRpm, preset.settings.model.maxRpm) : rendered.startRpm},
                    {"load", steady ? job.clip.load : rendered.startLoad}, {"throttle", job.clip.throttle},
                    {"rpmStart", rendered.startRpm}, {"rpmEnd", rendered.endRpm},
                    {"rpmPeak", rendered.peakRpm}, {"loadStart", rendered.startLoad}, {"loadEnd", rendered.endLoad},
                    {"frames", frames}, {"durationSeconds", double(frames) / o.sampleRate}, {"requestedSeconds", job.clip.seconds},
                    {"loop", job.clip.loop ? QJsonValue(QJsonObject{{"startFrame", 0}, {"endFrameExclusive", frames}}) : QJsonValue(QJsonValue::Null)}};
        if (progress) progress(++completed, steps);
    }

    const double gain = o.normalize && peak > 1e-8 ? std::pow(10.0, -1.0 / 20.0) / peak : 1.0;
    QJsonArray assets;
    for (const auto& job : jobs) {
        if (stopped()) return cancelled();
        QFile buffer(raw.filePath(job.name));
        const auto count = job.meta.value(QStringLiteral("frames")).toInteger();
        std::vector<StereoFrame> signal(static_cast<std::size_t>(count));
        const auto bytes = static_cast<qint64>(std::span(signal).size_bytes());
        if (!buffer.open(QIODevice::ReadOnly) || buffer.read(reinterpret_cast<char*>(signal.data()), bytes) != bytes)
            return error(buffer.errorString());
        const auto problem = makeWav(stage.filePath(job.meta.value(QStringLiteral("file")).toString()), o, signal, job.clip.loop, gain);
        if (!problem.isEmpty()) return error(problem);
        assets.append(job.meta);
        if (progress) progress(++completed, steps);
    }

    const QString mode = o.mode == ExportMode::Pack ? QStringLiteral("pack") :
        o.mode == ExportMode::Grid ? QStringLiteral("grid") : o.mode == ExportMode::Drive ? QStringLiteral("drive") : QStringLiteral("single");
    const QJsonObject manifest{{"version", 1}, {"engine", preset.name}, {"mode", mode}, {"sampleRate", o.sampleRate},
        {"bitsPerSample", o.bits}, {"channels", o.channels}, {"normalized", o.normalize && peak > 1e-8},
        {"normalizationGain", gain}, {"seed", static_cast<qint64>(preset.settings.model.seed)},
        {"listener", listenerJson(preset.settings.listener)}, {"clips", assets}};
    QSaveFile file(stage.filePath(QStringLiteral("manifest.json")));
    const auto data = QJsonDocument(manifest).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) return error(file.errorString());
    const auto presetError = savePreset(stage.filePath(QStringLiteral("preset.json")), preset);
    if (!presetError.isEmpty()) return error(presetError);
    if (stopped()) return cancelled();

    const QString prefix = slug(preset.name) + QLatin1Char('_') + mode;
    const auto from = QFileInfo(working.path()).fileName();
    for (int i = 1; i < 10000; ++i) {
        const QString target = i == 1 ? prefix : prefix + QLatin1Char('_') + QString::number(i);
        if (dest.rename(from, target)) {
            working.setAutoRemove(false);
            return {dest.filePath(target), {}, static_cast<int>(jobs.size()), false};
        }
        if (!dest.exists(target)) return error(QStringLiteral("Не удалось переместить WAV-файлы в папку %1.").arg(dest.path()));
    }
    return error(QStringLiteral("Не удалось подобрать свободное имя папки экспорта."));
}

} // namespace esl
