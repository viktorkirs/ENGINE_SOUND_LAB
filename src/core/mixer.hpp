#pragma once

#include "core/engine_model.hpp"

#include <algorithm>
#include <cmath>

namespace esl {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

class AudioMixer {
public:
    void configure(const Layers& layers, double master, double rate, bool immediate,
                   bool recorded = false, ListeningPosition position = ListeningPosition::Direct) {
        constexpr auto mic = static_cast<std::size_t>(LayerId::Recording);
        bool solo = false;
        for (std::size_t i = 0; i < layers.size(); ++i)
            solo |= (recorded || i != mic) && layers[i].solo;
        // Structure-borne vibration and mechanical noise carry into the cabin.
        constexpr std::array cabin{0.68, 0.43, 0.34, 1.25, 0.38, 1.48, 0.86, 1.32};
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const double transfer = position == ListeningPosition::Interior ? cabin[i] : 1.0;
            target_[i] = (!recorded && i == mic) || layers[i].mute || (solo && !layers[i].solo) ? 0.0 : layers[i].gain * transfer;
        }
        masterTarget_ = master;
        if (rate != rate_) {
            rate_ = rate;
            slew_ = -std::expm1(-1.0 / (0.012 * rate));
        }
        if (immediate) {
            gain_ = target_;
            master_ = masterTarget_;
        }
    }

    bool audible(std::size_t layer) const { return target_[layer] > 0.0 || gain_[layer] > 1e-7; }

    StereoFrame mix(const std::array<StereoFrame, layerCount>& source) {
        double left = 0.0;
        double right = 0.0;
        for (std::size_t i = 0; i < source.size(); ++i) {
            gain_[i] += (target_[i] - gain_[i]) * slew_;
            left += gain_[i] * source[i].left;
            right += gain_[i] * source[i].right;
        }
        master_ += (masterTarget_ - master_) * slew_;
        return {output(left * master_), output(right * master_)};
    }

private:
    static float output(double x) {
        // Linear near zero, with continuous peak protection for summed layers.
        return static_cast<float>(0.982 * x / std::sqrt(1.0 + x * x));
    }

    std::array<double, layerCount> target_{};
    std::array<double, layerCount> gain_{};
    double master_ = 0.0;
    double masterTarget_ = 0.0;
    double rate_ = 0.0;
    double slew_ = 0.0;
};

} // namespace esl
