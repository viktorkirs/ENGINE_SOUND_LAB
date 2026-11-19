#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>

namespace {

void require(bool valid, const char* message) {
    if (!valid) qFatal("%s", message);
}

QJsonObject invoke(const QString& cli, const QStringList& args, int exitCode = 0) {
    QProcess process;
    process.start(cli, args);
    require(process.waitForFinished(10000), "CLI did not finish");
    const auto out = process.readAllStandardOutput();
    const auto err = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != exitCode)
        qFatal("Unexpected CLI exit %d. stdout: %s stderr: %s", process.exitCode(), out.constData(), err.constData());
    require((exitCode == 0 ? err : out).isEmpty(), "CLI mixed diagnostics with results");
    const auto json = QJsonDocument::fromJson(exitCode == 0 ? out : err).object();
    require(json.value("schemaVersion").toInt() == 1 && json.value("ok").isBool() &&
            json.value("ok").toBool() == (exitCode == 0), "Invalid CLI result envelope");
    return json;
}

QJsonObject readJson(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Could not read exported JSON");
    return QJsonDocument::fromJson(file.readAll()).object();
}

void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Could not write test options");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    require(app.arguments().size() == 2, "Pass the CLI executable path");
    const auto cli = app.arguments().at(1);
    const QTemporaryDir temp;
    require(temp.isValid(), "Could not create temporary directory");
    const QDir dir(temp.path());

    const auto catalog = invoke(cli, {"catalog"});
    require(!catalog.value("presets").toArray().isEmpty() && !catalog.value("vehicles").toArray().isEmpty(), "Empty CLI catalog");
    const auto bmp = invoke(cli, {"show-preset", "--vehicle", "east_bmp2"});
    require(bmp.value("presetId").toString() == "03b_bmp12" && bmp.value("recordedRpm").toArray().first().toInt() == 830,
            "Vehicle selector must resolve the corrected BMP bank");
    const auto noPreset = invoke(cli, {"show-preset", "--vehicle", "west_m1025_javelin"}, 3);
    require(noPreset.value("error").toObject().value("code").toString() == "vehicle_has_no_preset", "Missing vehicle diagnostic");

    const auto settings = dir.filePath("grid.json");
    write(settings, R"({"rpmMin":830,"rpmMax":1600,"rpmCount":2,"loadCount":1,"seconds":1,"channels":1,"bitsPerSample":16})");
    const QStringList grid{"export", "--vehicle", "east_bmp2", "--mode", "grid", "--options", settings,
                           "--output", dir.filePath(QStringLiteral("Audio clips"))};
    const auto result = invoke(cli, grid);
    const auto manifest = readJson(result.value("manifest").toString());
    const auto clips = manifest.value("clips").toArray();
    require(result.value("wavCount").toInt() == 2 && clips.size() == 2 && manifest.value("channels").toInt() == 1,
            "Grid ignored requested dimensions or channels");
    const auto clip = clips.first().toObject();
    const auto loop = clip.value("loop").toObject();
    require(clip.value("rpm").toInt() == 830 && loop.value("startFrame").toInt(-1) == 0 &&
            loop.value("endFrameExclusive").toInt() == clip.value("frames").toInt(-1), "Grid loop metadata is invalid");
    const auto firstDirectory = result.value("directory").toString();
    require(QFileInfo::exists(QDir(firstDirectory).filePath(clip.value("file").toString())), "Grid clip is missing");
    require(invoke(cli, grid).value("directory").toString() != firstDirectory, "Export must not overwrite previous audio");

    auto i4 = invoke(cli, {"show-preset", "--preset", "07_gasoline_i4"}).value("preset").toObject();
    auto faults = i4.value("faults").toObject();
    faults.insert("sparkMiss", 0.5);
    i4.insert("faults", faults);
    const auto presetFile = dir.filePath("I4 misfire.json");
    write(presetFile, QJsonDocument(i4).toJson());
    const auto custom = invoke(cli, {"export", "--preset-file", presetFile, "--mode", "single", "--output", temp.path()});
    require(readJson(custom.value("presetFile").toString()).value("faults").toObject().value("sparkMiss").toDouble() == 0.5,
            "CLI did not render the effective custom preset");

    write(settings, R"({"rpmCount":2.5})");
    require(invoke(cli, grid, 2).value("error").toObject().value("code").toString() == "invalid_options", "Invalid grid option was accepted");
    return EXIT_SUCCESS;
}
