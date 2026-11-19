#include "core/dsp.hpp"
#include "core/listener.hpp"
#include "core/recording.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace esl {
namespace {

constexpr double tau = 2.0 * pi;
constexpr double controlRate = 500.0;
constexpr std::size_t sineSize = 2048;

double unit(double x) { return std::clamp(x, 0.0, 1.0); }

double pole(double hz, double rate) { return -std::expm1(-tau * hz / rate); }

double interpolate(double& value, double goal, double coefficient) {
    value += (goal - value) * coefficient;
    return value;
}

const auto sineTable = [] {
    std::array<float, sineSize + 1> table{};
    for (std::size_t i = 0; i < sineSize; ++i) table[i] = std::sin(tau * i / sineSize);
    return table;
}();

float sine(double phase) {
    const double index = (phase - std::floor(phase)) * sineSize;
    const auto low = static_cast<std::size_t>(index);
    return std::lerp(sineTable[low], sineTable[low + 1], static_cast<float>(index - low));
}

double advance(double& phase, double hz, double rate) {
    phase += hz / rate;
    phase -= std::floor(phase);
    return phase;
}

float harmonic(double phase, double hz, double rate) {
    // Use the original output Nyquist limit for blade and gear orders.
    const double edge = unit((rate * 0.222 - hz) / (rate * 0.055));
    return static_cast<float>(edge * sine(phase));
}

struct Noise {
    std::uint32_t bits = 1;
    float next() {
        bits ^= bits << 13;
        bits ^= bits >> 17;
        bits ^= bits << 5;
        return static_cast<float>(bits >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

struct Pole {
    double alpha = 0.0;
    double z = 0.0;
    double tick(double x) { return interpolate(z, x, alpha); }
};

struct Resonator {
    double b = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void tune(double frequency, double q, double rate) {
        const double w = tau * std::clamp(frequency, 18.0, rate * 0.16) / rate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double norm = 1.0 / (1.0 + alpha);
        b = alpha * norm;
        a1 = -2.0 * std::cos(w) * norm;
        a2 = (1.0 - alpha) * norm;
    }

    double tick(double in) {
        const double out = b * in + z1;
        z1 = z2 - a1 * out;
        z2 = -b * in - a2 * out;
        return out;
    }
};

struct Pulse {
    double fast = 0.0, slow = 0.0;

    void excite(double pressure) {
        fast += pressure;
        slow += pressure;
    }

    double tick(double attack, double decay) {
        fast *= attack;
        slow *= decay;
        return slow - fast;
    }
};

struct RunnerQueue {
    static constexpr unsigned size = 1024;
    std::array<float, size> samples{};
    unsigned head = 0;

    void push(double delay, double pressure) {
        const auto whole = static_cast<unsigned>(delay);
        const double fraction = delay - whole;
        samples[(head + whole) % size] += static_cast<float>((1.0 - fraction) * pressure);
        samples[(head + whole + 1) % size] += static_cast<float>(fraction * pressure);
    }

    double pop() {
        const float value = samples[head];
        samples[head] = 0;
        head = (head + 1) % size;
        return value;
    }
};

struct Pipe {
    static constexpr unsigned size = 2048;
    std::array<float, size> wave{};
    unsigned head = 0;
    Pole dispersion;

    double tap(double delay) const {
        const auto offset = static_cast<unsigned>(delay);
        return std::lerp(wave[(head - offset) % size], wave[(head - offset - 1) % size],
                         static_cast<float>(delay - offset));
    }

    double tick(double incoming, double delay, double damping) {
        const double reflected = dispersion.tick(tap(delay));
        wave[head] = static_cast<float>(incoming - (0.64 - 0.44 * damping) * reflected);
        const double outlet = 0.80 * tap(delay * 0.47) + 0.32 * incoming - 0.23 * reflected;
        head = (head + 1) % size;
        return outlet;
    }
};

struct Bank {
    RunnerQueue runner;
    RunnerQueue lateInjection;
    Pipe pipe;
    Pulse combustion, crack, injection, exhaust, intake, valves;
    Resonator block, muffler, airbox, metal;
    Pole combustionDc, exhaustDc, intakeDc, valveDc;
};

enum class EventKind { Fire, Blowdown, Inhale, Valve };

struct Event {
    double phase = 0.0;
    double travel = 0.0;
    double weight = 0.0;
    unsigned bank = 0;
    unsigned pipe = 0;
    unsigned cylinder = 0;
    EventKind kind = EventKind::Fire;
};

struct CylinderCharge {
    double exhaust = 1.0;
    double afterfire = 0.0;
};

struct Voice {
    Model model;
    double rate = 96000.0;
    std::array<Event, maxCylinders * 5> events{};
    std::size_t eventCount = 0, eventIndex = 0;
    double turn = 0.0, eventOffset = 0.0;
    std::array<Bank, 2> banks{};
    std::array<CylinderCharge, maxCylinders> charge{};
    IgnitionTrace ignitions;
    int firstCylinder = 1, attempts = 0;
    std::array<std::array<Pole, 2>, layerCount> outputFilters{};
    Layers tone = defaultLayers();
    Faults fault;
    AudioMixer mixer;
    RecordingPlayer recording;
    Resonator recordedBody;
    Pole recordedLow;
    double recordedCoast = 0.0, recordedDrive = 0.0;
    double pressureScale = 1.0, strokeAspect = 1.0, strokeOffsetMeters = 0.0, recordedDetail = 1.0;
    Noise soundNoise, eventNoise, faultNoise;
    Noise bearingNoise;
    Pole bearingLow, bearingHigh;
    double meshPhase = 0.0, finalDrivePhase = 0.0, axlePhase = 0.0;
    double outputRatio = 0.0, engagement = 0.0, shiftSlew = 0.0;
    Resonator chassis, shell, blower, air, turbineExhaust;
    Pole wind, fizz, jet, flutter;
    double smoothedRpm = 0.0, smoothedCore = 0.0, slew = 0.0;
    double shaft = 0.0, gear = 0.0, boost = 0.0, core = 0.0, generator = 0.0, powerWheel = 0.0, surge = 0.0;
    double volScale = 1.0, bore = 1.0, cylinderLevel = 1.0;
    double burnFast = 0.0, burnSlow = 0.0, knockFast = 0.0, knockSlow = 0.0;
    double exhaustFast = 0.0, exhaustSlow = 0.0, inhaleFast = 0.0, inhaleSlow = 0.0;
    double valveFast = 0.0, valveSlow = 0.0;
    double pipeDelay = 0.0, pipeTarget = 0.0;
    double speed = 0.0, rotation = 0.0, coreRotation = 0.0;
    double voiceEnvelope = 0.0, engineEnvelope = 0.0, lug = 0.0;

    void prepare(const Settings& settings, const EngineState& state, double oversampledRate, double phase) {
        *this = Voice{};
        model = settings.model;
        recording.prepare(model.recording, oversampledRate);
        rate = oversampledRate;
        tone = settings.layers;
        turn = phase * (model.strokes / 2.0);
        smoothedRpm = state.rpm;
        recordedCoast = state.overrun;
        smoothedCore = state.coreRpm;
        slew = -std::expm1(-1.0 / (0.006 * rate));
        shiftSlew = -std::expm1(-1.0 / (0.042 * rate));
        if (state.gear > 0) {
            outputRatio = 1.0 / gearRatios[state.gear];
            engagement = state.clutch;
        }
        bearingNoise.bits = (model.seed ^ 0x1b56c4e9u) | 1u;
        bearingLow.alpha = pole(125.0, rate);
        bearingHigh.alpha = pole(3150.0, rate);
        soundNoise.bits = model.seed ^ 0xf6a2bf57u;
        eventNoise.bits = model.seed ^ 0x4080e91du;
        faultNoise.bits = model.seed ^ 0x83ae7f4du;
        if (soundNoise.bits == 0) soundNoise.bits = 1;
        if (eventNoise.bits == 0) eventNoise.bits = 1;
        if (faultNoise.bits == 0) faultNoise.bits = 1;
        cylinderLevel = std::min(1.45, 1.20 / std::pow(model.cylinders / 5.0, 0.30));
        if (model.kind != EngineKind::Turbine) {
            const auto phases = cylinderPhases(model);
            firstCylinder = phases[0].cylinder;
            const double cycle = model.strokes / 2.0;
            for (int i = 0; i < model.cylinders; ++i) {
                const auto& cylinder = phases[i];
                const auto bank = static_cast<unsigned>(cylinder.bank);
                const auto pipe = model.exhaustTracts == 1 ? 0u : bank;
                const double travel = std::min(1022.0, cylinder.runnerMeters * rate / 480.0);
                auto add = [&](double offset, EventKind kind, double weight) {
                    double eventTurn = cylinder.turn + offset;
                    if (eventTurn >= cycle) eventTurn -= cycle;
                    events[eventCount++] = {eventTurn, travel, weight, bank, pipe,
                        static_cast<unsigned>(cylinder.cylinder - 1), kind};
                };
                add(0.0, EventKind::Fire, cylinder.weight);
                add(model.strokes == 2 ? 0.29 : 0.397, EventKind::Blowdown, cylinder.weight);
                add(model.strokes == 2 ? 0.37 : 1.105, EventKind::Inhale, cylinder.weight);
                if (model.strokes == 4) {
                    add(0.393, EventKind::Valve, 1.0);
                    add(1.49, EventKind::Valve, 0.67);
                }
            }
            std::sort(events.begin(), events.begin() + eventCount,
                [](const Event& a, const Event& b) { return a.phase < b.phase; });
            eventIndex = static_cast<std::size_t>(std::lower_bound(events.begin(), events.begin() + eventCount, turn,
                [](const Event& event, double t) { return event.phase < t; }) - events.begin());
            if (eventIndex == eventCount) {
                eventIndex = 0;
                eventOffset = cycle;
            }
        }
        tune(settings, true);
    }

    void tune(const Settings& settings, bool immediate) {
        mixer.configure(settings.layers, settings.controls.master, rate, immediate,
                        model.recording && model.kind != EngineKind::Turbine, settings.listener.position);
        constexpr double change = 0.064;
        fault.approach(settings.faults, immediate ? 1.0 : change);
        for (std::size_t i = 0; i < tone.size(); ++i) {
            auto& current = tone[i];
            const auto& next = settings.layers[i];
            interpolate(current.resonance, next.resonance, change);
            interpolate(current.cutoff, next.cutoff, change);
            interpolate(current.shape, next.shape, change);
            interpolate(current.amount, next.amount, change);
            interpolate(current.damping, next.damping, change);
            interpolate(current.drive, next.drive, change);
            for (auto& f : outputFilters[i]) f.alpha = pole(current.cutoff, rate);
        }
        const auto& c = tone[0];
        const auto& e = tone[1];
        const auto& i = tone[2];
        const auto& m = tone[3];
        const auto& t = tone[4];
        const auto& b = tone[5];
        if (model.recording) {
            const auto& mic = tone[static_cast<std::size_t>(LayerId::Recording)];
            recordedBody.tune(mic.resonance, 0.6 + 2.8 * (1.0 - mic.damping), rate);
        }
        const double boreTarget = std::pow(std::clamp(model.liters / model.cylinders, 0.025, 12.0), 0.27);
        interpolate(bore, boreTarget, immediate ? 1.0 : change);
        volScale = 0.71 + 0.40 * bore;
        const bool diesel = model.kind == EngineKind::Diesel;
        const double stroke = pistonStrokeMm(model);
        const double squareStroke = std::cbrt(4.0e6 * model.liters / (pi * model.cylinders));
        strokeOffsetMeters = (stroke - squareStroke) * 0.001;
        strokeAspect = std::clamp(stroke / cylinderBoreMm(model), 0.45, 2.4);
        pressureScale = std::clamp(compressionRatio(model) / (diesel ? 16.0 : 10.0), 0.38, 2.2);
        recordedDetail = 1.26 * pressureScale / std::sqrt(strokeAspect);
        recordedLow.alpha = pole(265.0, rate);
        auto decay = [this](double ms) { return std::exp(-1000.0 / (ms * rate)); };
        burnFast = decay((diesel ? 0.21 : 0.59) * (0.68 + c.shape * 0.90) * bore / pressureScale);
        burnSlow = decay((diesel ? 2.6 : 3.6) * (0.60 + c.shape * 1.40) * bore * std::sqrt(strokeAspect));
        knockFast = decay(0.105 + 0.13 * c.shape);
        knockSlow = decay(0.55 + 0.55 * c.shape);
        exhaustFast = decay((0.36 + e.shape * 0.65) * bore);
        exhaustSlow = decay((2.4 + e.shape * 4.1) * bore * std::sqrt(strokeAspect));
        inhaleFast = decay(0.66 + 1.7 * i.shape);
        inhaleSlow = decay(5.1 + 3.4 * i.shape);
        valveFast = decay(0.10 + m.damping * 0.18);
        valveSlow = decay(0.44 + (1.0 - m.damping) * 0.8);
        pipeTarget = rate * (0.0017 + 0.0066 * e.shape);
        if (immediate) pipeDelay = pipeTarget;
        for (std::size_t j = 0; j < banks.size(); ++j) {
            auto& bank = banks[j];
            const double skew = j == 0 ? 1.0 : 1.085;
            bank.block.tune(c.resonance * skew, 0.8 + 2.6 * (1.0 - c.damping), rate);
            bank.muffler.tune(e.resonance * skew, 0.82 + (1.0 - e.damping) * 1.95, rate);
            bank.airbox.tune(i.resonance * skew, 0.72 + (1.0 - i.damping) * 2.1, rate);
            bank.metal.tune(m.resonance * skew, 0.95 + (1.0 - m.damping) * 4.8, rate);
            bank.combustionDc.alpha = pole(105.0 + 215.0 * c.damping, rate);
            bank.exhaustDc.alpha = pole(18.0 + 17.0 * e.damping, rate);
            bank.intakeDc.alpha = pole(78.0, rate);
            bank.valveDc.alpha = pole(435.0, rate);
            bank.pipe.dispersion.alpha = pole(980.0 + 3600.0 * (1.0 - e.damping), rate);
        }
        chassis.tune(b.resonance * (1.12 - 0.34 * b.shape), 0.70 + 3.0 * (1.0 - b.damping), rate);
        shell.tune(b.resonance * (2.35 + 2.25 * (1.0 - b.shape)), 1.10 + 3.2 * (1.0 - b.damping), rate);
        blower.tune(t.resonance, 0.82 + (1.0 - t.damping) * 3.0, rate);
        air.tune(i.resonance * 1.72, 0.85, rate);
        turbineExhaust.tune(e.resonance, 0.65 + (1.0 - e.damping) * 1.85, rate);
        wind.alpha = pole(660.0 + 1400.0 * i.shape, rate);
        fizz.alpha = pole(5800.0 + 4800.0 * i.shape, rate);
        jet.alpha = pole(170.0 + 1170.0 * (1.0 - e.damping), rate);
        flutter.alpha = pole(16.0, rate);
    }

    Ignition fire(const Event& event, const EngineState& state, Bank& bank, double weight, bool diesel) {
        auto& cylinder = charge[event.cylinder];
        if (state.limiterCut) {
            cylinder = {0.09, 0.0};
            return Ignition::Cut;
        }
        const bool affected = event.cylinder + 1 == static_cast<unsigned>(fault.cylinder);
        const double lean = std::max(0.0, -fault.fuelBias);
        const double rich = std::max(0.0, fault.fuelBias);
        const double spark = !diesel && affected ? fault.sparkMiss : 0.0;
        const double injector = diesel && affected ? fault.injectorWear : 0.0;
        const double irregular = (diesel ? 0.37 : 0.54) * lean * lean +
            (diesel ? 0.17 : 0.47) * rich * rich + 0.23 * injector * injector + 0.085 * lug;
        const double probability = 1.0 - (1.0 - spark) * (1.0 - irregular);
        const double random = faultNoise.next();
        const bool missed = (random + 1.0) * 0.5 < probability;
        const double fuel = 1.0 - 0.70 * lean - 0.21 * rich;
        const double uneven = 1.0 + faultNoise.next() * (0.34 * lean + 0.26 * rich + 0.57 * injector + 0.39 * lug);
        const double pressure = fuel * uneven * (missed ? 0.028 : 1.0);
        const double knock = (missed ? 0.0 : pressure) * (1.0 + 0.77 * lean + 0.74 * injector + 0.78 * lug);
        bank.combustion.excite(weight * state.burn * (0.36 + 1.46 * state.fuel) * pressure * std::sqrt(pressureScale));
        bank.crack.excite(weight * state.burn * (diesel ? 0.75 + 1.04 * state.fuel : 0.26 + 0.54 * state.fuel) * knock * pressureScale);

        cylinder.exhaust = missed ? 0.13 + 0.15 * rich : pressure * (1.0 + 0.17 * rich);
        cylinder.afterfire = rich * (!diesel ? 0.91 : 0.28) * (missed ? 0.85 : 0.12) * (0.65 + 0.35 * random);
        if (injector > 0.0) {
            const double delay = rate * (0.00056 + 0.0024 * injector * (0.52 + 0.48 * random));
            bank.lateInjection.push(delay, state.burn * injector * (0.40 + 0.48 * state.fuel) * weight);
        }
        if (state.burn < 0.02) return Ignition::Inactive;
        return missed ? Ignition::Missed : Ignition::Fired;
    }

    void strike(const Event& event, const EngineState& state) {
        auto& bank = banks[event.bank];
        const bool diesel = model.kind == EngineKind::Diesel;
        const double variation = 1.0 + eventNoise.next() * (0.025 + 0.44 * model.irregularity);
        const double weight = event.weight * variation * volScale;
        switch (event.kind) {
        case EventKind::Fire: {
            const int cylinder = static_cast<int>(event.cylinder) + 1;
            if (cylinder == firstCylinder) {
                if (attempts == model.cylinders) {
                    ignitions.completed = ignitions.latest;
                    ignitions.hasCompleted = true;
                }
                attempts = 0;
            }
            ++attempts;
            ignitions.lastCylinder = cylinder;
            ignitions.latest[event.cylinder] = fire(event, state, bank, weight, diesel);
            break;
        }
        case EventKind::Blowdown: {
            auto& runner = banks[event.pipe].runner;
            const auto& combustion = charge[event.cylinder];
            const double pressure = weight * (diesel ? 1.0 : 1.08);
            runner.push(event.travel, pressure * (0.13 * state.starter +
                state.burn * (0.35 + 1.47 * state.fuel) * combustion.exhaust));
            if (combustion.afterfire > 0.0) {
                const double delay = std::min(1022.0, event.travel + rate * 0.0028);
                runner.push(delay, pressure * state.burn * combustion.afterfire);
            }
            break;
        }
        case EventKind::Inhale:
            bank.intake.excite(weight * (0.11 + (diesel ? 0.21 : 0.08) * speed +
                0.50 * state.throttle + 0.24 * state.load) * (0.42 + 0.58 * state.burn));
            break;
        case EventKind::Valve:
            bank.valves.excite(weight * (0.28 + 0.55 * speed) * (0.80 + 0.20 * model.loss) *
                (1.0 + fault.mechanicalWear * (1.4 + 0.82 * faultNoise.next())));
            break;
        }
    }

    StereoFrame shaped(std::size_t layer, double left, double right) {
        const double drive = tone[layer].drive;
        const auto saturate = [drive](double x) { return x * (1.0 + 1.7 * drive) / (1.0 + std::abs(x) * (0.05 + drive)); };
        return {static_cast<float>(outputFilters[layer][0].tick(saturate(left))),
                static_cast<float>(outputFilters[layer][1].tick(saturate(right)))};
    }

    StereoFrame centered(std::size_t layer, double mono, double width = 0.0) {
        return shaped(layer, mono + width, mono - width);
    }

    StereoFrame transmission(const EngineState& state) {
        constexpr auto id = static_cast<std::size_t>(LayerId::Transmission);
        if (!mixer.audible(id)) return {};
        const bool inGear = state.gear > 0 && state.mode != EngineMode::Off;
        interpolate(engagement, inGear ? state.clutch : 0.0, shiftSlew);
        if (!inGear && engagement < 1e-7) return {};
        if (inGear) interpolate(outputRatio, 1.0 / gearRatios[state.gear], shiftSlew);
        const auto& layer = tone[id];
        const double axleHz = rotation * outputRatio;
        const double axle = advance(axlePhase, axleHz, rate);
        // Two continuous meshes, one on the selected pair and one on the output shaft.
        const double meshHz = rotation * (0.70 + 0.54 * outputRatio) * layer.resonance / 30.0;
        const double finalHz = axleHz * 15.0;
        const double mesh = advance(meshPhase, meshHz, rate);
        const double finalDrive = advance(finalDrivePhase, finalHz, rate);
        const double coast = std::max(state.overrun, (1.0 - state.throttle) * speed);
        const double traction = (0.18 + 0.52 * state.throttle + 0.38 * state.load) * (1.0 - 0.50 * coast);
        const double reverseFlank = coast * layer.damping;
        const double whine = harmonic(mesh, meshHz, rate) * (0.23 + 0.77 * traction) +
            harmonic(mesh * 2.0 + 0.12, meshHz * 2.0, rate) * (0.15 * traction + 0.65 * reverseFlank) +
            harmonic(finalDrive, finalHz, rate) * (0.26 + 0.38 * reverseFlank);
        const double bearing = bearingHigh.tick(bearingNoise.next());
        const double band = bearing - bearingLow.tick(bearing);
        const double roughness = band * (0.60 + 0.26 * sine(axle) + 0.30 * traction);
        const double envelope = engineEnvelope * engagement * (0.22 + 0.47 * speed);
        return centered(id, envelope * (layer.shape * whine + layer.amount * roughness));
    }

    void pistons(const EngineState& state, float noise, double breeze, double hiss,
                 std::array<StereoFrame, layerCount>& out) {
        const double cycle = model.strokes / 2.0;
        if (rotation > 0.07) {
            const double end = turn + rotation / rate;
            while (events[eventIndex].phase + eventOffset <= end) {
                strike(events[eventIndex], state);
                if (++eventIndex == eventCount) {
                    eventIndex = 0;
                    eventOffset += cycle;
                }
            }
            turn = end;
            if (turn >= cycle) {
                turn -= cycle;
                eventOffset -= cycle;
            }
        }
        const auto& c = tone[0];
        const auto& e = tone[1];
        const auto& i = tone[2];
        const auto& m = tone[3];
        const auto& t = tone[4];
        const auto& b = tone[5];
        std::array<double, 2> combustion{}, exhaust{}, intake{}, mechanical{}, rawPressure{};
        interpolate(pipeDelay, pipeTarget, slew);
        const bool diesel = model.kind == EngineKind::Diesel;
        const double crankOrder = sine(turn) + 0.48 * sine(2.0 * turn + 0.14);
        for (std::size_t j = 0; j < banks.size(); ++j) {
            auto& bank = banks[j];
            const double chamber = bank.combustion.tick(burnFast, burnSlow);
            const double crack = bank.crack.tick(knockFast, knockSlow);
            bank.injection.excite(bank.lateInjection.pop());
            const double late = bank.injection.tick(knockFast, knockSlow);
            const double impact = chamber - bank.combustionDc.tick(chamber);
            combustion[j] = cylinderLevel * (0.47 * impact + 0.38 * bank.block.tick(impact + 0.5 * crack + 0.38 * late) +
                (diesel ? 0.42 : 0.22) * c.amount * crack * (1.0 + noise * 0.37) +
                late * (0.25 + 0.36 * noise));

            bank.exhaust.excite(bank.runner.pop());
            const double breath = bank.exhaust.tick(exhaustFast, exhaustSlow);
            rawPressure[j] = breath - bank.exhaustDc.tick(breath);
            const double exhaustJet = 0.28 * (1.0 - e.damping) * state.fuel * hiss * std::min(1.6, breath);
            const double wave = bank.pipe.tick(rawPressure[j] + exhaustJet, pipeDelay * (1.0 + 0.14 * j), e.damping);
            exhaust[j] = cylinderLevel * (wave * (0.63 + 0.35 * (1.0 - e.damping)) +
                (0.26 + 1.27 * e.amount) * bank.muffler.tick(wave));

            const double suction = bank.intake.tick(inhaleFast, inhaleSlow);
            const double intakePulse = suction - bank.intakeDc.tick(suction);
            const double intakeAir = (0.45 * breeze + 0.85 * i.shape * hiss) *
                (0.13 * engineEnvelope + 0.66 * std::min(1.7, suction));
            intake[j] = cylinderLevel * i.amount * (0.53 * intakePulse + 0.83 * bank.airbox.tick(intakePulse + intakeAir) +
                intakeAir * (0.34 + 0.47 * speed));

            const double valve = bank.valves.tick(valveFast, valveSlow);
            const double tap = valve - bank.valveDc.tick(valve);
            const double pistonSpeedOffset = 2.0 * strokeOffsetMeters * rotation;
            const double bearing = hiss * engineEnvelope * (0.11 + 0.34 * m.shape) *
                (0.38 + 0.87 * state.fuel + 0.68 * speed) * std::clamp(1.0 + 0.060 * pistonSpeedOffset, 0.65, 2.0);
            const double clatter = tap * hiss * (0.60 + 1.05 * m.shape + 0.86 * fault.mechanicalWear);
            mechanical[j] = m.amount * (0.49 * bank.metal.tick(0.62 * crack + tap + 0.33 * late + 1.4 * bearing + clatter) +
                0.25 * tap * (0.55 + 0.40 * noise) + 0.29 * bearing + 0.41 * clatter);
        }

        const double gearHz = rotation * (state.gear == 0 ? 7.0 : 10.0 + (6 - state.gear) * 2.3);
        const double gearPhase = advance(gear, gearHz, rate);
        // Fade the mesh bed to silence at the bottom of the gear-noise fader.
        const double meshGain = std::min(0.19, m.shape) + 0.81 * m.shape;
        const double mesh = (harmonic(gearPhase, gearHz, rate) * (0.75 + 0.25 * sine(2 * turn)) +
            0.27 * harmonic(gearPhase * 2.0 + 0.07, gearHz * 2.0, rate)) * meshGain;
        const double motorHz = 380.0 + 4.8 * rotation;
        const double motorPhase = advance(shaft, motorHz, rate);
        const double starter = state.starter * (0.100 * sine(motorPhase) * (0.66 + 0.34 * sine(2 * turn)) +
            0.039 * sine(3 * motorPhase) + 0.13 * breeze);
        const double chatter = fault.mechanicalWear * engineEnvelope * (0.06 * mesh + 0.043 * hiss) *
            (0.34 + 0.66 * sine(turn * 3.0) * sine(turn * 3.0));
        const double engineGear = starter + chatter + engineEnvelope * m.amount * (0.019 + 0.13 * state.load) * mesh;
        const double bodyInput = (rawPressure[0] + rawPressure[1]) * (0.27 + 0.26 * bore) +
            engineEnvelope * (0.19 + 0.31 * state.load + 0.45 * lug) * (crankOrder + 0.19 * breeze) *
            (0.66 + (model.layout == Layout::Inline || model.layout == Layout::Radial ? 0.30 : 0.0));
        const double structure = b.amount * ((0.59 + 0.55 * b.shape) * chassis.tick(bodyInput) +
            (0.32 - 0.19 * b.shape) * shell.tick(bodyInput + 0.34 * (mechanical[0] + mechanical[1])));

        double whistle = 0.0;
        if (model.boost != Boost::None) {
            const bool belt = model.boost == Boost::Supercharger;
            const double hz = belt ? 180.0 + rotation * (14.0 + t.resonance / 165.0) :
                t.resonance * (0.25 + 1.13 * state.boost);
            const double phase = advance(boost, hz, rate);
            const double rotor = harmonic(phase + 0.018 * breeze, hz, rate) +
                (belt ? 0.39 : 0.16) * harmonic(phase * 2.0, hz * 2.0, rate) +
                0.11 * harmonic(phase * 3.0, hz * 3.0, rate);
            whistle = t.amount * state.boost * (belt ? 0.46 + 0.54 * engineEnvelope : std::sqrt(state.boost)) *
                (0.29 * rotor + 0.65 * blower.tick(hiss + 0.4 * breeze));
        }

        const auto mid = [](const std::array<double, 2>& s) { return 0.63 * (s[0] + s[1]); };
        const auto side = [](const std::array<double, 2>& s) { return s[0] - s[1]; };
        out[0] = centered(0, mid(combustion), 0.15 * side(combustion));
        out[1] = centered(1, mid(exhaust), model.exhaustTracts == 2 ? 0.20 * side(exhaust) : 0.0);
        out[2] = centered(2, mid(intake), 0.13 * side(intake));
        out[3] = centered(3, mid(mechanical) + engineGear, 0.15 * side(mechanical));
        out[4] = centered(4, whistle, 0.06 * whistle * sine(turn));
        out[5] = centered(5, structure, 0.041 * side(rawPressure) * b.amount);
        if (model.recording) {
            constexpr auto id = static_cast<std::size_t>(LayerId::Recording);
            const auto& mic = tone[id];
            const double coast = interpolate(recordedCoast, state.overrun, slew);
            const double sample = recording.tick(smoothedRpm, coast * mic.shape);
            const double low = recordedLow.tick(sample);
            const double texture = low + recordedDetail * (sample - low) + 1.45 * mic.amount * recordedBody.tick(sample);
            double loadGain = 0.90 + mic.shape * (1.55 * state.fuel - 0.50);
            if (!model.recording->loops.back().coast.empty()) {
                const double shaftLoad = unit(state.torqueNm / model.torqueNm);
                loadGain += 0.72 * mic.shape * interpolate(recordedDrive, shaftLoad, slew);
            }
            const double faultMix = 1.0 - 0.55 * std::max({std::abs(fault.fuelBias), fault.injectorWear,
                                                          fault.mechanicalWear, fault.overload});
            out[id] = centered(id, 2.4 * texture * engineEnvelope * state.burn * loadGain * faultMix *
                (0.18 + 0.82 * state.firingGain));
        }
    }

    void turbine(const EngineState& state, double breeze, double hiss, double movingAir,
                 std::array<StereoFrame, layerCount>& out) {
        const auto& c = tone[0];
        const auto& e = tone[1];
        const auto& i = tone[2];
        const auto& m = tone[3];
        const auto& t = tone[4];
        const auto& b = tone[5];
        const double spool = unit(smoothedCore / model.coreMaxRpm);
        const double corePhase = advance(core, coreRotation, rate);
        const double gasPhase = advance(generator, coreRotation * 0.83, rate);
        const double freeRotation = rotation * 4.3; // Free turbine upstream of the reduction gear.
        const double freePhase = advance(powerWheel, freeRotation, rate);
        const double compressorOrder = model.compressorBlades;
        const double gasOrder = model.turbineBlades;
        const double compressorHz = coreRotation * compressorOrder;
        const double gasHz = coreRotation * 0.83 * gasOrder;
        const double freeHz = freeRotation * 11.0;
        const double sideband = sine(corePhase);
        const double compressor = (0.70 + (0.08 + 0.24 * t.shape) * sideband + 0.066 * breeze) *
            harmonic(corePhase * compressorOrder + 0.027 * breeze, compressorHz, rate) +
            0.25 * harmonic(corePhase * (compressorOrder - 3.0), coreRotation * (compressorOrder - 3.0), rate) +
            0.14 * harmonic(corePhase * compressorOrder * 2.0, compressorHz * 2.0, rate);
        const double gasRow = 0.74 * harmonic(gasPhase * gasOrder + 0.033 * breeze, gasHz, rate) +
            0.26 * harmonic(gasPhase * 4.0, coreRotation * 3.32, rate);
        const double freeRow = harmonic(freePhase * 11.0, freeHz, rate) * (0.73 + 0.25 * sine(freePhase)) +
            0.21 * harmonic(freePhase * 22.0, freeHz * 2.0, rate);
        const double fluttering = flutter.tick(movingAir);
        const double surgePhase = advance(surge, 6.2 + 8.0 * spool + 23.0 * fluttering, rate);
        const double mixture = std::max(0.0, -fault.fuelBias) * 0.69 + std::max(0.0, fault.fuelBias) * 0.43;
        const double flow = (1.0 + mixture * (0.43 * sine(surgePhase) + 4.7 * fluttering)) *
            (1.0 + fault.fuelBias * (fault.fuelBias < 0.0 ? 0.62 : 0.17));
        const double jetPower = state.burn * state.firingGain * (0.35 + 0.93 * state.fuel) * spool * std::max(0.09, flow);
        const double turbulent = jet.tick(movingAir + 0.42 * breeze + 0.14 * gasRow);
        const double exhaust = 2.35 * jetPower * ((0.77 + 0.76 * e.shape) * turbulent +
            (0.32 + 1.4 * e.amount) * turbineExhaust.tick(turbulent + 0.11 * sine(freePhase)) +
            (1.0 - e.damping) * 0.53 * hiss);
        const double ignition = state.burn * state.firingGain * spool * (0.36 + state.fuel * 0.91) *
            ((0.34 - 0.20 * c.shape) * gasRow + (0.35 + 0.75 * c.amount) *
                banks[0].block.tick((0.65 + 0.68 * c.shape) * movingAir + 0.16 * gasRow));
        const double induction = spool * (0.40 + 0.60 * spool) * i.amount *
            ((0.66 + 0.63 * i.shape) * (breeze + 0.28 * hiss) + 0.75 * air.tick(breeze + 0.11 * gasRow) +
            0.16 * gasRow);
        const double rotor = spool * t.amount * (0.45 + 0.55 * state.fuel) *
            (0.51 * compressor + 0.10 * gasRow + 0.43 * blower.tick(hiss));
        const double starterHz = 265.0 + coreRotation * 2.7;
        const double starterPhase = advance(shaft, starterHz, rate);
        const double gearbox = engineEnvelope * m.amount * (0.15 + 0.23 * m.shape + 0.16 * state.load) *
            (0.40 * freeRow + 0.22 * sine(freePhase) + 0.50 * banks[1].metal.tick(breeze + 0.24 * freeRow) +
             fault.mechanicalWear * (0.19 * freeRow + 0.20 * hiss) * (0.62 + 0.38 * sine(freePhase * 3.0)));
        const double starter = state.starter * (0.04 * sine(starterPhase) + 0.017 * sine(starterPhase * 3.0) + 0.08 * breeze);
        const double low = jetPower * ((0.34 + 0.5 * lug) * sine(freePhase) + 2.9 * turbulent) + 0.07 * spool * sine(corePhase);
        const double body = b.amount * ((0.88 + 0.81 * b.shape) * chassis.tick(low) + 0.38 * shell.tick(low));
        out[0] = centered(0, ignition, 0.04 * gasRow * jetPower);
        out[1] = centered(1, exhaust, breeze * 0.09 * jetPower);
        out[2] = centered(2, induction, hiss * 0.07 * spool);
        out[3] = centered(3, gearbox + starter, 0.10 * gearbox * sine(freePhase));
        out[4] = centered(4, rotor, 0.055 * rotor * sideband);
        out[5] = centered(5, body, 0.04 * shell.z1);
    }

    StereoFrame sample(const EngineState& state) {
        if (state.mode == EngineMode::Off && ignitions.lastCylinder) {
            ignitions = {};
            attempts = 0;
        }
        rotation = interpolate(smoothedRpm, state.rpm, slew) / 60.0;
        coreRotation = interpolate(smoothedCore, state.coreRpm, slew) / 60.0;
        speed = unit(rotation * 60.0 / model.maxRpm);
        engineEnvelope = unit(rotation * 60.0 / (model.idleRpm * 0.62));
        lug = fault.overload * (0.50 + 0.5 * (1.0 - speed)) * (0.65 + 0.35 * state.load);
        const float noise = soundNoise.next();
        const double breeze = wind.tick(noise);
        const double movingAir = fizz.tick(noise);
        const double hiss = movingAir - breeze;
        std::array<StereoFrame, layerCount> out{};
        if (model.kind == EngineKind::Turbine) turbine(state, breeze, hiss, movingAir, out);
        else pistons(state, noise, breeze, hiss, out);
        out[static_cast<std::size_t>(LayerId::Transmission)] = transmission(state);
        const double target = state.mode == EngineMode::Off ? 0.0 : 1.0;
        interpolate(voiceEnvelope, target, slew);
        auto frame = mixer.mix(out);
        frame.left *= static_cast<float>(voiceEnvelope);
        frame.right *= static_cast<float>(voiceEnvelope);
        return frame;
    }
};

class Decimator {
public:
    Decimator() {
        constexpr int center = 15;
        double sum = 0.0;
        for (int i = 0; i < taps; ++i) {
            const int offset = i - center;
            const double window = 0.54 - 0.46 * std::cos(tau * i / (taps - 1));
            if (offset == 0) coefficients_[i] = 0.5;
            else if (offset % 2 != 0) coefficients_[i] = window * std::sin(pi * 0.5 * offset) / (pi * offset);
            sum += coefficients_[i];
        }
        for (auto& coefficient : coefficients_) coefficient /= sum;
    }

    void clear() { history_ = {}; pos_ = 0; }

    void push(StereoFrame frame) {
        history_[pos_] = frame;
        if (++pos_ == taps) pos_ = 0;
    }

    StereoFrame read() const {
        StereoFrame result;
        int index = pos_;
        for (int i = 0; i < taps; ++i) {
            if (coefficients_[i] != 0.0) {
                result.left += history_[index].left * static_cast<float>(coefficients_[i]);
                result.right += history_[index].right * static_cast<float>(coefficients_[i]);
            }
            if (++index == taps) index = 0;
        }
        return result;
    }

private:
    static constexpr int taps = 31;
    std::array<double, taps> coefficients_{};
    std::array<StereoFrame, taps> history_{};
    int pos_ = 0;
};

} // namespace

struct Synthesizer::Impl {
    explicit Impl(int rate) : rate(rate * 2.0), controlPeriod(this->rate / controlRate), listener(rate) {}

    double rate;
    double controlPeriod;
    double controlCountdown = 0.0;
    double tailFrames = 0.0;
    Settings settings;
    SimulationController simulation;
    std::array<Voice, 2> voice;
    Decimator decimator;
    ListenerFilter listener;
    int index = 0;
    double morph = 1.0;
    bool steady = false;

    void updateVoice() {
        const auto& current = voice[index];
        if (!needsNewVoice(current.model, settings.model) || (morph < 1.0 && tailFrames > 0.0)) return;
        const double phase = current.turn / (current.model.strokes / 2.0);
        index = 1 - index;
        voice[index].prepare(settings, simulation.state(), rate, phase);
        morph = tailFrames > 0.0 ? 0.0 : 1.0;
    }

    StereoFrame sample() {
        if (controlCountdown <= 0.0) {
            controlCountdown += controlPeriod;
            const bool pending = needsNewVoice(voice[index].model, settings.model);
            if (!pending) voice[index].model = settings.model;
            if (!steady) {
                auto live = settings;
                live.model = voice[index].model;
                simulation.update(live, 1.0 / controlRate);
            }
            voice[index].tune(settings, false);
            if (morph < 1.0) voice[1 - index].tune(settings, false);
        }
        controlCountdown -= 1.0;
        if (simulation.state().mode != EngineMode::Off) tailFrames = 0.10 * rate;
        if (tailFrames <= 0.0) return {};
        tailFrames -= 1.0;
        auto next = voice[index].sample(simulation.state());
        if (morph < 1.0) {
            const auto old = voice[1 - index].sample(simulation.state());
            const float blend = static_cast<float>(morph * morph * (3.0 - 2.0 * morph));
            next.left = std::lerp(old.left, next.left, blend);
            next.right = std::lerp(old.right, next.right, blend);
            morph = std::min(1.0, morph + 1.0 / (0.037 * rate));
        }
        return next;
    }
};

Synthesizer::Synthesizer(int sampleRate) : impl_(std::make_unique<Impl>(sampleRate)) {
    reset({});
}

Synthesizer::~Synthesizer() = default;

void Synthesizer::reset(const Settings& settings) {
    auto& p = *impl_;
    p.settings = settings;
    p.simulation = {};
    p.decimator.clear();
    p.listener.reset(settings.listener);
    p.index = 0;
    p.morph = 1.0;
    p.controlCountdown = p.tailFrames = 0.0;
    p.steady = false;
    p.voice[0].prepare(settings, p.simulation.state(), p.rate, 0.0);
}

void Synthesizer::apply(const Settings& settings) {
    impl_->listener.configure(settings.listener);
    impl_->settings = settings;
    impl_->steady = false;
}

void Synthesizer::setSteady(const Settings& settings, double rpm) {
    reset(settings);
    auto& p = *impl_;
    p.simulation.setSteady(settings, rpm);
    p.voice[p.index].prepare(settings, p.simulation.state(), p.rate, 0.0);
    p.steady = !p.simulation.state().limiterCut;
    if (!p.steady) {
        p.settings.controls.running = p.settings.controls.hold = true;
        p.settings.controls.holdRpm = rpm;
        p.settings.controls.program = Program::Manual;
    }
}

void Synthesizer::render(std::span<StereoFrame> frames) {
    impl_->updateVoice();
    for (auto& frame : frames) {
        impl_->decimator.push(impl_->sample());
        impl_->decimator.push(impl_->sample());
        frame = impl_->listener.process(impl_->decimator.read());
    }
}

const EngineState& Synthesizer::state() const { return impl_->simulation.state(); }

ShaftPhases Synthesizer::phases() const {
    const auto& voice = impl_->voice[impl_->index];
    return {voice.turn, voice.core, voice.powerWheel};
}

const IgnitionTrace& Synthesizer::ignitions() const { return impl_->voice[impl_->index].ignitions; }

} // namespace esl
