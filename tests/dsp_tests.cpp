#include "core/dsp.hpp"
#include "core/listener.hpp"

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
    model.cylinders = 4;
    model.strokeMm = 86.0;
    model.liters = displacementLiters(4, 86, 86);
    require(std::abs(model.liters - 1.99823) < 0.00001 && std::abs(cylinderBoreMm(model) - 86) < 1e-8,
            "Bore/stroke and total swept volume disagree");
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
    for (std::size_t i = 0; i < static_cast<std::size_t>(LayerId::Recording); ++i) {
        auto solo = diesel;
        solo.layers[i].solo = true;
        const auto isolated = stationary(solo, rpm);
        require(energy(isolated) > 0.001 && energy(isolated) < energy(loaded), "Solo layer did not isolate distinct audio");
    }
    auto withoutRecording = diesel;
    withoutRecording.layers[static_cast<std::size_t>(LayerId::Recording)].solo = true;
    require(energy(stationary(withoutRecording, rpm)) == energy(loaded), "Unavailable recording intercepted solo");

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

void checkTransmission() {
    Settings settings;
    auto& layer = settings.layers[static_cast<std::size_t>(LayerId::Transmission)];
    require(layer.gain == 0.0, "Legacy settings enable transmission");
    layer.gain = 1.0;
    layer.solo = true;
    settings.controls.throttle = 0.82;
    settings.controls.load = 0.68;
    require(energy(stationary(settings, 1500)) == 0.0, "Transmission solo audible in neutral");
    for (auto kind : {EngineKind::Diesel, EngineKind::Turbine}) {
        settings.model.kind = kind;
        settings.controls.gear = 1;
        const auto first = stationary(settings, 1500);
        settings.controls.gear = 4;
        const auto fourth = stationary(settings, 1500);
        const double fundamental = 25 * (0.70 + 0.54 / gearRatios[4]) * layer.resonance / 30;
        require(energy(fourth) > .035 && spectralLine(fourth, fundamental) > spectralLine(first, fundamental) * 7,
                "Gear-dependent mesh tone missing");
        settings.controls.throttle = 0.0;
        settings.controls.load = 0.16;
        require(energy(stationary(settings, 1500)) < 0.8 * energy(fourth), "Gearbox does not react to lift");
        layer.mute = true;
        require(energy(stationary(settings, 1500)) == 0.0, "Muting transmission solo is not silent");
        layer.mute = false;
        settings.controls.throttle = 0.82;
        settings.controls.load = 0.68;
    }
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

void checkListeningPositions() {
    using P = ListeningPosition;
    auto take = [](P position, double distance) {
        ListenerFilter filter(rate);
        filter.reset({position, distance, 0.80});
        std::vector<StereoFrame> frames(rate);
        for (int i = 0; i < rate; ++i) {
            const double t = i * 2.0 * pi / rate;
            const float bass = static_cast<float>(0.24 * std::sin(120 * t));
            const float high = static_cast<float>(0.16 * std::sin(5200 * t));
            frames[i] = filter.process({bass + high, bass + high * 0.65f});
        }
        return frames;
    };
    const auto direct = take(P::Direct, 1), inside = take(P::Interior, 1);
    const auto near = take(P::Near, 2), chase = take(P::ThirdPerson, 8), far = take(P::Distant, 45);
    require(energy(near) > 1.15 * energy(chase) && energy(chase) > 1.7 * energy(far), "Exterior distance lost its level cue");
    const auto tilt = [](const auto& f) { return spectralLine(f, 5200) / spectralLine(f, 120); };
    require(tilt(inside) < tilt(direct) * 0.36 && tilt(far) < tilt(near) * 0.70,
            "Cabin transmission or distance filtering failed");
}

void checkLimiterAndDimensions() {
    Settings s;
    s.model.kind = EngineKind::Gasoline;
    s.model.cylinders = 4;
    s.model.liters = 2.0;
    s.model.strokeMm = 86;
    s.model.inertia = 1.0;
    s.model.torqueNm = 230;
    s.model.powerKw = 120;
    s.model.maxRpm = 6500;
    s.controls.running = true;
    s.controls.throttle = 1;
    s.controls.load = 0.4;
    s.limiter = {true, 5500, 5350};
    for (bool hold : {false, true}) {
        s.controls.hold = hold;
        s.controls.holdRpm = 6000;
        SimulationController sim;
        sim.setSteady(s, 5100);
        int cuts = 0, recoveries = 0;
        for (int i = 0; i < 4500; ++i) {
            const auto before = sim.state();
            sim.update(s, 0.002);
            const auto& after = sim.state();
            if (!before.limiterCut && after.limiterCut) {
                ++cuts;
                require(before.rpm >= 5499.9 && after.torqueNm == 0, "Limiter cut below threshold or left torque applied");
            }
            if (before.limiterCut && !after.limiterCut) {
                ++recoveries;
                require(before.rpm <= 5350.1, "Limiter ignored return threshold");
            }
        }
        require(cuts > 2 && recoveries > 2, "Limiter did not cycle under power or RPM hold");
    }
    Synthesizer synth(rate);
    synth.setSteady(s, 6000);
    bool sawCut = false, sawPower = false;
    bool sawCutCylinder = false;
    std::array<StereoFrame, 384> block;
    for (int i = 0; i < 200; ++i) {
        synth.render(block);
        sawCut |= synth.state().limiterCut;
        sawPower |= !synth.state().limiterCut;
        const auto& recent = synth.ignitions().latest;
        sawCutCylinder |= std::find(recent.begin(), recent.end(), Ignition::Cut) != recent.end();
    }
    require(sawCut && sawPower && sawCutCylinder, "Steady export latched or bypassed the limiter");
    s.layers[0].solo = true;
    s.model.compression = 7;
    const auto soft = stationary(s, 2000);
    s.model.compression = 14;
    const auto sharp = stationary(s, 2000);
    require(energy(sharp) > 1.12 * energy(soft), "Compression control did not affect combustion");
    s.model.strokeMm = 145;
    const auto longStroke = stationary(s, 2000);
    require(std::abs(energy(sharp) - energy(longStroke)) > 0.001, "Stroke control did not affect combustion at fixed displacement");
}

void checkOverrun() {
    Settings s;
    s.controls.running = true;
    s.controls.load = 0.25;
    s.model.overrunDrag = 0.45;
    SimulationController coast, free;
    coast.setSteady(s, 2000);
    auto legacy = s;
    legacy.model.overrunDrag = 0.0;
    free.setSteady(legacy, 2000);
    for (int i = 0; i < 150; ++i) {
        coast.update(s, 0.005);
        free.update(legacy, 0.005);
    }
    require(coast.state().overrun > 0.6 && coast.state().rpm + 200 < free.state().rpm,
            "Releasing the pedal did not engage overrun and engine drag");
    const auto lifted = coast.state().rpm;
    s.controls.throttle = 0.92;
    for (int i = 0; i < 80; ++i) coast.update(s, 0.005);
    require(coast.state().overrun < 0.03 && coast.state().rpm > lifted,
            "Reapplying the pedal did not restore drive during overrun");
    coast.setSteady(s, 1700);
    require(coast.state().overrun == 0.0, "Fixed RPM started on a stale lift");
}

void checkIgnitionTrace() {
    Settings s;
    s.model.kind = EngineKind::Gasoline;
    s.model.layout = Layout::Inline;
    s.model.cylinders = 4;
    s.model.firingOrder = {2, 1, 4, 3};
    s.faults.cylinder = 3;
    s.faults.sparkMiss = 1.0;
    Synthesizer synth(rate);
    synth.setSteady(s, 1600);
    std::array<StereoFrame, 48> millisecond;
    int last = 0, changes = 0;
    for (int i = 0; i < 350; ++i) {
        synth.render(millisecond);
        const auto& trace = synth.ignitions();
        const int current = trace.lastCylinder;
        if (!current || current == last) continue;
        if (last) {
            const auto previous = std::find(s.model.firingOrder.begin(), s.model.firingOrder.begin() + 4, last);
            require(s.model.firingOrder[(previous - s.model.firingOrder.begin() + 1) % 4] == current,
                    "Displayed ignition order diverged from the custom event schedule");
        }
        require(trace.latest[current - 1] == (current == 3 ? Ignition::Missed : Ignition::Fired),
                "A skipped spark was reported as a flash");
        ++changes;
        last = current;
    }
    const auto& trace = synth.ignitions();
    require(changes > 12 && trace.hasCompleted && trace.completed[2] == Ignition::Missed &&
            trace.completed[3] == Ignition::Fired, "No complete working cycle available for slow motion");
}
} // namespace

int main(int argc, char** argv) {
    const auto started = std::chrono::steady_clock::now();
    checkGeometry();
    checkSimulation();
    checkAudio(argc > 1 ? argv[1] : nullptr);
    checkTransmission();
    checkFaults();
    checkListeningPositions();
    checkLimiterAndDimensions();
    checkOverrun();
    checkIgnitionTrace();
    std::cout << "DSP verification took " << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()
              << " s\n";
}
