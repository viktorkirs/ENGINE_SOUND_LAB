#pragma once

#include "app/audio_exporter.hpp"
#include "app/audio_output.hpp"

#include <QMainWindow>

#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QToolButton;
template <typename Result> class QFutureWatcher;

namespace esl {

class NumberField;
class EngineDiagram;

class SignalView;
class Tachometer;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const QString& initialPreset = {});

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* makeHeader();
    QWidget* makeTransport();
    QWidget* makeModelPanel();
    QWidget* makeTelemetryPanel();
    QWidget* makeMixer();
    QWidget* makeFooter();
    void loadBuiltins(const QString& initialPreset);
    void selectPreset(int index);
    void updatePresetButton();
    void openPreset();
    void storePreset();
    void syncModel();
    void syncGeometry();
    void syncBehavior();
    void syncListener();
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
    int currentPreset_ = -1;
    bool syncing_ = false;
    bool running_ = false;
    Program activeProgram_ = Program::Manual;
    std::uint32_t programToken_ = 0;
    int selectedLayer_ = 1;

    QPushButton* presetButton_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* device_ = nullptr;
    QLabel* liveRpm_ = nullptr;
    QLabel* peak_ = nullptr;
    QPushButton* run_ = nullptr;
    QSlider* throttle_ = nullptr;
    QSlider* load_ = nullptr;
    QSlider* master_ = nullptr;
    QComboBox* listenerPosition_ = nullptr;
    NumberField* distance_ = nullptr;
    NumberField* insulation_ = nullptr;
    QLabel* acousticLabel_ = nullptr;
    QCheckBox* hold_ = nullptr;
    NumberField* holdRpm_ = nullptr;
    QComboBox* gear_ = nullptr;
    QCheckBox* brake_ = nullptr;
    QComboBox* scenario_ = nullptr;
    QPushButton* scenarioRun_ = nullptr;
    QProgressBar* scenarioProgress_ = nullptr;
    Tachometer* tach_ = nullptr;
    SignalView* scope_ = nullptr;
    SignalView* spectrum_ = nullptr;
    EngineDiagram* diagram_ = nullptr;

    QFormLayout* modelForm_ = nullptr;
    QFormLayout* dynamicsForm_ = nullptr;
    QComboBox* kind_ = nullptr;
    QComboBox* layout_ = nullptr;
    QComboBox* cylinders_ = nullptr;
    QComboBox* strokes_ = nullptr;
    QComboBox* boost_ = nullptr;
    QComboBox* tracts_ = nullptr;
    QComboBox* recording_ = nullptr;
    QWidget* cylinderSelector_ = nullptr;
    QWidget* tractSelector_ = nullptr;
    NumberField* liters_ = nullptr;
    NumberField* bore_ = nullptr;
    NumberField* strokeMm_ = nullptr;
    NumberField* compression_ = nullptr;
    NumberField* bankAngle_ = nullptr;
    NumberField* coreMax_ = nullptr;
    NumberField* coreIdle_ = nullptr;
    NumberField* coreLag_ = nullptr;
    NumberField* compressorBlades_ = nullptr;
    NumberField* turbineBlades_ = nullptr;
    QWidget* orderRow_ = nullptr;
    QLineEdit* order_ = nullptr;
    QLabel* orderError_ = nullptr;
    NumberField* idleRpm_ = nullptr;
    NumberField* maxRpm_ = nullptr;
    QCheckBox* limiterEnabled_ = nullptr;
    NumberField* cutRpm_ = nullptr;
    NumberField* resumeRpm_ = nullptr;
    NumberField* inertia_ = nullptr;
    NumberField* power_ = nullptr;
    NumberField* torque_ = nullptr;
    NumberField* loss_ = nullptr;
    NumberField* overrunDrag_ = nullptr;
    NumberField* irregularity_ = nullptr;
    NumberField* seed_ = nullptr;
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
    QWidget* recordingMixRow_ = nullptr;
    std::array<QToolButton*, layerCount> layerMute_{};
    std::array<QToolButton*, layerCount> layerSolo_{};
    std::array<QSlider*, layerCount> layerGain_{};
    QLabel* layerTitle_ = nullptr;
    QLabel* layerHint_ = nullptr;
    QFormLayout* layerForm_ = nullptr;
    NumberField* resonance_ = nullptr;
    NumberField* cutoff_ = nullptr;
    NumberField* shape_ = nullptr;
    NumberField* amount_ = nullptr;
    NumberField* damping_ = nullptr;
    NumberField* drive_ = nullptr;

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
