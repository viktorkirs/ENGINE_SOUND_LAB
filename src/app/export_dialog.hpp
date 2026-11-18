#pragma once

#include "app/audio_exporter.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace esl {

class ExportDialog final : public QDialog {
public:
    ExportDialog(const Preset& preset, double rpm, double load, double throttle, QWidget* parent);
    const ExportOptions& options() const { return options_; }
    const QString& directory() const { return directory_; }

private:
    void chooseFolder();
    ExportOptions options_;
    QString directory_;
    QComboBox* mode_ = nullptr;
    QComboBox* rate_ = nullptr;
    QComboBox* bits_ = nullptr;
    QComboBox* channels_ = nullptr;
    QCheckBox* normalize_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QDoubleSpinBox* duration_ = nullptr;
    QDoubleSpinBox* rpm_ = nullptr;
    QDoubleSpinBox* load_ = nullptr;
    QDoubleSpinBox* throttle_ = nullptr;
    QDoubleSpinBox* minRpm_ = nullptr;
    QDoubleSpinBox* maxRpm_ = nullptr;
    QDoubleSpinBox* rpmCount_ = nullptr;
    QDoubleSpinBox* minLoad_ = nullptr;
    QDoubleSpinBox* maxLoad_ = nullptr;
    QDoubleSpinBox* loadCount_ = nullptr;
    QLabel* validation_ = nullptr;
};

} // namespace esl
