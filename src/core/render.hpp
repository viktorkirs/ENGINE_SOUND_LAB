#pragma once

#include "core/dsp.hpp"

#include <atomic>
#include <vector>

namespace esl {

enum class Motion { Steady, Start, Acceleration, Deceleration, Shutdown, Drive };

struct ClipRequest {
    Motion motion = Motion::Steady;
    double seconds = 4.0;
    double rpm = 1000.0;
    double load = 0.2;
    double throttle = 0.24;
    bool loop = false;
};

struct RenderedClip {
    std::vector<StereoFrame> samples;
    double startRpm = 0.0;
    double endRpm = 0.0;
    double startLoad = 0.0;
    double endLoad = 0.0;
    double peakRpm = 0.0;
};

RenderedClip renderClip(Settings settings, const ClipRequest& request, int rate,
                        const std::atomic_bool* cancel = nullptr);

} // namespace esl
