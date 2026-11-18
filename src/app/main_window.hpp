#pragma once

#include "app/audio_exporter.hpp"
#include "app/audio_output.hpp"

#include <QMainWindow>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QToolButton;
template <typename Result> class QFutureWatcher;

namespace esl {

class SignalView;
class Tachometer;

class MainWindow final : public QMainWindow {
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* makeHeader();
    QWidget* makeTransport();
    QWidget* makeModelPanel();
    QWidget* makeTelemetryPanel();
    QWidget* makeMixer();
    QWidget* makeFooter();
    void loadBuiltins();
    void selectPreset(int index);
    void openPreset();
    void storePreset();
    void syncModel();
    void syncBehavior();
    void syncMixer();
    void syncLayer();
    QWidget* makeFaultPanel();
    void syncFaults();
    void updateRpmLimits();
    void updateFiringOrder();
    void submitEdit();
    void sendAudioSettings();
    void leaveProgram();
    void refresh();
    void exportWav();
    void finishExport();

    AudioOutput output_;
    AudioSnapshot meter_;
    Preset preset_;
    std::vector<Preset> presets_;
    bool syncing_ = false;
    bool running_ = false;
    Program activeProgram_ = Program::Manual;
    std::uint32_t programToken_ = 0;
    int selectedLayer_ = 1;

    QComboBox* presetList_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* device_ = nullptr;
    QLabel* liveRpm_ = nullptr;
    QLabel* peak_ = nullptr;
    QPushButton* run_ = nullptr;
    QSlider* throttle_ = nullptr;
    QSlider* load_ = nullptr;
    QSlider* master_ = nullptr;
    QCheckBox* hold_ = nullptr;
    QDoubleSpinBox* holdRpm_ = nullptr;
    QComboBox* gear_ = nullptr;
    QCheckBox* brake_ = nullptr;
    QComboBox* scenario_ = nullptr;
    QPushButton* scenarioRun_ = nullptr;
    QProgressBar* scenarioProgress_ = nullptr;
    Tachometer* tach_ = nullptr;
    SignalView* scope_ = nullptr;
    SignalView* spectrum_ = nullptr;

    QFormLayout* modelForm_ = nullptr;
    QFormLayout* dynamicsForm_ = nullptr;
    QComboBox* kind_ = nullptr;
    QComboBox* layout_ = nullptr;
    QComboBox* cylinders_ = nullptr;
    QComboBox* strokes_ = nullptr;
    QComboBox* boost_ = nullptr;
    QComboBox* tracts_ = nullptr;
    QDoubleSpinBox* liters_ = nullptr;
    QDoubleSpinBox* bankAngle_ = nullptr;
    QDoubleSpinBox* coreMax_ = nullptr;
    QDoubleSpinBox* coreIdle_ = nullptr;
    QDoubleSpinBox* coreLag_ = nullptr;
    QDoubleSpinBox* compressorBlades_ = nullptr;
    QDoubleSpinBox* turbineBlades_ = nullptr;
    QWidget* orderRow_ = nullptr;
    QLineEdit* order_ = nullptr;
    QLabel* orderError_ = nullptr;
    QDoubleSpinBox* idleRpm_ = nullptr;
    QDoubleSpinBox* maxRpm_ = nullptr;
    QDoubleSpinBox* inertia_ = nullptr;
    QDoubleSpinBox* power_ = nullptr;
    QDoubleSpinBox* torque_ = nullptr;
    QDoubleSpinBox* loss_ = nullptr;
    QDoubleSpinBox* irregularity_ = nullptr;
    QDoubleSpinBox* seed_ = nullptr;
    QWidget* faultCylinderRow_ = nullptr;
    QComboBox* faultCylinder_ = nullptr;
    QWidget* sparkRow_ = nullptr;
    QWidget* injectorRow_ = nullptr;
    QSlider* spark_ = nullptr;
    QSlider* fuel_ = nullptr;
    QSlider* injector_ = nullptr;
    QSlider* overload_ = nullptr;
    QSlider* wear_ = nullptr;

    std::array<QToolButton*, layerCount> layerName_{};
    std::array<QToolButton*, layerCount> layerMute_{};
    std::array<QToolButton*, layerCount> layerSolo_{};
    std::array<QSlider*, layerCount> layerGain_{};
    QLabel* layerTitle_ = nullptr;
    QLabel* layerHint_ = nullptr;
    QFormLayout* layerForm_ = nullptr;
    QDoubleSpinBox* resonance_ = nullptr;
    QDoubleSpinBox* cutoff_ = nullptr;
    QDoubleSpinBox* shape_ = nullptr;
    QDoubleSpinBox* amount_ = nullptr;
    QDoubleSpinBox* damping_ = nullptr;
    QDoubleSpinBox* drive_ = nullptr;

    QLabel* message_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* cancelExport_ = nullptr;
    QPushButton* openFolder_ = nullptr;
    QProgressBar* renderProgress_ = nullptr;
    QFutureWatcher<ExportResult>* job_ = nullptr;
    std::atomic_bool cancel_{false};
    QString exportedPath_;
};

} // namespace esl
