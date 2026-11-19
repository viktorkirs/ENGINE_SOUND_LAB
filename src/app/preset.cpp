#include "app/preset.hpp"
#include "app/recording_bank.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace esl {
namespace {

constexpr std::array kindNames{"diesel", "gasoline", "turbine"};
constexpr std::array layoutNames{"inline", "v", "boxer", "radial"};
constexpr std::array boostNames{"none", "turbocharger", "supercharger"};
constexpr std::array layerNames{"combustion", "exhaust", "intake", "mechanical", "turbo", "body", "recording", "transmission"};
static_assert(layerNames.size() == layerCount);
constexpr std::array positionNames{"direct", "interior", "near", "thirdPerson", "distant"};
constexpr std::array programNames{"manual", "idle", "slowRamp", "fastRamp", "fullLoad", "lift", "engineBrake", "startStop", "autoTest", "drive"};

struct Reader {
    QString error;

    void invalid(const char* key) {
        if (error.isEmpty()) error = QStringLiteral("Недопустимое значение в поле «%1».").arg(QLatin1String(key));
    }

    QJsonObject object(const QJsonObject& from, const char* key) {
        const auto item = from.value(QLatin1String(key));
        if (!item.isUndefined() && !item.isObject()) invalid(key);
        return item.toObject();
    }

    double number(const QJsonObject& from, const char* key, double fallback, double min, double max) {
        const auto item = from.value(QLatin1String(key));
        if (item.isUndefined()) return fallback;
        const double x = item.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(x) || x < min || x > max) {
            invalid(key);
            return fallback;
        }
        return x;
    }

    int integer(const QJsonObject& from, const char* key, int fallback, int min, int max) {
        const double x = number(from, key, fallback, min, max);
        if (x != std::floor(x)) invalid(key);
        return static_cast<int>(x);
    }

    bool flag(const QJsonObject& from, const char* key, bool fallback) {
        const auto item = from.value(QLatin1String(key));
        if (item.isUndefined()) return fallback;
        if (!item.isBool()) invalid(key);
        return item.toBool(fallback);
    }

    template <typename Enum, std::size_t N>
    Enum enumeration(const QJsonObject& from, const char* key, Enum fallback, const std::array<const char*, N>& names) {
        const auto item = from.value(QLatin1String(key));
        if (item.isUndefined()) return fallback;
        const auto name = item.toString();
        for (std::size_t i = 0; i < N; ++i) {
            if (name == QLatin1String(names[i])) return static_cast<Enum>(i);
        }
        invalid(key);
        return fallback;
    }
};

} // namespace

QString listeningPositionName(ListeningPosition position) {
    static const std::array names{QStringLiteral("У двигателя"), QStringLiteral("Внутри корпуса"),
        QStringLiteral("Снаружи рядом"), QStringLiteral("От третьего лица"), QStringLiteral("Вдалеке")};
    return names[static_cast<std::size_t>(position)];
}

QJsonObject listenerJson(const Listener& listener) {
    return {{"position", QLatin1String(positionNames[static_cast<std::size_t>(listener.position)])},
            {"distanceMeters", listener.distance}, {"insulation", listener.insulation}};
}

QString firingOrderText(const Model& model) {
    const auto order = model.firingOrder[0] == 0 ? defaultFiringOrder(model) : model.firingOrder;
    QStringList text;
    for (int i = 0; i < model.cylinders; ++i) text.append(QString::number(order[i]));
    return text.join(QStringLiteral(" · "));
}

bool parseFiringOrder(const QString& text, const Model& model, FiringOrder& order) {
    FiringOrder candidate{};
    if (!text.trimmed().isEmpty()) {
        static const QRegularExpression separator(QStringLiteral("[\\s,;·–-]+"));
        const auto parts = text.trimmed().split(separator, Qt::SkipEmptyParts);
        if (parts.size() != model.cylinders) return false;
        for (int i = 0; i < parts.size(); ++i) {
            bool ok = false;
            candidate[i] = parts[i].toInt(&ok);
            if (!ok) return false;
        }
        if (candidate[0] == 0 || !validFiringOrder(model, candidate)) return false;
    }
    order = candidate;
    return true;
}

QString loadPreset(const QString& path, Preset& preset) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (file.size() > 256 * 1024) return QStringLiteral("Файл пресета превышает 256 КБ.");
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) return parse.errorString();
    const auto root = doc.object();
    if (root.value(QStringLiteral("version")).toDouble() != 1.0)
        return QStringLiteral("Версия формата пресета должна быть 1.");

    Preset candidate;
    candidate.name = root.value(QStringLiteral("name")).toString().trimmed();
    if (candidate.name.isEmpty() || candidate.name.size() > 100) return QStringLiteral("Укажите название пресета (до 100 символов).");
    candidate.description = root.value(QStringLiteral("description")).toString();
    if (candidate.description.size() > 500) return QStringLiteral("Описание пресета превышает 500 символов.");
    Reader read;
    const auto m = read.object(root, "model");
    auto& model = candidate.settings.model;
    model.kind = read.enumeration(m, "kind", model.kind, kindNames);
    model.layout = read.enumeration(m, "layout", model.layout, layoutNames);
    model.cylinders = read.integer(m, "cylinders", model.cylinders, 1, maxCylinders);
    model.strokes = read.integer(m, "strokes", model.strokes, 2, 4);
    if (model.strokes == 3) read.invalid("strokes");
    if (!validCylinderCount(model.layout, model.cylinders)) read.invalid("cylinders");
    model.liters = read.number(m, "liters", model.liters, 0.05, 120.0);
    model.strokeMm = read.number(m, "strokeMm", pistonStrokeMm(model), 10.0, 650.0);
    model.compression = read.number(m, "compressionRatio", compressionRatio(model), 3.0, 30.0);
    model.idleRpm = read.number(m, "idleRpm", model.idleRpm, 100.0, 5000.0);
    model.maxRpm = read.number(m, "maxRpm", model.maxRpm, model.idleRpm + 50, 18000.0);
    if (model.maxRpm < model.idleRpm + 50) read.invalid("maxRpm");
    model.bankAngle = read.number(m, "bankAngle", model.bankAngle, 10.0, 180.0);
    model.irregularity = read.number(m, "irregularity", model.irregularity, 0.0, 1.0);
    model.inertia = read.number(m, "inertia", model.inertia, 0.05, 200.0);
    model.powerKw = read.number(m, "powerKw", model.powerKw, 1.0, 2200.0);
    model.torqueNm = read.number(m, "torqueNm", model.torqueNm, 1.0, 16000.0);
    model.loss = read.number(m, "loss", model.loss, 0.0, 1.0);
    model.overrunDrag = read.number(m, "overrunDrag", model.overrunDrag, 0.0, 1.0);
    model.exhaustTracts = read.integer(m, "exhaustTracts", model.exhaustTracts, 1, 2);
    model.boost = read.enumeration(m, "boost", model.boost, boostNames);
    model.coreMaxRpm = read.number(m, "coreMaxRpm", model.coreMaxRpm, 5000.0, 65000.0);
    model.coreIdle = read.number(m, "coreIdle", model.coreIdle, 0.20, 0.87);
    model.coreLag = read.number(m, "coreLag", model.coreLag, 0.15, 3.5);
    model.compressorBlades = read.integer(m, "compressorBlades", model.compressorBlades, 4, 48);
    model.turbineBlades = read.integer(m, "turbineBlades", model.turbineBlades, 4, 64);
    const double seed = read.number(m, "seed", model.seed, 0, 4294967295.0);
    if (seed != std::floor(seed)) read.invalid("seed");
    model.seed = static_cast<std::uint32_t>(seed);
    const auto recording = m.value(QStringLiteral("recording"));
    const auto key = recording.isUndefined() ? QStringLiteral("none") : recording.toString();
    const int source = recordingKeys().indexOf(key);
    model.recording = builtinRecording(static_cast<RecordingId>(source));
    if (source < 0 || (source != 0 && !model.recording)) read.invalid("recording");
    const auto order = m.value(QStringLiteral("firingOrder"));
    if (!order.isUndefined()) {
        const auto data = order.toArray();
        if (!order.isArray() || (!data.isEmpty() && data.size() != model.cylinders)) {
            read.invalid("firingOrder");
        } else {
            for (int i = 0; i < data.size(); ++i) model.firingOrder[i] = data[i].toInt(-1);
            if ((!data.isEmpty() && model.firingOrder[0] == 0) || !validFiringOrder(model, model.firingOrder)) read.invalid("firingOrder");
        }
    }

    const auto layers = read.object(root, "layers");
    for (std::size_t i = 0; i < layerNames.size(); ++i) {
        const auto data = read.object(layers, layerNames[i]);
        auto& layer = candidate.settings.layers[i];
        layer.gain = read.number(data, "gain", layer.gain, 0.0, 1.5);
        layer.resonance = read.number(data, "resonance", layer.resonance, 25.0, 9000.0);
        layer.cutoff = read.number(data, "cutoff", layer.cutoff, 90.0, 14000.0);
        layer.shape = read.number(data, "shape", layer.shape, 0.0, 1.0);
        layer.amount = read.number(data, "amount", layer.amount, 0.0, 1.0);
        layer.damping = read.number(data, "damping", layer.damping, 0.0, 1.0);
        layer.drive = read.number(data, "drive", layer.drive, 0.0, 1.0);
        layer.mute = read.flag(data, "mute", layer.mute);
        layer.solo = read.flag(data, "solo", layer.solo);
    }
    const auto behavior = read.object(root, "behavior");
    auto& controls = candidate.settings.controls;
    controls.throttle = read.number(behavior, "throttle", controls.throttle, 0.0, 1.0);
    controls.load = read.number(behavior, "load", controls.load, 0.0, 1.0);
    controls.hold = read.flag(behavior, "hold", controls.hold);
    controls.holdRpm = read.number(behavior, "holdRpm", std::clamp(controls.holdRpm, model.idleRpm, model.maxRpm),
                                   model.idleRpm, model.maxRpm);
    controls.gear = read.integer(behavior, "gear", controls.gear, 0, 6);
    controls.brake = read.flag(behavior, "brake", controls.brake);
    controls.master = read.number(behavior, "master", controls.master, 0.0, 1.0);
    controls.program = read.enumeration(behavior, "program", controls.program, programNames);
    const auto acoustics = read.object(root, "listener");
    auto& listener = candidate.settings.listener;
    listener.position = read.enumeration(acoustics, "position", listener.position, positionNames);
    listener.distance = read.number(acoustics, "distanceMeters", defaultDistance(listener.position), 1.0, 120.0);
    listener.insulation = read.number(acoustics, "insulation", listener.insulation, 0.0, 1.0);
    const auto cut = read.object(root, "revLimiter");
    auto& limiter = candidate.settings.limiter;
    limiter = defaultRevLimiter(model);
    limiter.enabled = read.flag(cut, "enabled", false);
    limiter.cutRpm = read.number(cut, "cutRpm", limiter.cutRpm, model.idleRpm + 1.0, model.maxRpm);
    limiter.resumeRpm = read.number(cut, "resumeRpm", std::min(limiter.resumeRpm, limiter.cutRpm - 1.0),
                                    model.idleRpm, limiter.cutRpm - 1.0);
    const auto faultGroup = read.object(root, "faults");
    auto& fault = candidate.settings.faults;
    fault.cylinder = read.integer(faultGroup, "cylinder", fault.cylinder, 1, model.cylinders);
    fault.sparkMiss = read.number(faultGroup, "sparkMiss", fault.sparkMiss, 0.0, 1.0);
    fault.fuelBias = read.number(faultGroup, "fuelBias", fault.fuelBias, -1.0, 1.0);
    fault.injectorWear = read.number(faultGroup, "injectorWear", fault.injectorWear, 0.0, 1.0);
    fault.overload = read.number(faultGroup, "overload", fault.overload, 0.0, 1.0);
    fault.mechanicalWear = read.number(faultGroup, "mechanicalWear", fault.mechanicalWear, 0.0, 1.0);
    if (!read.error.isEmpty()) return read.error;

    preset = std::move(candidate);
    return {};
}

QJsonObject presetJson(const Preset& preset) {
    const auto& model = preset.settings.model;
    QJsonArray order;
    if (model.firingOrder[0] != 0) {
        for (int i = 0; i < model.cylinders; ++i) order.append(model.firingOrder[i]);
    }
    const auto name = [](const auto& list, auto index) { return QLatin1String(list[static_cast<std::size_t>(index)]); };
    QJsonObject m{
        {"kind", name(kindNames, model.kind)}, {"layout", name(layoutNames, model.layout)}, {"cylinders", model.cylinders},
        {"strokes", model.strokes}, {"liters", model.liters}, {"idleRpm", model.idleRpm}, {"maxRpm", model.maxRpm},
        {"strokeMm", pistonStrokeMm(model)}, {"compressionRatio", compressionRatio(model)},
        {"bankAngle", model.bankAngle}, {"firingOrder", order}, {"irregularity", model.irregularity},
        {"inertia", model.inertia}, {"powerKw", model.powerKw}, {"torqueNm", model.torqueNm}, {"loss", model.loss},
        {"exhaustTracts", model.exhaustTracts}, {"boost", name(boostNames, model.boost)}, {"coreMaxRpm", model.coreMaxRpm},
        {"recording", recordingKeys().value(static_cast<int>(model.recording ? model.recording->id : RecordingId::None))},
        {"coreIdle", model.coreIdle}, {"coreLag", model.coreLag}, {"compressorBlades", model.compressorBlades},
        {"turbineBlades", model.turbineBlades}, {"seed", static_cast<qint64>(model.seed)}
    };
    if (model.overrunDrag > 0.0) m.insert(QStringLiteral("overrunDrag"), model.overrunDrag);
    QJsonObject layers;
    for (std::size_t i = 0; i < layerNames.size(); ++i) {
        const auto& layer = preset.settings.layers[i];
        layers.insert(name(layerNames, i), QJsonObject{{"gain", layer.gain}, {"resonance", layer.resonance},
            {"cutoff", layer.cutoff}, {"shape", layer.shape}, {"amount", layer.amount}, {"damping", layer.damping},
            {"drive", layer.drive}, {"mute", layer.mute}, {"solo", layer.solo}});
    }
    const auto& c = preset.settings.controls;
    const QJsonObject behavior{{"throttle", c.throttle}, {"load", c.load}, {"hold", c.hold}, {"holdRpm", c.holdRpm},
        {"gear", c.gear}, {"brake", c.brake}, {"master", c.master}, {"program", name(programNames, c.program)}};
    const auto& f = preset.settings.faults;
    const auto& limiter = preset.settings.limiter;
    const QJsonObject cut{{"enabled", limiter.enabled}, {"cutRpm", limiter.cutRpm}, {"resumeRpm", limiter.resumeRpm}};
    const QJsonObject faults{{"cylinder", f.cylinder}, {"sparkMiss", f.sparkMiss}, {"fuelBias", f.fuelBias},
        {"injectorWear", f.injectorWear}, {"overload", f.overload}, {"mechanicalWear", f.mechanicalWear}};
    return {{"version", 1}, {"name", preset.name}, {"description", preset.description}, {"model", m},
            {"layers", layers}, {"behavior", behavior}, {"faults", faults}, {"listener", listenerJson(preset.settings.listener)},
            {"revLimiter", cut}};
}

QString savePreset(const QString& path, const Preset& preset) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    const auto json = QJsonDocument(presetJson(preset)).toJson();
    if (file.write(json) != json.size() || !file.commit()) return file.errorString();
    return {};
}

QJsonArray fleetIndex() {
    QFile file(QStringLiteral(":/library/fleet.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array();
}

} // namespace esl
