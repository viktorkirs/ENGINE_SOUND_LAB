#include "core/dsp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using namespace esl;
constexpr int rate = 48000;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

auto stationary(Settings settings, double rpm, int seconds = 1) {
    Synthesizer synth(rate);
    synth.setSteady(settings, rpm);
    std::vector<StereoFrame> buffer(rate * seconds);
    synth.render(buffer);
    return buffer;
}

double energy(std::span<const StereoFrame> a) {
    double total = 0.0;
    for (const auto& frame : a) {
        require(std::isfinite(frame.left) && std::abs(frame.left) < 1.0f, "Invalid left sample");
        require(std::isfinite(frame.right) && std::abs(frame.right) < 1.0f, "Invalid right sample");
        total += (frame.left * frame.left + frame.right * frame.right) * 0.5;
    }
    return std::sqrt(total / a.size());
}

double spectralLine(std::span<const StereoFrame> frames, double hz) {
    double real = 0.0, imaginary = 0.0;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const double angle = 2 * pi * hz * i / rate;
        const double sample = (frames[i].left + frames[i].right) * 0.5;
        real += sample * std::cos(angle);
        imaginary += sample * std::sin(angle);
    }
    return std::hypot(real, imaginary) / frames.size();
}

void writeProbe(const char* prefix, const char* name, std::span<StereoFrame> frames) {
    std::ofstream file(std::string(prefix) + name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(frames.data()), frames.size_bytes());
    require(static_cast<bool>(file), "Cannot write audio probe");
}

void checkGeometry() {
    Model model;
    model.layout = Layout::Radial;
    model.cylinders = 9;
    require(defaultFiringOrder(model) == FiringOrder{1, 3, 5, 7, 9, 2, 4, 6, 8}, "Nine-cylinder radial firing order");
    model.cylinders = 14;
    require(defaultFiringOrder(model) == FiringOrder{1, 10, 5, 14, 9, 4, 13, 8, 3, 12, 7, 2, 11, 6}, "Twin radial firing order");
    const auto phases = cylinderPhases(model);
    for (int i = 0; i < model.cylinders; ++i) {
        const auto& event = phases[i];
        const double angle = (event.cylinder - 1) / 14.0 - 0.5 * event.bank;
        require(std::abs(std::remainder(event.turn - angle, 1.0)) < 1e-14, "Radial throw/cylinder phase mismatch");
    }
    require(!validCylinderCount(Layout::Radial, 12), "Even cylinder count per radial row was accepted");
    require(!validFiringOrder(model, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}), "Impossible radial order accepted");
}

void checkSimulation() {
    Settings settings;
    settings.controls.running = true;
    settings.controls.throttle = 0.8;
    SimulationController light, heavy;
    auto heavySettings = settings;
    heavySettings.model.inertia *= 3.0;
    for (int i = 0; i < 750; ++i) {
        light.update(settings, 0.002);
        heavy.update(heavySettings, 0.002);
    }
    require(light.state().rpm > heavy.state().rpm + 120.0, "Inertia did not change spin-up");
    settings.controls.hold = true;
    settings.controls.holdRpm = 1550.0;
    settings.controls.load = 0.92;
    for (int i = 0; i < 1800; ++i) light.update(settings, 0.002);
    require(std::abs(light.state().rpm - 1550) < 0.1 && light.state().load > 0.9, "RPM hold changed with load");

    settings.model.kind = EngineKind::Turbine;
    SimulationController turbine;
    for (int i = 0; i < 2500; ++i) turbine.update(settings, 0.002);
    require(turbine.state().coreRpm > turbine.state().rpm * 8.0, "Core and output shaft not separated");
    settings.controls.running = false;
    for (int i = 0; i < 5700; ++i) turbine.update(settings, 0.002);
    require(turbine.state().mode == EngineMode::Off, "Shutdown failed to reach silence");
}

void checkAudio(const char* probePrefix) {
    Settings diesel;
    diesel.controls.throttle = 0.39;
    diesel.controls.load = 0.17;
    constexpr double rpm = 1500;
    const auto a = stationary(diesel, rpm);
    const auto again = stationary(diesel, rpm);
    require(std::equal(a.begin(), a.end(), again.begin(), [](auto x, auto y) {
        return x.left == y.left && x.right == y.right;
    }), "Same seed and state failed exact reproducibility");
    diesel.controls.load = 0.88;
    auto loaded = stationary(diesel, rpm);
    require(energy(loaded) > energy(a) * 1.20, "Engine load did not materially change output at held RPM");
    require(spectralLine(loaded, 150.0) > 0.02, "V12 firing frequency missing");
    if (probePrefix) writeProbe(probePrefix, "_diesel.f32", loaded);

    auto v6 = diesel;
    v6.model.cylinders = 6;
    v6.model.liters = 15.9;
    v6.model.bankAngle = 120;
    auto six = stationary(v6, rpm);
    require(spectralLine(six, 75.0) > spectralLine(loaded, 75.0) * 1.4, "V6 firing frequency not distinct from V12");
    if (probePrefix) writeProbe(probePrefix, "_v6.f32", six);

    auto gasoline = diesel;
    gasoline.model.kind = EngineKind::Gasoline;
    gasoline.model.cylinders = 8;
    gasoline.model.liters = 5.7;
    gasoline.model.bankAngle = 90;
    gasoline.model.boost = Boost::None;
    auto eight = stationary(gasoline, rpm);
    require(spectralLine(eight, 100.0) > spectralLine(loaded, 100.0) * 1.4, "Gasoline firing frequency not distinct");
    if (probePrefix) writeProbe(probePrefix, "_v8.f32", eight);

    auto twoStroke = gasoline;
    twoStroke.model.strokes = 2;
    twoStroke.layers[1].solo = true;
    const auto two = stationary(twoStroke, rpm);
    require(spectralLine(two, 200.0) > 0.025 && energy(two) > 0.015, "Two-stroke exhaust cadence missing");

    auto muted = diesel;
    for (auto& layer : muted.layers) layer.mute = true;
    const auto silence = stationary(muted, rpm);
    require(energy(silence) == 0.0, "Muting all layers did not produce silence");
    for (std::size_t i = 0; i < layerCount; ++i) {
        auto solo = diesel;
        solo.layers[i].solo = true;
        const auto isolated = stationary(solo, rpm);
        require(energy(isolated) > 0.001 && energy(isolated) < energy(loaded), "Solo layer did not isolate distinct audio");
    }

    auto turbine = diesel;
    turbine.model.kind = EngineKind::Turbine;
    turbine.model.maxRpm = 3000;
    turbine.model.coreMaxRpm = 32000;
    auto jet = stationary(turbine, rpm);
    require(energy(jet) > 0.015 && spectralLine(jet, 150.0) < spectralLine(loaded, 150.0) * 0.45,
            "Turbine uses piston engine firing cadence or is inaudible");
    if (probePrefix) writeProbe(probePrefix, "_turbine.f32", jet);

    // Legal extreme settings exercise densely overlapping events and filter headroom.
    Settings extreme;
    extreme.model.cylinders = 24;
    extreme.model.strokes = 2;
    extreme.model.maxRpm = 18000;
    extreme.model.liters = 120;
    extreme.controls.master = 1.0;
    extreme.controls.load = extreme.controls.throttle = 1.0;
    for (auto& layer : extreme.layers) {
        layer.gain = 1.5;
        layer.drive = 1.0;
        layer.cutoff = 14000;
    }
    require(energy(stationary(extreme, 18000)) > 0.03, "Extreme engine has no useful output");

    std::cout << "RMS V12 low/high load " << energy(a) << " / " << energy(loaded)
              << ", V6 " << energy(six) << ", V8 " << energy(eight) << ", turbine " << energy(jet) << '\n';
}

void checkFaults() {
    Settings one;
    one.model.kind = EngineKind::Gasoline;
    one.model.layout = Layout::Inline;
    one.model.cylinders = 1;
    one.model.liters = 0.45;
    one.model.boost = Boost::None;
    one.controls.throttle = 0.49;
    one.controls.load = 0.8;
    one.layers[0].solo = true;
    const auto healthy = stationary(one, 1500);
    one.faults.sparkMiss = 1.0;
    require(energy(stationary(one, 1500)) < energy(healthy) * 0.12, "Disabled spark still produced normal combustion");
    one.faults.sparkMiss = 0.42;
    const auto intermittent = stationary(one, 1500);
    require(energy(intermittent) < energy(healthy) * 0.95 && energy(intermittent) > energy(healthy) * 0.32,
            "Intermittent misfire did not change some working strokes");

    auto diesel = one;
    diesel.model.kind = EngineKind::Diesel;
    diesel.faults = {};
    const auto clean = stationary(diesel, 1500);
    diesel.faults.injectorWear = 0.86;
    const auto broken = stationary(diesel, 1500);
    std::vector<StereoFrame> delta(clean.size());
    for (std::size_t i = 0; i < clean.size(); ++i) delta[i] = {clean[i].left - broken[i].left, clean[i].right - broken[i].right};
    require(energy(delta) > energy(clean) * 0.25, "Injector wear did not alter pressure impulses");

    Settings loaded;
    loaded.controls.running = true;
    loaded.controls.throttle = 0.75;
    loaded.controls.load = 0.84;
    auto overloaded = loaded;
    overloaded.faults.overload = 0.82;
    SimulationController normal, straining;
    for (int i = 0; i < 2000; ++i) {
        normal.update(loaded, 0.002);
        straining.update(overloaded, 0.002);
    }
    require(normal.state().rpm > straining.state().rpm + 100, "Overload did not add resistance to the shaft");
}
} // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now();
    checkGeometry();
    checkSimulation();
    checkAudio(argc > 1 ? argv[1] : nullptr);
    checkFaults();
    std::cout << "DSP verification took " << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
              << " s\n";
}
