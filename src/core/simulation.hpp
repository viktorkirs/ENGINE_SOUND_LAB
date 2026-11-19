#pragma once

#include "core/engine_model.hpp"

namespace esl {

enum class EngineMode { Off, Starting, Idle, Running, Braking, Stopping };
inline constexpr std::array gearRatios{0.0, 3.9, 2.5, 1.7, 1.25, 1.0, 0.83};

struct EngineState {
    double rpm = 0.0;
    double coreRpm = 0.0;
    double throttle = 0.0;
    double load = 0.0;
    double torqueNm = 0.0;
    double fuel = 0.0;
    double overrun = 0.0;
    double burn = 0.0;
    double boost = 0.0;
    double starter = 0.0;
    bool limiterCut = false;
    double firingGain = 1.0;
    double progress = 0.0;
    Program program = Program::Manual;
    std::uint32_t programToken = 0;
    int gear = 0;
    double clutch = 1.0;
    EngineMode mode = EngineMode::Off;
};

double programDuration(Program program);
Controls programControls(Program program, double seconds, const Controls& base);

class SimulationController {
public:
    const EngineState& state() const { return state_; }
    void setSteady(const Settings& settings, double rpm);
    void update(const Settings& settings, double dt);

private:
    EngineState state_;
    Faults fault_;
    bool running_ = false;
    double runTime_ = 0.0;
    double shiftTime_ = 0.0;
    double programTime_ = 0.0;
    Program program_ = Program::Manual;
    std::uint32_t programToken_ = 0;
};

} // namespace esl
