#pragma once

#include "core/mixer.hpp"

#include <array>
#include <vector>

namespace esl {

// Runs after the final mix, at the output rate, for both audition and file rendering.
class ListenerFilter {
public:
    explicit ListenerFilter(int sampleRate);
    void reset(const Listener& listener);
    void configure(const Listener& listener);
    StereoFrame process(StereoFrame input);

private:
    enum Parameter { Gain, Width, Lowpass, Treble, Hull, Wall, Ground, ParameterCount };
    using Response = std::array<double, ParameterCount>;
    Response response(const Listener& listener) const;
    double tap(int delay) const;

    int rate_, leftWall_, rightWall_, leftGround_, rightGround_;
    double hullLow_, hullHigh_, lowState_ = 0.0, highState_ = 0.0;
    std::array<double, 2> air_{};
    std::vector<float> history_;
    int head_ = 0;
    Listener listener_;
    Response current_{}, target_{}, step_{};
    int remaining_ = 0;
};

} // namespace esl
