#include "app/number_field.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <algorithm>
#include <cmath>

namespace esl {

NumberField::NumberField(double low, double high, int decimals, double step, const QString& suffix, bool logarithmic)
    : edit_(new QDoubleSpinBox(this)), slider_(new QSlider(Qt::Horizontal, this)), logarithmic_(logarithmic) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    slider_->setMinimumWidth(65);
    row->addWidget(slider_, 1);
    row->addWidget(edit_);
    edit_->setDecimals(decimals);
    edit_->setSingleStep(step);
    edit_->setSuffix(suffix);
    edit_->setKeyboardTracking(false);
    edit_->setButtonSymbols(QAbstractSpinBox::NoButtons);
    edit_->setObjectName(QStringLiteral("exactValue"));
    setFocusProxy(edit_);
    setRange(low, high);
    connect(edit_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        syncSlider();
        emit valueChanged(v);
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int position) {
        const double u = double(position) / std::max(1, slider_->maximum());
        const double lo = edit_->minimum(), hi = edit_->maximum();
        const double x = logarithmic_ && lo > 0 ? lo * std::pow(hi / lo, u) : std::lerp(lo, hi, u);
        edit_->setValue(x);
    });
}

void NumberField::syncSlider() {
    const QSignalBlocker guard(slider_);
    const double lo = edit_->minimum(), hi = edit_->maximum(), x = edit_->value();
    const double steps = std::min(10000.0, (hi - lo) * std::pow(10.0, edit_->decimals()));
    slider_->setRange(0, qRound(steps));
    const double u = hi == lo ? 0.0 : logarithmic_ && lo > 0 ? std::log(x / lo) / std::log(hi / lo) : (x - lo) / (hi - lo);
    slider_->setValue(qRound(u * slider_->maximum()));
    slider_->setPageStep(std::max(1, slider_->maximum() / 20));
}

double NumberField::value() const { return edit_->value(); }
void NumberField::setValue(double v) { edit_->setValue(v); }
void NumberField::setRange(double lo, double hi) { edit_->setRange(lo, hi); syncSlider(); }
void NumberField::setMinimum(double lo) { setRange(lo, edit_->maximum()); }
void NumberField::setMaximum(double hi) { setRange(edit_->minimum(), hi); }
void NumberField::setSuffix(const QString& suffix) { edit_->setSuffix(suffix); }

void NumberField::setFieldName(const QString& name) {
    setAccessibleName(name);
    edit_->setAccessibleName(name);
    slider_->setAccessibleName(name);
}

} // namespace esl
