#pragma once

#include "core/dsp.hpp"

#include <QElapsedTimer>
#include <QWidget>

namespace esl {

class EngineDiagram final : public QWidget {
public:
    EngineDiagram();
    void setData(const Model& model, const EngineState& engine, ShaftPhases phases, const IgnitionTrace& ignitions);
    void setSlow(bool enabled);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Model model_;
    EngineState engine_;
    std::array<CylinderPhase, maxCylinders> cylinders_ = cylinderPhases(model_);
    ShaftPhases shown_, live_;
    IgnitionTrace::Cycle displayed_{};
    int activeCylinder_ = 0;
    bool captured_ = false;
    QString caption_;
    QElapsedTimer clock_;
    bool slow_ = true;
};

} // namespace esl
