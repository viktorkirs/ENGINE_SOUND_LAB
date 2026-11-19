#pragma once

#include "core/dsp.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace esl {

inline constexpr int liveSampleRate = 48000;
inline constexpr std::size_t scopeFrames = 4096;

enum class StreamStatus { Standby, Streaming, NoDevice };

struct AudioSnapshot {
    EngineState engine;
    ShaftPhases phases;
    IgnitionTrace ignitions;
    std::array<float, scopeFrames> mono{};
    double peak = 0.0;
    StreamStatus stream = StreamStatus::Standby;
    unsigned deviceError = 0;
    unsigned underruns = 0;
    std::uint64_t serial = 0;
};

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    void submit(const Settings& settings);
    AudioSnapshot snapshot() const;

private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
};

} // namespace esl
