#include "core/listener.hpp"

#include <algorithm>
#include <cmath>

namespace esl {
namespace {
double pole(double hz, int rate) { return -std::expm1(-2.0 * pi * hz / rate); }

float guardPeak(double x) {
    const double excess = std::abs(x) - 0.93;
    return static_cast<float>(excess <= 0.0 ? x : std::copysign(0.93 + 0.068 * excess / (0.068 + excess), x));
}
}

ListenerFilter::ListenerFilter(int sampleRate)
    : rate_(sampleRate), leftWall_(std::lround(rate_ * 0.0037)), rightWall_(std::lround(rate_ * 0.0053)),
      leftGround_(std::lround(rate_ * 0.012)), rightGround_(std::lround(rate_ * 0.0165)),
      hullLow_(pole(56, rate_)), hullHigh_(pole(185, rate_)), history_(rightGround_ + 1) {}

ListenerFilter::Response ListenerFilter::response(const Listener& s) const {
    using P = ListeningPosition;
    if (s.position == P::Direct) return {1, 1, 1, 1, 0, 0, 0};
    if (s.position == P::Interior) {
        const double wall = std::clamp(s.insulation, 0.0, 1.0);
        return {0.84 - 0.19 * wall, 0.46 - 0.25 * wall, pole(1850 * std::exp(-1.9 * wall), rate_),
                0.17 - 0.15 * wall, 0.30 + 0.94 * wall, 0.11 + 0.09 * wall, 0.0};
    }
    const double d = std::clamp(s.distance, 1.0, 120.0);
    const bool near = s.position == P::Near;
    const bool far = s.position == P::Distant;
    const double cutoff = (near ? 18500 : far ? 8500 : 15500) / (1.0 + 0.063 * (d - 1.0));
    const double width = (near ? 1.0 : far ? 0.34 : 0.76) / (1.0 + 0.009 * d);
    return {1.0 / (1.0 + 0.070 * (d - 1.0)), width, pole(cutoff, rate_),
            0.065 / (1.0 + 0.12 * d), 0, 0, near ? 0.06 : far ? 0.14 : 0.11};
}

void ListenerFilter::reset(const Listener& listener) {
    listener_ = listener;
    current_ = target_ = response(listener);
    air_ = {};
    lowState_ = highState_ = 0;
    std::fill(history_.begin(), history_.end(), 0.0f);
    remaining_ = head_ = 0;
}

void ListenerFilter::configure(const Listener& listener) {
    if (listener_ == listener) return;
    listener_ = listener;
    target_ = response(listener);
    remaining_ = std::max(1, rate_ / 25);
    for (int i = 0; i < ParameterCount; ++i) step_[i] = (target_[i] - current_[i]) / remaining_;
}

double ListenerFilter::tap(int delay) const {
    const int index = head_ - delay;
    return history_[index < 0 ? index + static_cast<int>(history_.size()) : index];
}

StereoFrame ListenerFilter::process(StereoFrame input) {
    if (remaining_ > 0) {
        for (int i = 0; i < ParameterCount; ++i) current_[i] += step_[i];
        if (--remaining_ == 0) current_ = target_;
    } else if (listener_.position == ListeningPosition::Direct) {
        // Prime the short reflection line even while bypassed, for a live move into the cabin.
        const double mid = 0.5 * (input.left + input.right);
        air_ = {input.left, input.right};
        lowState_ += hullLow_ * (mid - lowState_);
        highState_ += hullHigh_ * (mid - highState_);
        history_[head_] = static_cast<float>(mid);
        if (++head_ == static_cast<int>(history_.size())) head_ = 0;
        return input;
    }
    const auto& c = current_;
    air_[0] += c[Lowpass] * (input.left - air_[0]);
    air_[1] += c[Lowpass] * (input.right - air_[1]);
    const double left = std::lerp(air_[0], double(input.left), c[Treble]);
    const double right = std::lerp(air_[1], double(input.right), c[Treble]);
    const double mid = 0.5 * (left + right), side = 0.5 * (left - right) * c[Width];
    lowState_ += hullLow_ * (mid - lowState_);
    highState_ += hullHigh_ * (mid - highState_);
    const double body = mid + c[Hull] * (highState_ - lowState_);
    const double echoL = c[Wall] * tap(leftWall_) + c[Ground] * tap(leftGround_);
    const double echoR = c[Wall] * tap(rightWall_) + c[Ground] * tap(rightGround_);
    history_[head_] = static_cast<float>(mid);
    if (++head_ == static_cast<int>(history_.size())) head_ = 0;
    return {guardPeak(c[Gain] * (body + side + echoL)), guardPeak(c[Gain] * (body - side + echoR))};
}

} // namespace esl
