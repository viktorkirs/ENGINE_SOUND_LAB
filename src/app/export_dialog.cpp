#include "app/export_dialog.hpp"

#include "app/ui_fields.hpp"

#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <algorithm>

namespace esl {
namespace {

QFormLayout* editPage(QStackedWidget* pages) {
    auto* page = new QWidget;
    pages->addWidget(page);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(0, 5, 0, 2);
    form->setVerticalSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

} // namespace

ExportDialog::ExportDialog(const Preset& preset, double rpm, double load, double throttle, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Экспорт WAV — %1").arg(preset.name));
    setMinimumWidth(460);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 17, 20, 17);
    root->setSpacing(10);
    root->addWidget(uiLabel(QStringLiteral("Экспорт WAV"), "section"));
    auto* common = new QFormLayout;
    common->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mode_ = choices({QStringLiteral("Один звук"), QStringLiteral("RPM Sound Pack"), QStringLiteral("Сетка RPM × нагрузка")});
    formRow(common, QStringLiteral("Режим"), mode_);
    auto* format = new QWidget;
    auto* formatRow = new QHBoxLayout(format);
    formatRow->setContentsMargins(0, 0, 0, 0);
    rate_ = choices({QStringLiteral("48 000 Гц"), QStringLiteral("44 100 Гц")});
    bits_ = choices({QStringLiteral("PCM 24 бит"), QStringLiteral("PCM 16 бит")});
    channels_ = choices({QStringLiteral("Стерео"), QStringLiteral("Моно")});
    rate_->setAccessibleName(QStringLiteral("Частота дискретизации"));
    bits_->setAccessibleName(QStringLiteral("Разрядность"));
    channels_->setAccessibleName(QStringLiteral("Каналы"));
    formatRow->addWidget(rate_, 1);
    formatRow->addWidget(bits_, 1);
    formatRow->addWidget(channels_, 1);
    formRow(common, QStringLiteral("Формат"), format);
    duration_ = numberField(1, 15, 1, 0.5, QStringLiteral(" с"));
    duration_->setValue(4.0);
    formRow(common, QStringLiteral("Длительность"), duration_);
    auto* durationLabel = qobject_cast<QLabel*>(common->labelForField(duration_));
    normalize_ = new QCheckBox(QStringLiteral("Пиковая нормализация до −1 дБFS"));
    normalize_->setChecked(true);
    normalize_->setToolTip(QStringLiteral("Один коэффициент для всей папки. Разница громкости между точками сохраняется."));
    common->addRow(QString(), normalize_);
    root->addLayout(common);

    auto* pages = new QStackedWidget;
    const auto& m = preset.settings.model;
    auto* single = editPage(pages);
    rpm_ = numberField(m.idleRpm, m.maxRpm, 1, 25, QStringLiteral(" об/мин"));
    load_ = numberField(0, 100, 1, 5, QStringLiteral(" %"));
    throttle_ = numberField(0, 100, 1, 5, QStringLiteral(" %"));
    rpm_->setValue(std::clamp(rpm, m.idleRpm, m.maxRpm));
    load_->setValue(load * 100);
    throttle_->setValue(throttle * 100);
    formRow(single, QStringLiteral("Обороты"), rpm_);
    formRow(single, QStringLiteral("Нагрузка"), load_);
    formRow(single, QStringLiteral("Газ"), throttle_);
    loop_ = new QCheckBox(QStringLiteral("Бесшовный цикл"));
    loop_->setChecked(true);
    single->addRow(QString(), loop_);

    auto* pack = editPage(pages);
    auto* explanation = uiLabel(QStringLiteral("Девять файлов WAV. Пять циклов и четыре перехода — запуск, разгон, сброс газа и выключение."), "hint");
    explanation->setWordWrap(true);
    pack->addRow(explanation);

    auto* grid = editPage(pages);
    minRpm_ = numberField(m.idleRpm, m.maxRpm, 1, 25, QStringLiteral(" об/мин"));
    maxRpm_ = numberField(m.idleRpm, m.maxRpm, 1, 25, QStringLiteral(" об/мин"));
    minRpm_->setValue(m.idleRpm);
    maxRpm_->setValue(m.maxRpm);
    rpmCount_ = numberField(1, 16, 0, 1);
    rpmCount_->setValue(4);
    minLoad_ = numberField(0, 100, 1, 5, QStringLiteral(" %"));
    maxLoad_ = numberField(0, 100, 1, 5, QStringLiteral(" %"));
    minLoad_->setValue(20);
    maxLoad_->setValue(80);
    loadCount_ = numberField(1, 10, 0, 1);
    loadCount_->setValue(2);
    auto bounds = [&](QDoubleSpinBox* from, QDoubleSpinBox* to) {
        auto* w = new QWidget;
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(from, 1);
        row->addWidget(new QLabel(QStringLiteral("–")));
        row->addWidget(to, 1);
        return w;
    };
    minRpm_->setAccessibleName(QStringLiteral("Начальные обороты сетки"));
    maxRpm_->setAccessibleName(QStringLiteral("Конечные обороты сетки"));
    minLoad_->setAccessibleName(QStringLiteral("Минимальная нагрузка сетки"));
    maxLoad_->setAccessibleName(QStringLiteral("Максимальная нагрузка сетки"));
    formRow(grid, QStringLiteral("Диапазон оборотов"), bounds(minRpm_, maxRpm_));
    formRow(grid, QStringLiteral("Точек RPM"), rpmCount_);
    formRow(grid, QStringLiteral("Диапазон нагрузки"), bounds(minLoad_, maxLoad_));
    formRow(grid, QStringLiteral("Точек нагрузки"), loadCount_);
    auto* count = uiLabel(QString(), "numeric");
    grid->addRow(QString(), count);
    auto updateCount = [this, count] { count->setText(QStringLiteral("%1 циклов WAV").arg(qRound(rpmCount_->value() * loadCount_->value()))); };
    connect(rpmCount_, &QDoubleSpinBox::valueChanged, this, updateCount);
    connect(loadCount_, &QDoubleSpinBox::valueChanged, this, updateCount);
    updateCount();
    connect(mode_, &QComboBox::currentIndexChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this, durationLabel](int mode) {
        if (durationLabel) durationLabel->setText(mode == 0 ? QStringLiteral("Длительность") : QStringLiteral("Длительность цикла"));
        validation_->clear();
    });
    root->addWidget(pages);
    validation_ = uiLabel(QString(), "validation");
    validation_->setWordWrap(true);
    root->addWidget(validation_);
    auto* actions = new QHBoxLayout;
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Отмена"));
    auto* go = new QPushButton(QStringLiteral("Выбрать папку…"));
    go->setObjectName(QStringLiteral("primary"));
    go->setDefault(true);
    actions->addWidget(cancel);
    actions->addWidget(go);
    root->addLayout(actions);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(go, &QPushButton::clicked, this, &ExportDialog::chooseFolder);
}

void ExportDialog::chooseFolder() {
    options_.mode = static_cast<ExportMode>(mode_->currentIndex());
    options_.sampleRate = rate_->currentIndex() == 0 ? 48000 : 44100;
    options_.bits = bits_->currentIndex() == 0 ? 24 : 16;
    options_.channels = channels_->currentIndex() == 0 ? 2 : 1;
    options_.normalize = normalize_->isChecked();
    options_.seconds = duration_->value();
    options_.singleLoop = loop_->isChecked();
    options_.rpm = rpm_->value();
    options_.load = load_->value() * 0.01;
    options_.throttle = throttle_->value() * 0.01;
    options_.rpmMin = minRpm_->value();
    options_.rpmMax = maxRpm_->value();
    options_.rpmCount = qRound(rpmCount_->value());
    options_.loadMin = minLoad_->value() * 0.01;
    options_.loadMax = maxLoad_->value() * 0.01;
    options_.loadCount = qRound(loadCount_->value());
    if (options_.mode == ExportMode::Grid &&
        (options_.rpmMin > options_.rpmMax || options_.loadMin > options_.loadMax ||
         (options_.rpmCount > 1 && options_.rpmMin == options_.rpmMax) ||
         (options_.loadCount > 1 && options_.loadMin == options_.loadMax))) {
        validation_->setText(QStringLiteral("Укажите возрастающие диапазоны для осей с несколькими точками."));
        return;
    }
    validation_->clear();
    directory_ = QFileDialog::getExistingDirectory(this, QStringLiteral("Папка для набора WAV"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (!directory_.isEmpty()) accept();
}

} // namespace esl
