#pragma once

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>

namespace esl {

inline QLabel* uiLabel(const QString& text, const char* styleName, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    if (styleName) label->setObjectName(QLatin1String(styleName));
    return label;
}

inline QDoubleSpinBox* numberField(double low, double high, int decimals, double step, const QString& suffix = {}) {
    auto* field = new QDoubleSpinBox;
    field->setDecimals(decimals);
    field->setRange(low, high);
    field->setSingleStep(step);
    field->setSuffix(suffix);
    field->setKeyboardTracking(false);
    field->setMinimumWidth(80);
    return field;
}

inline QComboBox* choices(const QStringList& names) {
    auto* field = new QComboBox;
    field->addItems(names);
    return field;
}

inline void formRow(QFormLayout* form, const QString& name, QWidget* field) {
    auto* label = new QLabel(name);
    label->setBuddy(field);
    field->setAccessibleName(name);
    form->addRow(label, field);
}

} // namespace esl
