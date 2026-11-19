#include "core/simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace esl {
namespace {

double unit(double x) { return std::clamp(x, 0.0, 1.0); }

double smooth(double x) {
    x = unit(x);
    return x * x * (3.0 - 2.0 * x);
}

double approach(double value, double target, double dt, double timeConstant) {
    return value + (target - value) * -std::expm1(-dt / timeConstant);
}

constexpr double rpmPerRadian = 60.0 / (2.0 * pi);

struct Cue {
    double time;
    double throttle;
    double load;
    int gear;
    bool running = true;
    bool brake = false;
};

std::span<const Cue> cues(Program program) {
    static constexpr Cue idle[]{{0, 0, 0.12, 0}, {4, 0, 0.12, 0}};
    static constexpr Cue slow[]{{0, 0, 0.2, 1}, {2, 0, 0.2, 1}, {8, 0.95, 0.65, 2}};
    static constexpr Cue fast[]{{0, 0, 0.16, 1}, {2, 0, 0.16, 1}, {2.35, 1, 0.55, 1}, {6, 1, 0.55, 2}};
    static constexpr Cue full[]{{0, 0, 0.2, 0}, {2, 0.65, 0.45, 2}, {4, 1, 1, 3}, {8, 1, 1, 3}};
    static constexpr Cue lift[]{{0, 0.85, 0.50, 2}, {3.5, 0.85, 0.50, 2}, {3.75, 0, 0.25, 2}, {8, 0, 0.15, 2}};
    static constexpr Cue brake[]{{0, 0.9, 0.55, 3}, {3.75, 0.9, 0.55, 3}, {4, 0, 0.78, 2, true, true},
                                {8, 0, 0.78, 2, true, true}};
    static constexpr Cue startStop[]{{0, 0, 0.12, 0, false}, {0.30, 0, 0.12, 0},
                                    {4, 0, 0.12, 0}, {5.5, 0.38, 0.34, 0},
                                    {7, 0, 0.12, 0}, {8.5, 0, 0, 0, false}, {15, 0, 0, 0, false}};
    static constexpr Cue automatic[]{{0, 0, 0.10, 0, false}, {0.35, 0, 0.10, 0},
                                    {3.8, 0, 0.10, 0}, {7.2, 0.58, 0.55, 1},
                                    {8.0, 0.44, 0.44, 2}, {8.4, 1, 0.85, 2},
                                    {12.3, 1, 1, 3}, {12.5, 0, 0.62, 3},
                                    {13.35, 0, 0.9, 2, true, true}, {16.2, 0, 0.15, 0},
                                    {18, 0, 0, 0, false}, {25, 0, 0, 0, false}};
    static constexpr Cue drive[]{{0, 0.04, 0.12, 0}, {1.1, 0.18, 0.32, 1},
                                {3.5, 0.84, 0.68, 1}, {4.2, 0.60, 0.50, 2},
                                {6.5, 0.91, 0.77, 2}, {7.2, 0.57, 0.53, 3},
                                {9.2, 0.74, 0.67, 3}, {9.7, 0, 0.26, 3},
                                {12.1, 0, 0.32, 2, true, true}, {14.2, 0, 0.12, 0},
                                {15, 0, 0.12, 0}};
    switch (program) {
    case Program::Idle: return idle;
    case Program::SlowRamp: return slow;
    case Program::FastRamp: return fast;
    case Program::FullLoad: return full;
    case Program::Lift: return lift;
    case Program::EngineBrake: return brake;
    case Program::StartStop: return startStop;
    case Program::AutoTest: return automatic;
    case Program::Drive: return drive;
    case Program::Manual: return {};
    }
    return {};
}

double gasCoreTarget(const Model& model, const Controls& controls, double speed) {
    return model.coreMaxRpm * (model.coreIdle + (1.0 - model.coreIdle) *
        unit(0.64 * controls.throttle + 0.36 * controls.load + 0.10 * speed));
}

double boostTarget(const Model& model, double throttle, double load, double speed) {
    if (model.boost == Boost::Supercharger) return unit(0.12 + 0.73 * speed + 0.12 * throttle);
    if (model.boost == Boost::Turbocharger) return unit(0.06 + 0.21 * speed + 0.43 * throttle + 0.31 * load);
    return 0.0;
}

double fuelDemand(double throttle, double load, double speed) {
    return unit(0.14 + throttle * 0.30 + load * 0.52 + speed * 0.10);
}

double availableCharge(const Model& model, const Faults& fault) {
    const double lean = std::max(0.0, -fault.fuelBias);
    const double rich = std::max(0.0, fault.fuelBias);
    const double failed = model.kind == EngineKind::Gasoline ? fault.sparkMiss :
        (model.kind == EngineKind::Diesel ? 0.49 * fault.injectorWear : 0.0);
    return (1.0 - 0.70 * lean - 0.43 * rich * rich) * (1.0 - 0.97 * failed / model.cylinders);
}

} // namespace

double programDuration(Program program) {
    const auto timeline = cues(program);
    return timeline.empty() ? 0.0 : timeline.back().time;
}

Controls programControls(Program program, double seconds, const Controls& base) {
    auto result = base;
    const auto timeline = cues(program);
    if (timeline.empty()) return result;
    const auto next = std::upper_bound(timeline.begin() + 1, timeline.end(), seconds,
        [](double time, const Cue& cue) { return time < cue.time; });
    const auto& a = *(next - 1);
    const auto& b = next == timeline.end() ? a : *next;
    const double mix = a.time == b.time ? 0.0 : smooth((seconds - a.time) / (b.time - a.time));
    result.running = a.running;
    result.throttle = std::lerp(a.throttle, b.throttle, mix);
    result.load = std::lerp(a.load, b.load, mix);
    result.gear = a.gear;
    result.brake = a.brake;
    result.hold = false;
    return result;
}

void SimulationController::setSteady(const Settings& settings, double rpm) {
    const auto& model = settings.model;
    const auto& controls = settings.controls;
    state_ = {};
    fault_ = settings.faults;
    running_ = true;
    runTime_ = 30.0;
    shiftTime_ = 0.0;
    program_ = Program::Manual;
    programTime_ = 0.0;
    state_.rpm = std::clamp(rpm, model.idleRpm, model.maxRpm);
    if (settings.limiter.enabled && state_.rpm >= settings.limiter.cutRpm) {
        state_.rpm = settings.limiter.cutRpm;
        state_.limiterCut = true;
    }
    state_.throttle = unit(controls.throttle);
    state_.load = unit(controls.load);
    state_.gear = std::clamp(controls.gear, 0, 6);
    state_.burn = 1.0;
    const double speed = unit((state_.rpm - model.idleRpm) / (model.maxRpm - model.idleRpm));
    state_.fuel = fuelDemand(state_.throttle, state_.load, speed);
    state_.mode = state_.rpm < model.idleRpm * 1.08 ? EngineMode::Idle : EngineMode::Running;
    if (model.kind == EngineKind::Turbine) {
        state_.coreRpm = gasCoreTarget(model, controls, speed) * (0.70 + 0.30 * availableCharge(model, fault_));
    } else {
        state_.boost = boostTarget(model, state_.throttle, state_.load, speed);
    }
}

void SimulationController::update(const Settings& settings, double dt) {
    const auto& model = settings.model;
    const auto& boostLayer = settings.layers[4];
    const auto& requested = settings.controls;
    fault_.approach(settings.faults, -std::expm1(-dt / 0.08));
    if (requested.program != program_ || requested.programToken != programToken_) {
        program_ = requested.program;
        programToken_ = requested.programToken;
        programTime_ = 0.0;
    }
    const auto controls = programControls(program_, programTime_, requested);
    state_.program = program_;
    state_.programToken = programToken_;
    const double duration = programDuration(program_);
    state_.progress = duration > 0.0 ? unit(programTime_ / duration) : 0.0;
    programTime_ = std::min(programTime_ + dt, duration);
    if (controls.running != running_) {
        runTime_ = controls.running && state_.burn > 0.68 ? 4.0 : 0.0;
        running_ = controls.running;
    }
    runTime_ += dt;
    state_.throttle = approach(state_.throttle, running_ ? unit(controls.throttle) : 0.0, dt, 0.065);
    state_.load = approach(state_.load, unit(controls.load), dt, 0.085);
    const int gear = std::clamp(controls.gear, 0, 6);
    if (gear != state_.gear) {
        if (running_ && state_.gear != 0 && gear != 0) shiftTime_ = 0.29;
        state_.gear = gear;
    }
    shiftTime_ = std::max(0.0, shiftTime_ - dt);
    const bool turbine = model.kind == EngineKind::Turbine;
    const double span = model.maxRpm - model.idleRpm;
    const double speed = unit((state_.rpm - model.idleRpm) / span);
    const double pedal = controls.brake ? 0.0 : state_.throttle;
    const auto& limiter = settings.limiter;
    if (!limiter.enabled || !running_ || state_.rpm <= limiter.resumeRpm) state_.limiterCut = false;
    else if (state_.rpm >= limiter.cutRpm - 0.05) state_.limiterCut = true;
    state_.firingGain = approach(state_.firingGain, state_.limiterCut ? 0.0 : 1.0, dt, 0.008);
    const double shift = 1.0 - 0.78 * smooth(shiftTime_ / 0.21);
    state_.clutch = shift;
    const double ratio = gearRatios[gear];
    const double inertia = model.inertia * (1.0 + 0.16 * ratio * state_.load);
    Controls metered = controls;
    metered.throttle = pedal * shift;
    metered.load = state_.load;
    const double charge = availableCharge(model, fault_);
    if (turbine) {
        const double core = state_.coreRpm / model.coreMaxRpm;
        const double idleFraction = core / model.coreIdle;
        state_.starter = running_ ? (1.0 - smooth((idleFraction - 0.23) / 0.58)) * smooth(runTime_ / 0.12) : 0.0;
        const double target = running_ ? gasCoreTarget(model, metered, speed) * (0.70 + 0.30 * charge) : 0.0;
        const double lag = model.coreLag * (running_ ? (idleFraction < 0.40 ? 1.42 : 1.0) : 1.18);
        state_.coreRpm = approach(state_.coreRpm, target, dt, lag);
        const double fire = running_ ? smooth((idleFraction - 0.19) / 0.55) : 0.0;
        state_.burn = approach(state_.burn, fire, dt, running_ ? 0.29 : 0.40);
    } else {
        state_.coreRpm = 0.0;
        const double fire = running_ ? smooth((runTime_ - 0.18) / 0.65) : 0.0;
        state_.burn = approach(state_.burn, fire, dt, running_ ? 0.075 : 0.082);
        state_.starter = running_ ? smooth(runTime_ / 0.085) * (1.0 - smooth((runTime_ - 0.35) / 0.62)) : 0.0;
        const double target = running_ ? boostTarget(model, metered.throttle, state_.load, speed) : 0.0;
        const double lag = model.boost == Boost::Supercharger ? 0.045 + 0.11 * boostLayer.shape :
            (target > state_.boost ? 0.19 + 1.55 * boostLayer.shape : 0.24 + 1.68 * boostLayer.damping);
        state_.boost = approach(state_.boost, target, dt, lag);
    }

    const double peak = model.torqueNm;
    const double friction = peak * (0.025 + model.loss * (0.042 + 0.148 * speed) + 0.027 * fault_.mechanicalWear);
    const double absorber = peak * state_.load * (0.145 + 0.35 * speed) * (1.0 + 0.052 * ratio);
    const double overload = peak * fault_.overload * (0.23 + 0.55 * speed) * (0.7 + 0.3 * state_.load);
    const double resistance = friction + absorber + overload + (controls.brake ? peak * speed * 0.42 : 0.0);
    const double headroom = limiter.enabled ? 0.10 * smooth((pedal - 0.80) / 0.20) : 0.0;
    const double target = model.idleRpm + span * (std::pow(pedal, turbine ? 1.24 : 0.92) + headroom);
    // Unloaded exhaust returns to its powered timbre as soon as the driver
    // matches the current crank speed again, including part-throttle reapplication.
    const double overrun = running_ && !controls.hold && !turbine ?
        smooth((state_.rpm - target) / (0.40 * span)) : 0.0;
    state_.overrun = approach(state_.overrun, overrun, dt, overrun > state_.overrun ? 0.085 : 0.055);
    const double torqueCurve = 0.66 + 0.34 * std::sin(pi * (0.22 + 0.70 * speed));
    const double powerLimit = model.powerKw * 9550.0 / std::max(1.0, state_.rpm);
    const double available = std::min(peak * torqueCurve, powerLimit) * charge;
    const double governor = (target - state_.rpm) / span * peak * (turbine ? 2.0 : 3.1);
    const double drive = running_ && !state_.limiterCut ?
        std::clamp(friction + absorber * 0.90 + governor, 0.0, available) * shift * state_.burn : 0.0;

    if (controls.hold && running_ && state_.burn > 0.55 && !controls.brake && !state_.limiterCut) {
        const double heldRpm = std::clamp(controls.holdRpm, model.idleRpm, model.maxRpm);
        // The stand can demand the threshold. Release the drive during each cut.
        const double goal = limiter.enabled && heldRpm >= limiter.cutRpm ?
            limiter.cutRpm + (limiter.cutRpm - limiter.resumeRpm) : heldRpm;
        state_.rpm = std::min(model.maxRpm, approach(state_.rpm, goal, dt, 0.10 + 0.006 * inertia));
        state_.torqueNm = std::min(available, resistance);
    } else {
        const double starterRpm = std::min(model.idleRpm * 0.66, 430.0);
        const double starterTorque = turbine ? 0.0 : state_.starter * peak *
            unit((starterRpm - state_.rpm) / (starterRpm * 0.55));
        const double windage = running_ ? 0.0 : peak * (0.035 + (turbine ? 0.19 : 0.12) * state_.rpm / model.maxRpm);
        const double engineDrag = model.overrunDrag * peak * std::sqrt(speed) * state_.overrun;
        const double net = drive + starterTorque - (running_ || !turbine ? resistance : friction + 0.5 * absorber) - windage - engineDrag;
        state_.rpm = std::clamp(state_.rpm + (net / inertia) * rpmPerRadian * dt, 0.0, model.maxRpm);
        state_.torqueNm = drive;
    }

    const double mixture = running_ && !state_.limiterCut ? fuelDemand(metered.throttle, state_.load, speed) *
        (controls.brake ? 0.32 : 1.0) : 0.0;
    state_.fuel = approach(state_.fuel, mixture, dt, 0.092);
    if (!running_ && state_.coreRpm < model.coreMaxRpm * 0.014 && state_.rpm < 14.0) {
        state_.rpm = state_.coreRpm = state_.starter = state_.burn = state_.boost = state_.fuel = 0.0;
        state_.mode = EngineMode::Off;
    } else if (!running_) {
        state_.mode = EngineMode::Stopping;
    } else if (state_.burn < 0.72 || runTime_ < 0.90) {
        state_.mode = EngineMode::Starting;
    } else if (controls.brake) {
        state_.mode = EngineMode::Braking;
    } else if (state_.rpm <= model.idleRpm * 1.08 && pedal < 0.08) {
        state_.mode = EngineMode::Idle;
    } else {
        state_.mode = EngineMode::Running;
    }
}

} // namespace esl
