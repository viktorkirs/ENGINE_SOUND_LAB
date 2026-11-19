#pragma once

#include "core/mixer.hpp"
#include "core/simulation.hpp"

#include <memory>
#include <span>

namespace esl {

struct ShaftPhases {
    double crankTurns = 0.0;
    double gasGenerator = 0.0;
    double freeTurbine = 0.0;
};

enum class Ignition : std::uint8_t { Inactive, Fired, Missed, Cut };

struct IgnitionTrace {
    using Cycle = std::array<Ignition, maxCylinders>;
    Cycle latest{};
    Cycle completed{};
    int lastCylinder = 0; // One-based, zero before the first event.
    bool hasCompleted = false;
};

// Each synthesizer is owned by one render thread. Settings are copied at block boundaries.
class Synthesizer {
public:
    explicit Synthesizer(int sampleRate);
    ~Synthesizer();
    Synthesizer(const Synthesizer&) = delete;
    Synthesizer& operator=(const Synthesizer&) = delete;

    void reset(const Settings& settings);
    void apply(const Settings& settings);
    void render(std::span<StereoFrame> frames);
    void setSteady(const Settings& settings, double rpm);
    const EngineState& state() const;
    ShaftPhases phases() const;
    const IgnitionTrace& ignitions() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esl
