#include "core/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace esl {
namespace {

double sCurve(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

bool cancelled(const std::atomic_bool* cancel) { return cancel && cancel->load(std::memory_order_relaxed); }

Controls trajectory(const Controls& base, const ClipRequest& clip, double time) {
    auto controls = base;
    const double u = time / clip.seconds;
    switch (clip.motion) {
    case Motion::Start:
        controls.running = time >= 0.25;
        controls.throttle = 0.0;
        controls.load = clip.load;
        break;
    case Motion::Acceleration: {
        const double pedal = sCurve((u - 0.10) / 0.56);
        controls.throttle = std::lerp(0.04, 1.0, pedal);
        controls.load = std::lerp(0.22, clip.load, pedal);
        break;
    }
    case Motion::Deceleration: {
        const double lift = sCurve((u - 0.13) / 0.085);
        controls.throttle = 0.87 * (1.0 - lift);
        controls.load = std::lerp(clip.load, 0.24, lift);
        break;
    }
    case Motion::Shutdown:
        controls.running = time < 0.40;
        controls.throttle = 0.0;
        controls.load = time < 0.40 ? clip.load : 0.0;
        break;
    case Motion::Steady: break;
    case Motion::Drive: return programControls(Program::Drive, u * programDuration(Program::Drive), base);
    }
    return controls;
}

int correlatedLoopEnd(std::span<const StereoFrame> signal, int nominal, int search, int fade) {
    int bestEnd = nominal;
    double bestScore = -2.0;
    // Independent shafts, microphone loops and an active limiter each have multiple periods.
    for (int offset = -search; offset <= search; ++offset) {
        double cross = 0.0, energy = 1e-15;
        const int end = nominal + offset;
        for (int i = 0; i < fade; i += 13) {
            const auto& a = signal[i];
            const auto& b = signal[end + i];
            cross += 2.0 * (a.left * b.left + a.right * b.right);
            energy += a.left * a.left + a.right * a.right + b.left * b.left + b.right * b.right;
        }
        const double score = cross / energy - 0.035 * std::abs(offset) / search;
        if (score > bestScore) {
            bestEnd = end;
            bestScore = score;
        }
    }
    return bestEnd;
}

} // namespace

RenderedClip renderClip(Settings settings, const ClipRequest& request, int rate, const std::atomic_bool* cancel) {
    RenderedClip result;
    const bool steady = request.motion == Motion::Steady;
    const bool loop = steady && request.loop;
    const bool turbine = settings.model.kind == EngineKind::Turbine;
    const auto& model = settings.model;
    const auto duration = std::clamp(request.seconds, 0.5, 18.0);
    auto rpm = std::clamp(request.rpm, model.idleRpm, model.maxRpm);
    int frames = static_cast<int>(std::lround(duration * rate));
    const auto& transmission = settings.layers[static_cast<std::size_t>(LayerId::Transmission)];
    const bool correlate = turbine || settings.model.recording || (settings.controls.gear > 0 && transmission.gain > 0 && !transmission.mute) ||
        (settings.limiter.enabled && rpm >= settings.limiter.cutRpm);
    if (loop && !correlate) {
        const double cycle = 60.0 * (model.strokes / 2.0) / rpm;
        const double cycles = std::max(1.0, std::round(duration / cycle));
        frames = static_cast<int>(std::lround(cycles * cycle * rate));
        rpm = cycles * 60.0 * (model.strokes / 2.0) * rate / frames;
        rpm = std::clamp(rpm, model.idleRpm, model.maxRpm);
    }
    const int fade = loop ? static_cast<int>(rate * (correlate ? 0.120 : 0.087)) : 0;
    const int search = loop && correlate ? static_cast<int>(rate * 0.010) : 0;
    auto& controls = settings.controls;
    controls.program = Program::Manual;
    controls.programToken = 0;
    controls.running = request.motion != Motion::Start;
    controls.hold = false;
    controls.brake = false;
    controls.throttle = std::clamp(request.throttle, 0.0, 1.0);
    controls.load = std::clamp(request.load, 0.0, 1.0);
    if (!steady) controls = trajectory(controls, request, 0.0);

    Synthesizer synth(rate);
    if (controls.running) {
        synth.setSteady(settings, rpm);
        std::array<StereoFrame, 512> preroll;
        for (int i = 0; i < rate * 0.58; i += static_cast<int>(preroll.size())) {
            if (cancelled(cancel)) return {};
            synth.render(preroll);
        }
    } else {
        synth.reset(settings);
    }
    result.startRpm = result.peakRpm = synth.state().rpm;
    result.startLoad = steady ? controls.load : synth.state().load;
    result.samples.resize(frames + fade + search);
    for (std::size_t first = 0; first < result.samples.size(); first += 256) {
        if (cancelled(cancel)) return {};
        if (!steady) {
            controls = trajectory(controls, request, static_cast<double>(first) / rate);
            synth.apply(settings);
        }
        synth.render(std::span(result.samples).subspan(first, std::min<std::size_t>(256, result.samples.size() - first)));
        result.peakRpm = std::max(result.peakRpm, synth.state().rpm);
    }
    result.endRpm = synth.state().rpm;
    result.endLoad = synth.state().load;
    if (loop) {
        if (search > 0) frames = correlatedLoopEnd(result.samples, frames, search, fade);
        // The sample following the last exported frame becomes the first frame of the loop.
        // Blend the continuation into the original head with zero slope at both crossfade ends.
        for (int i = 0; i < fade; ++i) {
            const float blend = static_cast<float>(sCurve(static_cast<double>(i) / (fade - 1)));
            auto& head = result.samples[i];
            const auto& tail = result.samples[frames + i];
            head = {std::lerp(tail.left, head.left, blend), std::lerp(tail.right, head.right, blend)};
        }
        result.samples.resize(frames);
    } else {
        const int lead = static_cast<int>(rate * 0.034);
        const int tail = static_cast<int>(rate * (request.motion == Motion::Shutdown ? 0.43 : 0.095));
        for (int i = 0; i < frames; ++i) {
            const double gain = sCurve(static_cast<double>(i) / lead) * sCurve(static_cast<double>(frames - 1 - i) / tail);
            result.samples[i].left *= static_cast<float>(gain);
            result.samples[i].right *= static_cast<float>(gain);
        }
    }
    return result;
}

} // namespace esl
