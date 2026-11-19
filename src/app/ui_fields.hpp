#pragma once

#include "app/number_field.hpp"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>

namespace esl {

inline QLabel* uiLabel(const QString& text, const char* styleName, QWidget* parent = nullptr) {
    auto* label = new QLabel(text, parent);
    if (styleName) label->setObjectName(QLatin1String(styleName));
    return label;
}

inline NumberField* numberField(double low, double high, int decimals, double step, const QString& suffix = {}, bool logarithmic = false) {
    return new NumberField(low, high, decimals, step, suffix, logarithmic);
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
    if (auto* number = qobject_cast<NumberField*>(field)) number->setFieldName(name);
    form->addRow(label, field);
}

} // namespace esl
