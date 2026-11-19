#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QSlider;

namespace esl {

class NumberField final : public QWidget {
    Q_OBJECT
public:
    NumberField(double low, double high, int decimals, double step, const QString& suffix, bool logarithmic);
    double value() const;
    void setValue(double value);
    void setRange(double low, double high);
    void setMinimum(double low);
    void setMaximum(double high);
    void setSuffix(const QString& suffix);
    void setFieldName(const QString& name);

signals:
    void valueChanged(double value);

private:
    void syncSlider();
    QDoubleSpinBox* edit_;
    QSlider* slider_;
    bool logarithmic_;
};

} // namespace esl
