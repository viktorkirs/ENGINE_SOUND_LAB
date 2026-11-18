#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esl {

inline constexpr int maxCylinders = 24;
inline constexpr double pi = 3.14159265358979323846;

enum class EngineKind { Diesel, Gasoline, Turbine };
enum class Layout { Inline, V, Boxer, Radial };
enum class Boost { None, Turbocharger, Supercharger };
enum class LayerId { Combustion, Exhaust, Intake, Mechanical, Turbo, Body, Count };
enum class Program { Manual, Idle, SlowRamp, FastRamp, FullLoad, Lift, EngineBrake, StartStop, AutoTest };

inline constexpr std::size_t layerCount = static_cast<std::size_t>(LayerId::Count);
using FiringOrder = std::array<int, maxCylinders>;

struct Model {
    EngineKind kind = EngineKind::Diesel;
    Layout layout = Layout::V;
    int cylinders = 12;
    int strokes = 4;
    double liters = 38.88;
    double idleRpm = 650.0;
    double maxRpm = 2200.0;
    double bankAngle = 60.0;
    FiringOrder firingOrder{}; // Zero-filled selects the layout's default schedule.
    double irregularity = 0.18;
    double inertia = 19.0; // Effective test-stand inertia in kg m².
    double powerKw = 618.0;
    double torqueNm = 3500.0;
    double loss = 0.36;
    int exhaustTracts = 2;
    Boost boost = Boost::Supercharger;
    double coreMaxRpm = 28000.0;
    double coreIdle = 0.54; // Fraction of the gas generator's maximum speed.
    double coreLag = 1.25; // Spin-up time constant in seconds.
    int compressorBlades = 18;
    int turbineBlades = 29;
    std::uint32_t seed = 1973;
};

struct Layer {
    double gain;
    double resonance;
    double cutoff;
    double shape;
    double amount;
    double damping;
    double drive;
    bool mute = false;
    bool solo = false;
};

using Layers = std::array<Layer, layerCount>;
Layers defaultLayers();

struct Controls {
    bool running = false;
    double throttle = 0.0;
    double load = 0.18;
    bool hold = false;
    double holdRpm = 1000.0;
    int gear = 0;
    bool brake = false;
    double master = 0.76;
    Program program = Program::Manual;
    std::uint32_t programToken = 0;
};

struct Faults {
    int cylinder = 1;
    double sparkMiss = 0.0;
    double fuelBias = 0.0; // Negative starves the engine, positive overfuels it.
    double injectorWear = 0.0;
    double overload = 0.0;
    double mechanicalWear = 0.0;

    void approach(const Faults& target, double fraction);
};

struct Settings {
    Model model;
    Layers layers = defaultLayers();
    Controls controls;
    Faults faults;
};

struct CylinderPhase {
    double turn = 0.0;
    int cylinder = 0; // One-based cylinder number.
    int bank = 0;
    double runnerMeters = 0.0;
    double weight = 1.0;
};

bool validCylinderCount(Layout layout, int cylinders);
FiringOrder defaultFiringOrder(const Model& model);
bool validFiringOrder(const Model& model, const FiringOrder& order);
std::array<CylinderPhase, maxCylinders> cylinderPhases(const Model& model);
bool needsNewVoice(const Model& oldModel, const Model& newModel);

} // namespace esl
