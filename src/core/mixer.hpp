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
    void configure(const Layers& layers, double master, double rate, bool immediate) {
        const bool solo = std::any_of(layers.begin(), layers.end(), [](const Layer& layer) { return layer.solo; });
        for (std::size_t i = 0; i < layers.size(); ++i) {
            target_[i] = layers[i].mute || (solo && !layers[i].solo) ? 0.0 : layers[i].gain;
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
