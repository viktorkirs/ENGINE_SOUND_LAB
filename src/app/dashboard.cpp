#include "app/dashboard.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <complex>

namespace esl {
namespace {

const QColor text("#e7eef5");
const QColor muted("#93a8bb");
const QColor dim("#46576a");
const QColor teal("#59e0c3");
const QColor cyan("#79baf6");
const QColor amber("#f6af71");

QFont face(int pixels, bool strong = false, bool mono = false) {
    QFont f(mono ? QStringLiteral("Cascadia Mono") : QStringLiteral("Segoe UI"));
    f.setPixelSize(pixels);
    if (strong) f.setWeight(QFont::DemiBold);
    return f;
}

QPointF radial(QPointF center, double radius, double position) {
    const double angle = (135.0 + 270.0 * position) * pi / 180.0;
    return center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
}

void arc(QPainter& p, const QRectF& r, double start, double end, const QPen& pen) {
    p.setPen(pen);
    p.drawArc(r, qRound((-135.0 - 270.0 * start) * 16.0), qRound(-270.0 * (end - start) * 16.0));
}

void label(QPainter& p, QRectF r, const QString& s, const QFont& f, const QColor& c, int alignment = Qt::AlignCenter) {
    p.setPen(c);
    p.setFont(f);
    p.drawText(r, alignment, s);
}

void meter(QPainter& p, QRectF r, const QString& name, double value, const QColor& color) {
    const auto align = Qt::AlignVCenter | Qt::AlignLeft;
    label(p, QRectF(r.x(), r.y(), r.width(), 16), name, face(10, true), muted, align);
    label(p, QRectF(r.x(), r.y() + 18, r.width(), 21), QString::number(qRound(100 * value)) + QLatin1Char('%'),
          face(16, true, true), text, align);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#29384a"));
    p.drawRoundedRect(QRectF(r.x(), r.y() + 43, r.width(), 3), 1.5, 1.5);
    if (value > 0.0) {
        p.setBrush(color);
        p.drawRoundedRect(QRectF(r.x(), r.y() + 43, r.width() * value, 3), 1.5, 1.5);
    }
}

constexpr float fftSize = scopeFrames;
const auto fftWindow = [] {
    std::array<float, scopeFrames> out{};
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = 0.5f - 0.5f * std::cos(2 * pi * i / (out.size() - 1));
    return out;
}();

void fft(std::array<std::complex<float>, scopeFrames>& points) {
    for (std::size_t i = 1, j = 0; i < points.size(); ++i) {
        std::size_t bit = points.size() >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(points[i], points[j]);
    }
    for (std::size_t length = 2; length <= points.size(); length *= 2) {
        const auto step = std::polar(1.0f, static_cast<float>(-2 * pi / length));
        for (std::size_t base = 0; base < points.size(); base += length) {
            std::complex<float> w{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto a = points[base + j];
                const auto b = points[base + j + length / 2] * w;
                points[base + j] = a + b;
                points[base + j + length / 2] = a - b;
                w *= step;
            }
        }
    }
}

} // namespace

QString engineModeName(EngineMode mode) {
    switch (mode) {
    case EngineMode::Off: return QStringLiteral("Выключен");
    case EngineMode::Starting: return QStringLiteral("Запуск");
    case EngineMode::Idle: return QStringLiteral("Холостой ход");
    case EngineMode::Running: return QStringLiteral("Работа");
    case EngineMode::Braking: return QStringLiteral("Торможение");
    case EngineMode::Stopping: return QStringLiteral("Остановка");
    }
    return {};
}

Tachometer::Tachometer(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(194);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Тахометр"));
}

void Tachometer::setData(const Model& model, const EngineState& engine, bool hold, double holdRpm) {
    model_ = model;
    engine_ = engine;
    hold_ = hold;
    holdRpm_ = holdRpm;
    setAccessibleDescription(QStringLiteral("%1 об/мин. %2.").arg(qRound(engine.rpm)).arg(engineModeName(engine.mode)));
    update();
}

void Tachometer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double radius = std::min(height() * 0.405, width() * 0.245);
    const QPointF hub(width() / 2.0, height() * 0.47);
    const auto bounds = [&](double d) { return QRectF(hub.x() - d, hub.y() - d, 2 * d, 2 * d); };
    QRadialGradient lighting(hub, radius * 1.18);
    lighting.setColorAt(0.0, QColor("#1c343b"));
    lighting.setColorAt(0.73, QColor("#15222d"));
    lighting.setColorAt(1.0, QColor(15, 23, 32, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(lighting);
    p.drawEllipse(bounds(radius * 1.15));
    const double limit = std::max(1.0, model_.maxRpm);
    const double value = std::clamp(engine_.rpm / limit, 0.0, 1.0);
    const auto bezel = bounds(radius);
    arc(p, bezel, 0.0, 1.0, QPen(QColor("#2b4051"), 6.0, Qt::SolidLine, Qt::RoundCap));
    arc(p, bezel, 0.90, 1.0, QPen(QColor("#6a4d43"), 6.0, Qt::SolidLine, Qt::RoundCap));
    if (value > 0.0) arc(p, bezel, 0.0, value, QPen(value > 0.90 ? amber : teal, 6.0, Qt::SolidLine, Qt::RoundCap));
    arc(p, bounds(radius - 18), 0.0, 1.0, QPen(QColor("#283b48"), 1));
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0;
        const bool major = i % 8 == 0;
        p.setPen(QPen(t > 0.90 ? QColor("#ad755d") : (major ? muted : dim), major ? 1.6 : 1.0));
        p.drawLine(radial(hub, radius - 8, t), radial(hub, radius - (major ? 17 : 12), t));
        if (major) {
            const QPointF at = radial(hub, radius - 28, t);
            label(p, QRectF(at.x() - 25, at.y() - 9, 50, 18), QString::number(qRound(t * limit)), face(10, false, true), muted);
        }
    }
    if (hold_) {
        const double target = std::clamp(holdRpm_ / limit, 0.0, 1.0);
        p.setPen(QPen(cyan, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(radial(hub, radius + 6, target), radial(hub, radius + 13, target));
    }
    const auto alive = engine_.mode != EngineMode::Off;
    const auto liveColor = alive ? QColor("#edfff9") : muted;
    label(p, QRectF(hub.x() - 82, hub.y() - 41, 164, 18), QStringLiteral("ОБ / МИН"), face(11, true), muted);
    label(p, QRectF(hub.x() - 89, hub.y() - 26, 178, 57), QString::number(qRound(engine_.rpm)),
          face(std::clamp(qRound(radius * 0.53), 32, 56), true, true), liveColor);

    const QString status = engineModeName(engine_.mode);
    const double chipWidth = QFontMetricsF(face(11, true)).horizontalAdvance(status) + 29;
    const QRectF chip(hub.x() - chipWidth / 2, hub.y() + 34, chipWidth, 22);
    p.setPen(Qt::NoPen);
    p.setBrush(alive ? QColor("#193e3e") : QColor("#243142"));
    p.drawRoundedRect(chip, 11, 11);
    p.setBrush(alive ? teal : muted);
    p.drawEllipse(QPointF(chip.left() + 10, chip.center().y()), 2.5, 2.5);
    label(p, chip.adjusted(13, 0, -4, 0), status, face(11, true), alive ? teal : muted);

    const double flank = std::max(45.0, std::min(99.0, (width() / 2.0 - radius - 25.0) * 0.77));
    const double flankY = hub.y() + radius * 0.27;
    meter(p, QRectF(16, flankY, flank, 48), QStringLiteral("ГАЗ"), engine_.throttle, teal);
    meter(p, QRectF(width() - flank - 16, flankY, flank, 48), QStringLiteral("НАГРУЗКА"), engine_.load, amber);

    const bool turbine = model_.kind == EngineKind::Turbine;
    const auto frequency = engine_.rpm / 60.0 * model_.cylinders / (model_.strokes / 2.0);
    const QString detail = turbine ? QStringLiteral("Газогенератор   %1 об/мин").arg(qRound(engine_.coreRpm)) :
        QStringLiteral("Частота вспышек   %1 Гц").arg(QString::number(frequency, 'f', 1));
    label(p, QRectF(42, height() - 26, width() - 84, 20), detail, face(11, false, true), muted);
}

SignalView::SignalView(bool spectrum, QWidget* parent) : QWidget(parent), spectrum_(spectrum) {
    setMinimumHeight(106);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(spectrum ? QStringLiteral("Спектр сигнала") : QStringLiteral("Осциллограмма"));
}

void SignalView::setSignal(const AudioSnapshot& snapshot) {
    signal_ = snapshot.mono;
    if (spectrum_) {
        if (snapshot.peak < 1e-6) {
            bands_.fill(0);
        } else {
            std::array<std::complex<float>, scopeFrames> points;
            for (std::size_t i = 0; i < points.size(); ++i) points[i] = signal_[i] * fftWindow[i];
            fft(points);
            for (std::size_t i = 0; i < bands_.size(); ++i) {
                const auto bin = [](double ratio) { return 23.44 * std::pow(20000.0 / 23.44, ratio) * fftSize / liveSampleRate; };
                const auto first = static_cast<std::size_t>(bin(i / static_cast<double>(bands_.size())));
                const auto end = static_cast<std::size_t>(std::ceil(bin((i + 1) / static_cast<double>(bands_.size()))));
                float magnitude = 0.0f;
                for (auto j = first; j < end; ++j) magnitude = std::max(magnitude, std::abs(points[j]));
                const float db = 20.0f * std::log10(std::max(1e-6f, magnitude / (fftSize * 0.25f)));
                const auto target = std::clamp((db + 72.0f) / 72.0f, 0.0f, 1.0f);
                bands_[i] = std::max(bands_[i] * 0.74f, target);
            }
        }
    }
    update();
}

void SignalView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setBrush(QColor("#111b27"));
    p.setPen(QColor("#263649"));
    p.drawRoundedRect(panel, 8, 8);
    label(p, QRectF(13, 5, width() - 26, 22), spectrum_ ? QStringLiteral("Спектр") : QStringLiteral("Осциллограмма"),
          face(12, true), text, Qt::AlignLeft | Qt::AlignVCenter);
    label(p, QRectF(13, 6, width() - 26, 20), spectrum_ ? QStringLiteral("дБFS") : QStringLiteral("85 мс"),
          face(10, false, true), muted, Qt::AlignRight | Qt::AlignVCenter);

    const QRectF graph(39, 29, width() - 55, height() - 50);
    const auto guide = QColor("#253345");
    const int divisions = spectrum_ ? 3 : 2;
    for (int i = 0; i <= divisions; ++i) {
        const double y = graph.top() + graph.height() * i / divisions;
        p.setPen(QPen(guide, 1));
        p.drawLine(QPointF(graph.left(), y), QPointF(graph.right(), y));
        const auto tick = spectrum_ ? QString::number(-24 * i) : QString::number(1 - i);
        label(p, QRectF(5, y - 7, 28, 14), tick, face(9, false, true), muted, Qt::AlignRight | Qt::AlignVCenter);
    }

    if (spectrum_) {
        const std::array frequencies{30, 100, 300, 1000, 3000, 10000};
        for (const int hz : frequencies) {
            const double x = graph.left() + graph.width() * std::log(hz / 23.44) / std::log(20000.0 / 23.44);
            const auto tick = hz < 1000 ? QString::number(hz) : QStringLiteral("%1k").arg(hz / 1000);
            p.setPen(QPen(guide, 1));
            p.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
            label(p, QRectF(x - 14, graph.bottom() + 3, 28, 14), tick, face(9, false, true), muted);
        }
        QLinearGradient brush(graph.topLeft(), graph.bottomLeft());
        brush.setColorAt(0.0, teal);
        brush.setColorAt(1.0, QColor("#277565"));
        p.setPen(Qt::NoPen);
        p.setBrush(brush);
        const double widthPerBand = graph.width() / bands_.size();
        for (std::size_t i = 0; i < bands_.size(); ++i) {
            const double bar = graph.height() * bands_[i];
            if (bar > 0.0) p.drawRect(QRectF(graph.left() + i * widthPerBand, graph.bottom() - bar, widthPerBand * 0.78, bar));
        }
    } else {
        for (int tick : {0, 20, 40, 60, 80}) {
            const double x = graph.left() + graph.width() * tick / (1000.0 * scopeFrames / liveSampleRate);
            p.setPen(QPen(guide, 1));
            p.drawLine(QPointF(x, graph.top()), QPointF(x, graph.bottom()));
            label(p, QRectF(x - 12, graph.bottom() + 3, 24, 14), QString::number(tick), face(9, false, true), muted);
        }
        const int samples = std::max(2, static_cast<int>(graph.width()));
        QPainterPath trace;
        for (int i = 0; i < samples; ++i) {
            const auto position = i * (signal_.size() - 1) / (samples - 1);
            const QPointF point(graph.left() + i, graph.center().y() - std::clamp(signal_[position], -1.0f, 1.0f) * graph.height() * 0.5);
            if (!i) trace.moveTo(point);
            else trace.lineTo(point);
        }
        p.setClipRect(graph.adjusted(-1, -1, 1, 1));
        p.setPen(QPen(cyan, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(trace);
    }
}

QString dashboardStyle() {
    return QString::fromLatin1(R"qt(
QWidget { color: #e5ecf3; font-family: 'Segoe UI'; font-size: 13px; }
QMainWindow, QDialog, QWidget#workspace { background: #0d141e; }
QFrame#panel, QFrame#transport { background-color: #131f2b; border: 1px solid #27374a; border-radius: 9px; }
QFrame#transport { background-color: #142332; }
QLabel { background: transparent; }
QLabel#brand { color: #f0f7ff; font-size: 24px; font-weight: 650; }
QLabel#section { color: #f0f7ff; font-size: 17px; font-weight: 650; }
QLabel#hint { color: #9bafc2; font-size: 12px; }
QLabel#caption { color: #a5b9ca; font-size: 11px; font-weight: 600; }
QLabel#numeric, QLabel#readout { color: #7ceaca; font-family: 'Cascadia Mono'; font-size: 12px; font-weight: 600; }
QLabel#readout { font-size: 21px; }
QLabel#status { color: #9bafc2; font-size: 11px; }
QLabel#status[alert="true"], QLabel#validation { color: #f6af71; }
QLabel#validation { font-size: 12px; }
QLabel#separator { background: #2e4054; }
QPushButton, QToolButton, QComboBox, QAbstractSpinBox, QLineEdit {
    background: #192839; color: #e5ecf3; border: 1px solid #33475c;
    border-radius: 5px; padding: 4px 9px; min-height: 21px; selection-background-color: #275a59;
}
QPushButton:hover, QToolButton:hover, QComboBox:hover, QAbstractSpinBox:hover, QLineEdit:hover {
    background: #233548; border-color: #50708b;
}
QPushButton:focus, QToolButton:focus, QComboBox:focus, QAbstractSpinBox:focus, QLineEdit:focus {
    border-color: #68c9c0;
}
QPushButton:disabled, QToolButton:disabled, QComboBox:disabled, QAbstractSpinBox:disabled, QLineEdit:disabled {
    color: #728597; border-color: #293747; background: #17222d;
}
QPushButton#primary { background: #59e0c3; color: #0b2728; border-color: #59e0c3; font-weight: 700; padding-left: 15px; padding-right: 15px; }
QPushButton#primary:hover { background: #83f1dc; border-color: #83f1dc; }
QPushButton#primary:disabled { background: #2b4648; color: #88a5a6; border-color: #344f4f; }
QPushButton#run { color: #72e7cc; background: #1b343b; border-color: #376b68; font-weight: 700; }
QPushButton#run:hover { background: #264851; }
QPushButton#run:checked { color: #ffd3ae; background: #3a2b2c; border-color: #715047; }
QComboBox, QAbstractSpinBox { padding-right: 23px; }
QComboBox::drop-down, QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border; width: 20px; border: none;
}
QComboBox::down-arrow { image: url(:/icons/chevron-down.png); width: 10px; height: 6px; }
QAbstractSpinBox::up-arrow { image: url(:/icons/chevron-up.png); width: 9px; height: 5px; }
QAbstractSpinBox::down-arrow { image: url(:/icons/chevron-down.png); width: 9px; height: 5px; }
QComboBox QAbstractItemView { background: #182635; border: 1px solid #36546a; selection-background-color: #2b5558; outline: 0; padding: 3px; }
QComboBox QAbstractItemView::item { padding: 5px 7px; min-height: 20px; }
QSlider::groove:horizontal { height: 4px; background: #2a3c4e; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #54bdae; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; margin: -5px 0; border-radius: 5px; background: #77e8ce; }
QSlider::handle:horizontal:hover, QSlider::handle:horizontal:focus { background: #d2fff2; }
QCheckBox { spacing: 6px; }
QCheckBox::indicator { width: 14px; height: 14px; border-radius: 4px; background: #1a2a3c; border: 1px solid #486178; }
QCheckBox::indicator:checked { background: #59e0c3; border: 3px solid #214a50; }
QCheckBox::indicator:hover, QCheckBox::indicator:focus { border-color: #80dfcc; }
QFrame#mixRow { background: #162433; border: 1px solid #27394c; border-radius: 6px; }
QToolButton#layerName { border: none; background: transparent; padding: 2px 5px; text-align: left; font-weight: 600; }
QToolButton#layerName:hover { background: #213747; }
QToolButton#layerName:checked { color: #77e8ce; background: #21404a; }
QToolButton#mute, QToolButton#solo { min-height: 19px; padding: 1px 3px; font-family: 'Cascadia Mono'; font-weight: 600; }
QToolButton#mute:checked { color: #ffd0af; background: #473432; border-color: #805846; }
QToolButton#solo:checked { color: #83f1dc; background: #245553; border-color: #3e8e82; }
QTabWidget::pane { border: 0; border-top: 1px solid #2c3b4e; }
QTabBar::tab { color: #9db0c2; padding: 7px 11px; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #75e6cd; border-bottom-color: #59e0c3; }
QTabBar::tab:hover { color: #d1e1eb; }
QScrollArea { border: 0; background: transparent; }
QScrollBar:vertical { background: #14212d; width: 7px; border-radius: 3px; }
QScrollBar::handle:vertical { background: #435970; min-height: 23px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QProgressBar { background: #213345; border: 0; border-radius: 2px; text-align: center; }
QProgressBar::chunk { background: #59e0c3; border-radius: 2px; }
QToolTip { color: #eef5f8; background: #24384a; border: 1px solid #577286; padding: 5px; }
)qt");
}

} // namespace esl
