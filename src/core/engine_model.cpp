#include "core/engine_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace esl {

Layers defaultLayers() {
    return {{
        {0.52, 650.0, 4400.0, 0.44, 0.52, 0.45, 0.13},
        {1.00, 115.0, 2200.0, 0.62, 0.72, 0.52, 0.19},
        {0.46, 280.0, 4400.0, 0.53, 0.56, 0.52, 0.04},
        {0.37, 840.0, 5600.0, 0.45, 0.55, 0.54, 0.03},
        {0.32, 2600.0, 8000.0, 0.48, 0.52, 0.46, 0.02},
        {0.56, 75.0, 1100.0, 0.65, 0.69, 0.63, 0.07},
        {1.00, 125.0, 13000.0, 0.65, 0.00, 0.65, 0.00},
        {0.00, 610.0, 3900.0, 0.62, 0.38, 0.48, 0.04}
    }};
}

double defaultDistance(ListeningPosition position) {
    switch (position) {
    case ListeningPosition::Near: return 2.0;
    case ListeningPosition::ThirdPerson: return 8.0;
    case ListeningPosition::Distant: return 45.0;
    default: return 1.0;
    }
}

RevLimiter defaultRevLimiter(const Model& model) {
    const double span = model.maxRpm - model.idleRpm;
    return {false, std::round(model.idleRpm + 0.95 * span), std::round(model.idleRpm + 0.91 * span)};
}

double pistonStrokeMm(const Model& model) {
    if (model.strokeMm > 0.0) return model.strokeMm;
    return std::cbrt(4.0e6 * model.liters / (pi * model.cylinders));
}

double cylinderBoreMm(const Model& model) {
    return std::sqrt(4.0e6 * model.liters / (pi * model.cylinders * pistonStrokeMm(model)));
}

double displacementLiters(int cylinders, double boreMm, double strokeMm) {
    return cylinders * pi * boreMm * boreMm * strokeMm / 4.0e6;
}

double compressionRatio(const Model& model) {
    return model.compression > 0.0 ? model.compression : (model.kind == EngineKind::Diesel ? 16.0 : 10.0);
}

void Faults::approach(const Faults& target, double fraction) {
    cylinder = target.cylinder;
    sparkMiss = std::lerp(sparkMiss, target.sparkMiss, fraction);
    fuelBias = std::lerp(fuelBias, target.fuelBias, fraction);
    injectorWear = std::lerp(injectorWear, target.injectorWear, fraction);
    overload = std::lerp(overload, target.overload, fraction);
    mechanicalWear = std::lerp(mechanicalWear, target.mechanicalWear, fraction);
}

bool validCylinderCount(Layout layout, int cylinders) {
    if (cylinders < 1 || cylinders > maxCylinders) return false;
    if (layout != Layout::Radial) return true;
    return (cylinders >= 3 && cylinders % 2 == 1) || (cylinders >= 6 && cylinders % 4 == 2);
}

FiringOrder defaultFiringOrder(const Model& model) {
    FiringOrder order{};
    const int n = model.cylinders;
    if (!validCylinderCount(model.layout, n)) return order;

    if (model.layout == Layout::Radial) {
        // Four-stroke twin rows have staggered cylinders and crank throws 180° apart.
        const int step = model.strokes == 2 ? 1 : (n % 2 == 0 ? n / 2 + 2 : 2);
        for (int position = 0; position < n; ++position) {
            order[position] = position * step % n + 1;
        }
        return order;
    }

    std::iota(order.begin(), order.begin() + n, 1);
    if (model.layout == Layout::Inline) {
        if (n == 3) order = {1, 3, 2};
        if (n == 4) order = {1, 3, 4, 2};
        if (n == 6) order = {1, 5, 3, 6, 2, 4};
    } else if (model.layout == Layout::V) {
        if (n == 6) order = {1, 4, 5, 2, 3, 6};
        if (n == 8) order = {1, 8, 7, 2, 6, 5, 4, 3};
        if (n == 12) order = {1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6};
    } else if (model.layout == Layout::Boxer) {
        if (n == 4) order = {1, 4, 2, 3};
        if (n == 6) order = {1, 4, 5, 2, 3, 6};
    }
    return order;
}

bool validFiringOrder(const Model& model, const FiringOrder& order) {
    if (!validCylinderCount(model.layout, model.cylinders)) return false;
    if (std::all_of(order.begin(), order.end(), [](int value) { return value == 0; })) return true;

    std::array<bool, maxCylinders> used{};
    const int n = model.cylinders;
    for (int i = 0; i < n; ++i) {
        const int cylinder = order[i];
        if (cylinder < 1 || cylinder > n || used[cylinder - 1]) return false;
        used[cylinder - 1] = true;
    }
    if (std::any_of(order.begin() + n, order.end(), [](int value) { return value != 0; })) return false;
    if (model.layout != Layout::Radial) return true;

    const auto radial = defaultFiringOrder(model);
    const int offset = static_cast<int>(std::find(radial.begin(), radial.begin() + n, order[0]) - radial.begin());
    for (int i = 0; i < n; ++i) {
        if (order[i] != radial[(i + offset) % n]) return false;
    }
    return true;
}

std::array<CylinderPhase, maxCylinders> cylinderPhases(const Model& model) {
    std::array<CylinderPhase, maxCylinders> result{};
    const int n = model.cylinders;
    if (!validCylinderCount(model.layout, n)) return result;
    const auto order = model.firingOrder[0] && validFiringOrder(model, model.firingOrder)
        ? model.firingOrder : defaultFiringOrder(model);
    const double turns = model.strokes / 2.0;
    const double spacing = turns / n;

    for (int i = 0; i < n; ++i) {
        const int cylinder = order[i];
        const int parity = (cylinder - 1) % 2;
        double phase = i * spacing;
        int bank = parity;
        double runner = 0.48;
        double weight = 1.0;
        switch (model.layout) {
        case Layout::Inline:
            bank = cylinder > (n + 1) / 2;
            runner += 0.065 * ((cylinder - 1) % ((n + 1) / 2));
            break;
        case Layout::V: {
            const double evenBankAngle = n == 8 ? 90.0 : std::clamp(720.0 / n, 60.0, 120.0);
            const double offset = std::clamp((model.bankAngle - evenBankAngle) / 360.0,
                                             -0.31 * spacing, 0.31 * spacing);
            phase += (bank == 0 ? -0.5 : 0.5) * offset;
            runner += 0.07 * ((cylinder - 1) / 2) + bank * model.bankAngle * 0.002;
            weight += (bank == 0 ? 1.0 : -1.0) * 0.08 * model.bankAngle / 120.0;
            break;
        }
        case Layout::Boxer:
            runner = 0.84 + 0.09 * ((cylinder - 1) / 2) + bank * 0.16;
            weight = bank == 0 ? 1.08 : 0.92;
            break;
        case Layout::Radial: {
            const double azimuth = 2.0 * pi * (cylinder - 1) / n;
            if (n % 2 != 0) bank = std::sin(azimuth) < 0.0;
            runner = 0.68 + 0.19 * (1.0 + std::cos(azimuth)) + (n % 2 == 0 ? bank * 0.16 : 0.0);
            weight = 1.0 + 0.13 * std::cos(azimuth);
            break;
        }
        }
        // Fixed, cyclic imbalance preserves the phase of repeatable rendered loops.
        if (model.layout != Layout::Radial) {
            phase += spacing * 0.115 * model.irregularity * std::sin(cylinder * 2.39996323);
        }
        if (phase < 0.0) phase += turns;
        result[i] = {phase, cylinder, bank, runner, weight};
    }
    return result;
}

bool needsNewVoice(const Model& a, const Model& b) {
    return a.kind != b.kind || a.layout != b.layout || a.cylinders != b.cylinders
        || a.strokes != b.strokes || a.bankAngle != b.bankAngle || a.firingOrder != b.firingOrder
        || a.exhaustTracts != b.exhaustTracts || a.boost != b.boost || a.seed != b.seed || a.irregularity != b.irregularity
        || a.compressorBlades != b.compressorBlades || a.turbineBlades != b.turbineBlades
        || a.recording != b.recording;
}

} // namespace esl
