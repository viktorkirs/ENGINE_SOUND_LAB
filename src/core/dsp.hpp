#pragma once

#include "core/mixer.hpp"
#include "core/simulation.hpp"

#include <memory>
#include <span>

namespace esl {

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esl
