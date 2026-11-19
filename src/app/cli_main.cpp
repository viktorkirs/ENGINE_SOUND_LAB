#include "app/audio_exporter.hpp"
#include "core/recording.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include <cstdio>
#include <limits>
#include <utility>

namespace {

using namespace esl;

void printJson(FILE* stream, const QJsonObject& object) {
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stream);
    std::fputc('\n', stream);
}

int success(QJsonObject result) {
    result.insert("ok", true);
    result.insert("schemaVersion", 1);
    printJson(stdout, result);
    return 0;
}

int fail(int exitCode, const QString& code, const QString& message) {
    printJson(stderr, {{"ok", false}, {"schemaVersion", 1}, {"error", QJsonObject{{"code", code}, {"message", message}}}});
    return exitCode;
}

struct Catalog {
    QMap<QString, Preset> presets;
    QJsonArray vehicles;
};

QString loadCatalog(Catalog& catalog) {
    const QDir directory(QStringLiteral(":/presets"));
    const auto files = directory.entryList({QStringLiteral("*.json")}, QDir::Files);
    const auto vehicles = fleetIndex();
    if (files.isEmpty() || vehicles.isEmpty()) return QStringLiteral("The embedded catalog is unavailable.");

    QHash<QString, QString> idsByName;
    for (const auto& file : files) {
        Preset preset;
        if (const auto error = loadPreset(directory.filePath(file), preset); !error.isEmpty())
            return QStringLiteral("%1: %2").arg(file, error);
        const auto id = QFileInfo(file).completeBaseName();
        if (idsByName.contains(preset.name)) return QStringLiteral("Duplicate preset name: %1").arg(preset.name);
        idsByName.insert(preset.name, id);
        catalog.presets.insert(id, std::move(preset));
    }
    for (const auto& vehicle : vehicles) {
        auto row = vehicle.toObject();
        const auto name = row.take("preset").toString();
        const auto id = idsByName.value(name);
        if (!name.isEmpty() && id.isEmpty()) return QStringLiteral("Vehicle references an unknown preset: %1").arg(name);
        row.insert("presetId", id.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(id));
        catalog.vehicles.append(row);
    }
    return {};
}

QJsonArray recordedRpm(const Preset& preset) {
    QJsonArray rpm;
    if (const auto* recording = preset.settings.model.recording) {
        for (const auto& loop : recording->loops) rpm.append(loop.rpm);
    }
    return rpm;
}

int listCatalog(const Catalog& catalog) {
    QJsonArray presets;
    for (auto it = catalog.presets.cbegin(); it != catalog.presets.cend(); ++it) {
        const auto& preset = it.value();
        const auto model = presetJson(preset).value("model").toObject();
        presets.append(QJsonObject{{"id", it.key()}, {"name", preset.name}, {"description", preset.description},
            {"kind", model.value("kind")}, {"cylinders", model.value("cylinders")}, {"idleRpm", model.value("idleRpm")},
            {"maxRpm", model.value("maxRpm")}, {"recording", model.value("recording")}, {"recordedRpm", recordedRpm(preset)}});
    }
    return success({{"appVersion", QCoreApplication::applicationVersion()}, {"presets", presets}, {"vehicles", catalog.vehicles}});
}

QString readOptions(const QString& path, ExportOptions& options) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return file.errorString();
    if (file.size() > 64 * 1024) return QStringLiteral("The options file exceeds 64 KiB.");
    QJsonParseError parse;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError) return parse.errorString();
    if (!json.isObject()) return QStringLiteral("The options file must contain a JSON object.");

    QHash<QString, int ExportOptions::*> integers{{"sampleRate", &ExportOptions::sampleRate},
        {"bitsPerSample", &ExportOptions::bits}, {"channels", &ExportOptions::channels}};
    QHash<QString, double ExportOptions::*> numbers;
    QHash<QString, bool ExportOptions::*> flags{{"normalize", &ExportOptions::normalize}};
    if (options.mode != ExportMode::Drive) numbers.insert("seconds", &ExportOptions::seconds);
    if (options.mode == ExportMode::Grid) {
        integers.insert("rpmCount", &ExportOptions::rpmCount);
        integers.insert("loadCount", &ExportOptions::loadCount);
        numbers.insert("rpmMin", &ExportOptions::rpmMin);
        numbers.insert("rpmMax", &ExportOptions::rpmMax);
        numbers.insert("loadMin", &ExportOptions::loadMin);
        numbers.insert("loadMax", &ExportOptions::loadMax);
    }
    if (options.mode == ExportMode::Single) {
        numbers.insert("rpm", &ExportOptions::rpm);
        numbers.insert("throttle", &ExportOptions::throttle);
        numbers.insert("load", &ExportOptions::load);
        flags.insert("singleLoop", &ExportOptions::singleLoop);
    }

    const auto fields = json.object();
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
        const auto& key = it.key();
        const auto value = it.value();
        if (const auto member = integers.value(key)) {
            const auto n = value.toInteger(std::numeric_limits<qint64>::min());
            if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
                return QStringLiteral("Expected an integer for %1.").arg(key);
            options.*member = static_cast<int>(n);
        } else if (const auto member = numbers.value(key)) {
            if (!value.isDouble()) return QStringLiteral("Expected a number for %1.").arg(key);
            options.*member = value.toDouble();
        } else if (const auto member = flags.value(key)) {
            if (!value.isBool()) return QStringLiteral("Expected a boolean for %1.").arg(key);
            options.*member = value.toBool();
        } else {
            return QStringLiteral("Unknown option for this export mode: %1").arg(key);
        }
    }
    return {};
}

int run(const QStringList& args) {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Render Engine Sound Lab presets without a desktop. See docs/agent-cli.md."));
    const auto help = parser.addHelpOption();
    const auto version = parser.addVersionOption();
    parser.addPositionalArgument("command", "catalog, show-preset, or export");
    const QCommandLineOption presetId("preset", "Built-in preset ID from catalog.", "id");
    const QCommandLineOption vehicleId("vehicle", "Game vehicle ID from catalog.", "id");
    const QCommandLineOption presetFile("preset-file", "Path to a version 1 preset JSON.", "file");
    const QCommandLineOption output("output", "Destination directory for export.", "directory");
    const QCommandLineOption mode("mode", "Export mode: single, pack, grid, or drive.", "mode", "pack");
    const QCommandLineOption optionsFile("options", "Path to a JSON file with export options.", "file");
    parser.addOptions({presetId, vehicleId, presetFile, output, mode, optionsFile});
    if (!parser.parse(args)) return fail(2, "invalid_arguments", parser.errorText());
    if (args.size() == 1 || parser.isSet(help)) {
        std::fputs(parser.helpText().toUtf8().constData(), stdout);
        return 0;
    }
    if (parser.isSet(version)) {
        std::puts(QCoreApplication::applicationVersion().toUtf8().constData());
        return 0;
    }

    const auto arguments = parser.positionalArguments();
    const auto command = arguments.value(0);
    if (arguments.size() != 1 || (command != "catalog" && command != "show-preset" && command != "export"))
        return fail(2, "invalid_arguments", "Expected one command: catalog, show-preset, or export.");
    const auto selected = parser.optionNames();
    for (const auto& option : selected) {
        if (selected.count(option) > 1 || parser.value(option).isEmpty())
            return fail(2, "invalid_arguments", QStringLiteral("Provide one nonempty value for --%1.").arg(option));
    }
    if (command == "catalog" && !selected.isEmpty()) return fail(2, "invalid_arguments", "catalog takes no options.");
    const int selectors = parser.isSet(presetId) + parser.isSet(vehicleId) + parser.isSet(presetFile);
    if (command != "catalog" && selectors != 1)
        return fail(2, "invalid_arguments", "Select one of --preset, --vehicle, or --preset-file.");
    if (command == "show-preset" && selected.size() != 1)
        return fail(2, "invalid_arguments", "show-preset takes only a preset selector.");
    if (command == "export" && !parser.isSet(output)) return fail(2, "invalid_arguments", "export requires --output.");

    Preset preset;
    QJsonObject result;
    if (parser.isSet(presetFile)) {
        const auto path = QFileInfo(parser.value(presetFile)).absoluteFilePath();
        if (const auto error = loadPreset(path, preset); !error.isEmpty()) return fail(3, "invalid_preset", error);
    } else {
        Catalog catalog;
        if (const auto error = loadCatalog(catalog); !error.isEmpty()) return fail(5, "catalog_failed", error);
        if (command == "catalog") return listCatalog(catalog);

        auto id = parser.value(presetId);
        if (parser.isSet(vehicleId)) {
            const auto vehicle = parser.value(vehicleId);
            for (const auto& item : catalog.vehicles) {
                const auto row = item.toObject();
                if (row.value("id").toString() != vehicle) continue;
                id = row.value("presetId").toString();
                if (id.isEmpty()) return fail(3, "vehicle_has_no_preset", vehicle);
                break;
            }
            if (id.isEmpty()) return fail(3, "unknown_vehicle", vehicle);
            result.insert("vehicleId", vehicle);
        }
        const auto found = catalog.presets.constFind(id);
        if (found == catalog.presets.cend()) return fail(3, "unknown_preset", id);
        preset = found.value();
        result.insert("presetId", id);
    }
    if (command == "show-preset") {
        result.insert("preset", presetJson(preset));
        result.insert("recordedRpm", recordedRpm(preset));
        return success(result);
    }

    const QHash<QString, ExportMode> modes{{"single", ExportMode::Single}, {"pack", ExportMode::Pack},
        {"grid", ExportMode::Grid}, {"drive", ExportMode::Drive}};
    const auto exportMode = modes.constFind(parser.value(mode));
    if (exportMode == modes.cend()) return fail(2, "invalid_arguments", "Unknown export mode.");
    ExportOptions options;
    options.mode = exportMode.value();
    options.rpm = options.rpmMin = preset.settings.model.idleRpm;
    options.rpmMax = preset.settings.model.maxRpm;
    options.throttle = 0.0;
    options.load = 0.12;
    if (parser.isSet(optionsFile)) {
        if (const auto error = readOptions(parser.value(optionsFile), options); !error.isEmpty())
            return fail(2, "invalid_options", error);
    }
    if (options.mode == ExportMode::Single && (options.rpm < options.rpmMin || options.rpm > options.rpmMax))
        return fail(2, "invalid_options", "rpm must be between the preset's idleRpm and maxRpm.");

    const auto exported = exportAudio(preset, options, parser.value(output));
    if (exported.wavCount <= 0) return fail(4, "export_failed", exported.error);
    const QDir directory(exported.directory);
    result.insert("directory", directory.absolutePath());
    result.insert("manifest", directory.absoluteFilePath("manifest.json"));
    result.insert("presetFile", directory.absoluteFilePath("preset.json"));
    result.insert("wavCount", exported.wavCount);
    return success(result);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    application.setApplicationVersion(QStringLiteral(ESL_VERSION));
    return run(application.arguments());
}
