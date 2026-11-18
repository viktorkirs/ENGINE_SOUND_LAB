#include "app/dashboard.hpp"
#include "app/main_window.hpp"

#include <QApplication>
#include <QDir>
#include <QPalette>
#include <QScreen>
#include <QStyleFactory>

#include <windows.h>
#include <dwmapi.h>

namespace {

int exportDemos(const QString& destination) {
    using namespace esl;
    Preset p;
    auto load = [&](const QString& name) { return loadPreset(QStringLiteral(":/presets/") + name + QStringLiteral(".json"), p).isEmpty(); };
    auto write = [&](const ExportOptions& o) { return exportAudio(p, o, destination).wavCount > 0; };
    ExportOptions pack;
    pack.mode = ExportMode::Pack;
    pack.seconds = 3.0;
    if (!load(QStringLiteral("01_t72b3")) || !write(pack)) return 2;
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
    single.rpm = 735;
    single.throttle = 0.0;
    single.load = 0.12;
    if (!load(QStringLiteral("04_leopard2")) || !write(single)) return 2;
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
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
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

    esl::MainWindow window;
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
