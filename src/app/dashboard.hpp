#pragma once

#include "app/audio_output.hpp"

#include <QWidget>

namespace esl {

QString engineModeName(EngineMode mode);
QString dashboardStyle();

class Tachometer final : public QWidget {
public:
    explicit Tachometer(QWidget* parent = nullptr);
    void setData(const Model& model, const EngineState& engine, bool hold, double holdRpm, const RevLimiter& limiter = {});

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Model model_;
    EngineState engine_;
    bool hold_ = false;
    double holdRpm_ = 0;
    RevLimiter limiter_;
};

class SignalView final : public QWidget {
public:
    SignalView(bool spectrum, QWidget* parent = nullptr);
    void setSignal(const AudioSnapshot& snapshot);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    bool spectrum_;
    std::array<float, scopeFrames> signal_{};
    std::array<float, 68> bands_{};
};

} // namespace esl
