#include "app/dashboard.hpp"
#include "app/main_window.hpp"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>

#include <windows.h>
#include <dwmapi.h>
#include <cmath>

namespace {

int exportDemos(const QString& destination) {
    using namespace esl;
    Preset p;
    auto load = [&](const QString& name) { return loadPreset(QStringLiteral(":/presets/") + name + QStringLiteral(".json"), p).isEmpty(); };
    auto write = [&](const ExportOptions& o) { return exportAudio(p, o, destination).wavCount > 0; };
    ExportOptions pack;
    pack.mode = ExportMode::Pack;
    pack.seconds = 3.0;
    pack.normalize = false;
    ExportOptions listening;
    listening.seconds = 3.6;
    listening.rpm = 1600;
    listening.throttle = 0.40;
    listening.load = 0.45;
    listening.bits = 16;
    listening.normalize = false;
    const QStringList fleetPacks{"fleet_oshkosh_het", "fleet_kamaz_5320", "fleet_btr82a", "fleet_btr90"};
    const QStringList referencePacks{QStringLiteral("01_t72b3"), QStringLiteral("02_t90m"),
                             QStringLiteral("04b_leopard2_field"), QStringLiteral("04c_leopard1"),
                             QStringLiteral("04d_marder1"), QStringLiteral("04e_puma"),
                             QStringLiteral("04f_bradley"), QStringLiteral("09_hummer_h2"),
                             QStringLiteral("03b_bmp12"), QStringLiteral("03c_bmp3"),
                             QStringLiteral("10_dci23"), QStringLiteral("11_uaz"), QStringLiteral("04g_stryker"),
                             QStringLiteral("04h_ariete"), QStringLiteral("04i_m113")};
    for (const auto& name : referencePacks + fleetPacks) {
        if (!load(name) || !write(pack)) return 2;
        for (const auto position : {ListeningPosition::Near, ListeningPosition::ThirdPerson,
                                    ListeningPosition::Interior, ListeningPosition::Distant}) {
            auto& listener = p.settings.listener;
            listener.position = position;
            listener.distance = defaultDistance(position);
            const auto folder = listenerJson(listener).value(QStringLiteral("position")).toString();
            const auto path = QDir(destination).filePath(QStringLiteral("perspectives/") + folder);
            if (exportAudio(p, listening, path).wavCount != 1) return 2;
        }
    }
    if (!load(QStringLiteral("04_leopard2")) || !write(pack)) return 2;
    if (!load(QStringLiteral("07_gasoline_i4")) || !write(pack)) return 2;
    pack.normalize = true;
    if (!load(QStringLiteral("05_m1_abrams")) || !write(pack)) return 2;

    ExportOptions grid;
    grid.mode = ExportMode::Grid;
    grid.seconds = 2.6;
    grid.sampleRate = 44100;
    grid.bits = 16;
    grid.channels = 1;
    grid.rpmMin = 1100;
    grid.rpmMax = 2000;
    grid.rpmCount = 2;
    if (!load(QStringLiteral("03_bmp2")) || !write(grid)) return 2;
    grid.rpmMin = 1000;
    grid.rpmMax = 2600;
    grid.rpmCount = 9;
    grid.loadMin = 0.45;
    grid.loadCount = 1;
    grid.normalize = false;
    if (!load(QStringLiteral("01_t72b3")) || !write(grid)) return 2;

    ExportOptions single;
    single.seconds = 3.2;
    single.rpm = 1900;
    single.throttle = 0.32;
    single.load = 0.58;
    if (!load(QStringLiteral("08_gasoline_v8")) || !write(single)) return 2;
    p.name = QStringLiteral("V8 Misfire");
    p.description = QStringLiteral("Бензиновый V8 с периодическим пропуском искры на третьем цилиндре и переливом топлива");
    p.settings.faults = {3, 0.64, 0.37, 0.0, 0.34, 0.28};
    if (!write(single)) return 2;
    if (!load(QStringLiteral("04_leopard2"))) return 2;
    single.rpm = p.settings.model.idleRpm;
    single.throttle = 0.0;
    single.load = 0.12;
    if (!write(single)) return 2;
    if (!load(QStringLiteral("08_gasoline_v8"))) return 2;
    p.name = QStringLiteral("V8 Rev Limiter");
    single.rpm = p.settings.model.maxRpm;
    single.throttle = 1.0;
    single.load = 0.55;
    single.seconds = 4.5;
    single.singleLoop = false;
    if (!write(single)) return 2;

    const QDir resources(QStringLiteral(":/presets"));
    const auto libraryPath = QDir(destination).filePath(QStringLiteral("library"));
    for (const auto& file : resources.entryList({QStringLiteral("fleet_*.json")}, QDir::Files)) {
        const auto name = QFileInfo(file).completeBaseName();
        if (fleetPacks.contains(name)) continue;
        if (!load(name)) return 2;
        listening.rpm = std::lerp(p.settings.model.idleRpm, p.settings.model.maxRpm, 0.48);
        if (exportAudio(p, listening, libraryPath).wavCount != 1) return 2;
    }
    if (!load(QStringLiteral("12_oshkosh_drive"))) return 2;
    ExportOptions ride;
    ride.mode = ExportMode::Drive;
    ride.bits = 16;
    ride.normalize = false;
    auto& transmission = p.settings.layers[static_cast<std::size_t>(LayerId::Transmission)];
    for (const auto& stem : {QStringLiteral("mix"), QStringLiteral("engine"), QStringLiteral("transmission")}) {
        transmission.mute = stem == QStringLiteral("engine");
        transmission.solo = stem == QStringLiteral("transmission");
        if (exportAudio(p, ride, QDir(destination).filePath(QStringLiteral("drive/") + stem)).wavCount != 1) return 2;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && QByteArray(argv[1]) == "--export-demo") {
        QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        return args.size() == 3 ? exportDemos(args[2]) : 1;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Engine Sound Lab"));
    QApplication::setApplicationVersion(QStringLiteral(ESL_VERSION));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#131f2b"));
    palette.setColor(QPalette::WindowText, QColor("#e5ecf3"));
    palette.setColor(QPalette::Base, QColor("#192839"));
    palette.setColor(QPalette::AlternateBase, QColor("#213344"));
    palette.setColor(QPalette::Text, QColor("#e5ecf3"));
    palette.setColor(QPalette::Button, QColor("#192839"));
    palette.setColor(QPalette::ButtonText, QColor("#e5ecf3"));
    palette.setColor(QPalette::Highlight, QColor("#366b69"));
    palette.setColor(QPalette::HighlightedText, QColor("#f3fffd"));
    app.setPalette(palette);
    app.setStyleSheet(esl::dashboardStyle());

    const auto args = app.arguments();
    esl::MainWindow window(args.value(1) == QStringLiteral("--preset") ? args.value(2) : QString{});
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        const auto available = screen->availableGeometry();
        window.resize(window.size().boundedTo(available.size() - QSize(30, 56)));
        window.move(available.center() - QPoint(window.width() / 2, window.height() / 2));
    }
    const BOOL dark = TRUE;
    const HWND native = reinterpret_cast<HWND>(window.winId());
    DwmSetWindowAttribute(native, 20, &dark, sizeof(dark));
    window.show();
    return app.exec();
}
