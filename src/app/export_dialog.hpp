#pragma once

#include "app/audio_exporter.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;

namespace esl {

class NumberField;

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
    NumberField* duration_ = nullptr;
    NumberField* rpm_ = nullptr;
    NumberField* load_ = nullptr;
    NumberField* throttle_ = nullptr;
    NumberField* minRpm_ = nullptr;
    NumberField* maxRpm_ = nullptr;
    NumberField* rpmCount_ = nullptr;
    NumberField* minLoad_ = nullptr;
    NumberField* maxLoad_ = nullptr;
    NumberField* loadCount_ = nullptr;
    QLabel* validation_ = nullptr;
};

} // namespace esl
