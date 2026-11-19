#include "app/engine_diagram.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace esl {
namespace {
const QColor outline("#527184"), metal("#9bb2bf"), shaft("#57867f"), glow("#f9ad67");
const QColor missed("#d3a4c8");

double sinceFiring(double turn, double at, double cycle) { return std::fmod(turn - at + 2 * cycle, cycle); }

void bearing(QPainter& p, QPointF at, double radius) {
    p.setPen(QPen(shaft, 1.1));
    p.setBrush(QColor("#1b3039"));
    p.drawEllipse(at, radius, radius);
}

// Local y points down towards the crank. The pin traces a circle and the rod keeps its length.
void cylinder(QPainter& p, QPointF crank, double degrees, double width, const CylinderPhase& cylinder,
              double turn, double cycle, double burn, double throwRadius, double compression, bool firing) {
    p.save();
    p.translate(crank);
    p.rotate(degrees);
    const double t = sinceFiring(turn, cylinder.turn, cycle);
    const double angle = 2 * pi * t;
    const QPointF pin(throwRadius * std::sin(angle), -throwRadius * std::cos(angle));
    const double rod = 20.0;
    const double piston = pin.y() - std::sqrt(rod * rod - pin.x() * pin.x());
    const double heat = burn * std::exp(-10.0 * t);
    const double head = -rod - throwRadius - 3.0 - 2.0 * throwRadius / (compression - 1.0);
    const QRectF liner(-width / 2, head, width, -9.5 - head);
    p.setPen(QPen(firing ? glow : outline, firing ? 2.0 : 1.1));
    p.setBrush(QColor("#142530"));
    p.drawRoundedRect(liner, 2, 2);
    if (heat > 0.025) {
        QColor fire = glow;
        fire.setAlphaF(std::min(1.0, 0.86 * heat));
        p.fillRect(QRectF(-width / 2 + 1, head + 1, width - 2, std::max(0.5, piston - 4.0 - head)), fire);
    }
    p.setPen(QPen(metal, 1.6, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(pin, QPointF(0, piston));
    p.setPen(Qt::NoPen);
    p.setBrush(heat > 0.35 ? glow : QColor("#77b8aa"));
    p.drawRoundedRect(QRectF(-width * 0.40, piston - 3, width * 0.8, 5.5), 1, 1);
    p.setPen(QPen(outline, 1));
    p.drawLine(QPointF(-width / 2 - 1, head - 2.0), QPointF(width / 2 + 1, head - 2.0));
    p.setPen(QPen(shaft, 2.2));
    p.drawLine(QPointF(0, 0), pin);
    p.setBrush(metal);
    p.setPen(Qt::NoPen);
    p.drawEllipse(pin, 1.5, 1.5);
    p.translate(0, head - 8.0);
    p.rotate(-degrees);
    QFont font = p.font();
    font.setPixelSize(8);
    p.setFont(font);
    if (firing) {
        p.setBrush(glow);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(-7, -5, 14, 10), 3, 3);
    }
    p.setPen(firing ? QColor("#19232a") : metal);
    p.drawText(QRectF(-8,-5,16,10), Qt::AlignCenter, QString::number(cylinder.cylinder));
    p.restore();
}

void rotor(QPainter& p, QPointF center, double radius, int blades, double phase) {
    p.save();
    p.translate(center);
    bearing(p, {}, radius + 3);
    p.rotate(phase * 360.0);
    const QPolygonF blade{{radius * 0.17, 0}, {radius * 0.63, -radius * 0.22},
                          {radius, -radius * 0.12}, {radius * 0.84, radius * 0.06}};
    for (int i = 0; i < blades; ++i) {
        p.setPen(Qt::NoPen);
        p.setBrush(i == 0 ? glow : QColor("#77b8aa"));
        p.drawPolygon(blade);
        p.rotate(360.0 / blades);
    }
    bearing(p, {}, radius * 0.17);
    p.restore();
}
}

EngineDiagram::EngineDiagram() {
    setFixedHeight(144);
    setAccessibleName(QStringLiteral("Схема двигателя"));
    setToolTip(QStringLiteral("Янтарным отмечен рабочий такт. Числа обозначают цилиндры."));
    clock_.start();
}

void EngineDiagram::setSlow(bool enabled) {
    slow_ = enabled;
    captured_ = false;
    shown_ = live_;
    clock_.restart();
    update();
}

void EngineDiagram::setData(const Model& model, const EngineState& engine, ShaftPhases phases, const IgnitionTrace& ignitions) {
    const double dt = std::min(0.12, clock_.nsecsElapsed() * 1e-9);
    clock_.restart();
    const bool changed = needsNewVoice(model_, model);
    model_ = model;
    engine_ = engine;
    live_ = phases;
    if (changed) {
        cylinders_ = cylinderPhases(model_);
        setToolTip(model.kind == EngineKind::Turbine ? QStringLiteral("Газогенератор слева, свободная турбина справа.") :
                   QStringLiteral("Янтарным отмечен рабочий такт. Числа обозначают цилиндры."));
    }
    const double cycle = model.strokes / 2.0;
    const double advance = engine.rpm * dt / (60.0 * 30.0);
    const bool nextCycle = sinceFiring(shown_.crankTurns, cylinders_[0].turn, cycle) + advance >= cycle;
    if (!slow_ || changed) shown_ = live_;
    else {
        const double turns = dt / (60.0 * 30.0);
        shown_.crankTurns = std::fmod(shown_.crankTurns + advance, cycle);
        shown_.gasGenerator = std::fmod(shown_.gasGenerator + engine.coreRpm * turns, 1.0);
        shown_.freeTurbine = std::fmod(shown_.freeTurbine + engine.rpm * 4.3 * turns, 1.0);
    }
    // In slow motion replay one real cycle, including its misses, then pick up a fresh cycle.
    // In real time the label follows the most recent audio-thread event.
    if (!slow_ || !captured_ || changed || nextCycle) {
        captured_ = slow_ && ignitions.hasCompleted;
        displayed_ = captured_ ? ignitions.completed : ignitions.latest;
    }
    activeCylinder_ = 0;
    const bool rotating = engine.mode != EngineMode::Off && engine.rpm > 1;
    if (rotating && ignitions.lastCylinder > 0 && model.kind != EngineKind::Turbine) {
        const auto end = cylinders_.begin() + model.cylinders;
        const auto active = slow_ ? std::min_element(cylinders_.begin(), end, [&](const auto& a, const auto& b) {
            return sinceFiring(shown_.crankTurns, a.turn, cycle) < sinceFiring(shown_.crankTurns, b.turn, cycle);
        }) : std::find_if(cylinders_.begin(), end, [&](const auto& c) { return c.cylinder == ignitions.lastCylinder; });
        if (active != end) {
            activeCylinder_ = active->cylinder;
            const auto result = displayed_[activeCylinder_ - 1];
            QString event = QStringLiteral("Прокрутка");
            if (result == Ignition::Fired) event = QStringLiteral("Вспышка");
            if (result == Ignition::Missed) event = QStringLiteral("Пропуск");
            if (result == Ignition::Cut) event = QStringLiteral("Отсечка");
            caption_ = QStringLiteral("%1 · цилиндр %2").arg(event).arg(activeCylinder_);
            if (model.layout == Layout::V || model.layout == Layout::Boxer || (model.layout == Layout::Radial && model.cylinders % 2 == 0))
                caption_ += QStringLiteral(" · ряд %1").arg(active->bank + 1);
        }
    }
    if (!activeCylinder_) caption_ = QStringLiteral("Порядок вспышек");
    if (!rotating) captured_ = false;
    const bool turbine = model.kind == EngineKind::Turbine;
    if (turbine) caption_ = engine.limiterCut ? QStringLiteral("Отсечка") :
        engine.burn > 0.02 ? QStringLiteral("Горение в камере") : QStringLiteral("Камера сгорания");
    const int targetHeight = turbine ? 122 : model.cylinders > 12 ? 159 : 144;
    if (height() != targetHeight) setFixedHeight(targetHeight);
    if (accessibleDescription() != caption_) setAccessibleDescription(caption_);
    update();
}

void EngineDiagram::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#2d4352"), 1));
    p.setBrush(QColor("#101c27"));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    const bool turbine = model_.kind == EngineKind::Turbine;
    const int n = model_.cylinders;
    const int rows = (n + 11) / 12;
    const int plotHeight = height() - (turbine ? 21 : 22 + rows * 15);
    const bool fired = activeCylinder_ > 0 && displayed_[activeCylinder_ - 1] == Ignition::Fired;
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);
    p.setPen(activeCylinder_ ? (fired ? glow : missed) : metal);
    p.drawText(QRectF(3, plotHeight, width() - 6, 15), Qt::AlignCenter, caption_);
    if (!turbine) {
        font.setPixelSize(10);
        p.setFont(font);
        const double step = std::min(21.0, (width() - 12.0) / std::min(n, 12));
        for (int i = 0; i < n; ++i) {
            const int inRow = std::min(12, n - i / 12 * 12);
            const QRectF cell((width() - inRow * step) / 2 + (i % 12) * step,
                              plotHeight + 17 + (i / 12) * 15, step - 1, 13);
            const bool active = cylinders_[i].cylinder == activeCylinder_;
            p.setPen(Qt::NoPen);
            p.setBrush(active ? (fired ? glow : missed) : QColor("#1c313c"));
            p.drawRoundedRect(cell, 3, 3);
            p.setPen(active ? QColor("#18232b") : metal);
            p.drawText(cell, Qt::AlignCenter, QString::number(cylinders_[i].cylinder));
        }
    }
    p.translate(width() * 0.5, plotHeight * 0.5);
    const double scale = std::min((width() - 12) / 248.0, (plotHeight - 6) / 100.0);
    p.scale(scale, scale);
    if (model_.kind == EngineKind::Turbine) {
        p.setPen(QPen(outline, 3));
        p.drawLine(QPointF(-86, 0), QPointF(91, 0));
        p.setBrush(engine_.burn > 0.3 && !engine_.limiterCut ? glow : outline);
        p.drawRoundedRect(QRectF(-7, -8, 16, 16), 4, 4);
        rotor(p, {-49,0}, 28, model_.compressorBlades, shown_.gasGenerator);
        rotor(p, {53,0}, 23, 11, shown_.freeTurbine);
        return;
    }
    const double radius = std::clamp(5.6 * std::sqrt(pistonStrokeMm(model_) / cylinderBoreMm(model_)), 4.0, 8.2);
    const double cycle = model_.strokes / 2.0;
    const auto draw = [&](QPointF crank, double angle, double width, const CylinderPhase& c) {
        const bool lit = activeCylinder_ && displayed_[c.cylinder - 1] == Ignition::Fired;
        cylinder(p, crank, angle, width, c, shown_.crankTurns, cycle, lit ? engine_.burn : 0.0,
                 radius, compressionRatio(model_), lit && c.cylinder == activeCylinder_);
    };
    if (model_.layout == Layout::Radial) {
        const bool twin = n % 2 == 0;
        for (int i = 0; i < n; ++i) {
            const auto& c = cylinders_[i];
            const double x = twin ? (c.bank ? 57 : -57) : 0;
            const double angle = 360.0 * (c.cylinder - 1) / n;
            draw({x,0}, angle, std::min(12.0, 95.0 / (twin ? n / 2 : n)), c);
        }
        bearing(p, {twin ? -57.0 : 0.0, 0}, 5);
        if (twin) bearing(p, {57, 0}, 5);
        return;
    }
    const bool banks = model_.layout != Layout::Inline;
    const bool splitInline = !banks && n > 12;
    const int stations = banks || splitInline ? (n + 1) / 2 : n;
    const double pitch = std::min(40.0, 212.0 / stations);
    const double extent = (stations - 1) * pitch / 2.0;
    const double y = banks ? 0.0 : splitInline ? -5.0 : 16.0;
    p.setPen(QPen(shaft, 3.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-extent - 8, y), QPointF(extent + 8, y));
    if (splitInline) p.drawLine(QPointF(-extent - 8, 44), QPointF(extent + 8, 44));
    for (int i = 0; i < n; ++i) {
        const auto& c = cylinders_[i];
        const int id = c.cylinder - 1;
        const int station = banks ? id / 2 : id % stations;
        const QPointF crank(station * pitch - extent, splitInline && id >= stations ? 44.0 : y);
        const double tilt = model_.layout == Layout::V ? (180.0 - model_.bankAngle) * 0.19 : 0.0;
        const double direction = banks ? (c.bank ? 180.0 - tilt : tilt) : 0.0;
        draw(crank, direction, std::min(18.0, pitch * 0.58), c);
        bearing(p, crank, 2.8);
    }
}

} // namespace esl
