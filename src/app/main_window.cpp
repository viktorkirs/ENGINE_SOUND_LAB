#include "app/main_window.hpp"

#include "app/dashboard.hpp"
#include "app/export_dialog.hpp"
#include "app/engine_diagram.hpp"
#include "app/recording_bank.hpp"
#include "app/preset_library.hpp"
#include "app/ui_fields.hpp"

#include <QApplication>
#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace esl {
namespace {

const QString rpmUnit = QStringLiteral(" об/мин");
const QString hzUnit = QStringLiteral(" Гц");
const QString percent = QStringLiteral(" %");

const std::array layerNames{QStringLiteral("Сгорание"), QStringLiteral("Выхлоп"), QStringLiteral("Впуск"),
    QStringLiteral("Механика"), QStringLiteral("Наддув"), QStringLiteral("Корпус"), QStringLiteral("Запись"), QStringLiteral("Трансмиссия")};
const std::array layerColors{QStringLiteral("#efa56f"), QStringLiteral("#7fe1bc"), QStringLiteral("#82b5eb"),
    QStringLiteral("#a0a8ef"), QStringLiteral("#e4b793"), QStringLiteral("#b6b9c5"), QStringLiteral("#dfa4bc"), QStringLiteral("#9fd6d0")};
static_assert(layerNames.size() == layerCount && layerColors.size() == layerCount);

constexpr int silentFader = -601;
constexpr int maxLayerFader = 36;

int gainToFader(double gain) {
    if (gain <= 0) return silentFader;
    if (gain >= 1.5) return maxLayerFader;
    return std::max(silentFader + 1, qRound(200.0 * std::log10(gain)));
}

double faderToGain(int step) {
    if (step == silentFader) return 0.0;
    return std::min(1.5, std::pow(10.0, step / 200.0));
}

QString faderText(int step) {
    return step == silentFader ? QStringLiteral("−∞") : QString::number(20 * std::log10(faderToGain(step)), 'f', 1);
}

QFrame* panel(const char* name = "panel") {
    auto* frame = new QFrame;
    frame->setObjectName(QLatin1String(name));
    return frame;
}

QFormLayout* formFor(QWidget* widget) {
    auto* form = new QFormLayout(widget);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(7);
    form->setHorizontalSpacing(7);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return form;
}

QScrollArea* scrollPane(QWidget* content) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    area->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    return area;
}

QSlider* slider(int high, const QString& name) {
    auto* control = new QSlider(Qt::Horizontal);
    control->setRange(0, high);
    control->setSingleStep(1);
    control->setPageStep(5);
    control->setAccessibleName(name);
    return control;
}

QWidget* indexedSlider(QComboBox* choices, const QString& name) {
    auto* group = new QWidget;
    auto* row = new QHBoxLayout(group);
    row->setContentsMargins(0, 0, 0, 0);
    auto* index = slider(0, name);
    index->setMinimumWidth(65);
    row->addWidget(index, 1);
    row->addWidget(choices);
    choices->setAccessibleName(name);
    group->setFocusProxy(choices);
    auto sync = [index, choices] {
        const QSignalBlocker guard(index);
        index->setRange(0, std::max(0, choices->count() - 1));
        index->setValue(choices->currentIndex());
    };
    QObject::connect(choices, &QComboBox::currentIndexChanged, index, sync);
    QObject::connect(choices->model(), &QAbstractItemModel::rowsInserted, index, sync);
    QObject::connect(choices->model(), &QAbstractItemModel::rowsRemoved, index, sync);
    QObject::connect(index, &QSlider::valueChanged, choices, &QComboBox::setCurrentIndex);
    sync();
    return group;
}

QWidget* controlStrip(const QString& name, QSlider*& field, int low = 0, bool decibels = false) {
    auto* group = new QWidget;
    auto* stack = new QVBoxLayout(group);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(2);
    auto* labelRow = new QHBoxLayout;
    labelRow->addWidget(uiLabel(name, "caption"));
    auto* value = uiLabel(decibels ? QStringLiteral("0.0 дБ") : QStringLiteral("0%"), "numeric");
    labelRow->addStretch();
    labelRow->addWidget(value);
    stack->addLayout(labelRow);
    field = slider(decibels ? 0 : 100, name);
    field->setMinimum(low);
    stack->addWidget(field);
    QObject::connect(field, &QSlider::valueChanged, value, [value, low, decibels](int v) {
        if (decibels) { value->setText(faderText(v) + QStringLiteral(" дБ")); return; }
        const auto sign = low < 0 && v > 0 ? QStringLiteral("+") : QString();
        value->setText(sign + QString::number(v) + QLatin1Char('%'));
    });
    return group;
}

QPixmap mark() {
    QPixmap logo(80, 80);
    logo.setDevicePixelRatio(2);
    logo.fill(Qt::transparent);
    QPainter p(&logo);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor("#1b3a3e"));
    p.setPen(QColor("#376c66"));
    p.drawRoundedRect(QRectF(0.5, 0.5, 39, 39), 9, 9);
    p.setPen(QPen(QColor("#79ebce"), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath waveform(QPointF(7, 21));
    waveform.lineTo(12, 21);
    waveform.lineTo(15.5, 13);
    waveform.lineTo(19.5, 29);
    waveform.lineTo(23, 10);
    waveform.lineTo(27.5, 23);
    waveform.lineTo(30, 19);
    waveform.lineTo(33, 19);
    p.drawPath(waveform);
    return logo;
}

} // namespace

MainWindow::MainWindow(const QString& initialPreset) {
    setWindowTitle(QStringLiteral("Engine Sound Lab %1").arg(QCoreApplication::applicationVersion()));
    setWindowIcon(QIcon(mark()));
    setMinimumSize(1040, 704);
    resize(1432, 864);
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("workspace"));
    setCentralWidget(content);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(18, 15, 18, 9);
    root->setSpacing(11);
    root->addWidget(makeHeader());
    root->addWidget(makeTransport());

    auto* columns = new QSplitter(Qt::Horizontal);
    columns->setChildrenCollapsible(false);
    columns->setHandleWidth(11);
    columns->addWidget(makeModelPanel());
    columns->addWidget(makeTelemetryPanel());
    columns->addWidget(makeMixer());
    columns->setStretchFactor(0, 0);
    columns->setStretchFactor(1, 1);
    columns->setStretchFactor(2, 0);
    columns->setSizes({300, 700, 347});
    root->addWidget(columns, 1);
    root->addWidget(makeFooter());

    auto* timer = new QTimer(this);
    timer->setInterval(42);
    connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
    timer->start();
    connect(new QShortcut(QKeySequence::Open, this), &QShortcut::activated, this, &MainWindow::openPreset);
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, &MainWindow::storePreset);
    loadBuiltins(initialPreset);
}

QWidget* MainWindow::makeHeader() {
    auto* head = new QWidget;
    auto* bar = new QHBoxLayout(head);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(9);
    auto* logo = new QLabel;
    logo->setPixmap(mark());
    logo->setFixedSize(40, 40);
    bar->addWidget(logo);
    bar->addWidget(uiLabel(QStringLiteral("Engine Sound Lab"), "brand"));
    bar->addStretch();
    auto* presetLabel = uiLabel(QStringLiteral("Двигатель"), "caption");
    bar->addWidget(presetLabel);
    presetButton_ = new QPushButton;
    presetButton_->setObjectName(QStringLiteral("presetPicker"));
    presetButton_->setFixedWidth(230);
    presetButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogListView));
    presetLabel->setBuddy(presetButton_);
    bar->addWidget(presetButton_);
    connect(presetButton_, &QPushButton::clicked, this, [this] {
        const int choice = chooseLibraryPreset(this, presets_, currentPreset_);
        if (choice >= 0 && choice != currentPreset_) selectPreset(choice);
    });
    connect(new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this), &QShortcut::activated, presetButton_, &QPushButton::click);
    auto* open = new QPushButton(QStringLiteral("Открыть…"));
    auto* save = new QPushButton(QStringLiteral("Сохранить…"));
    export_ = new QPushButton(QStringLiteral("Экспорт WAV"));
    export_->setObjectName(QStringLiteral("primary"));
    bar->addWidget(open);
    bar->addWidget(save);
    bar->addSpacing(4);
    bar->addWidget(export_);
    connect(open, &QPushButton::clicked, this, &MainWindow::openPreset);
    connect(save, &QPushButton::clicked, this, &MainWindow::storePreset);
    connect(export_, &QPushButton::clicked, this, &MainWindow::exportWav);
    return head;
}

QWidget* MainWindow::makeTransport() {
    auto* frame = panel("transport");
    auto* bar = new QHBoxLayout(frame);
    bar->setContentsMargins(13, 10, 13, 10);
    bar->setSpacing(17);
    run_ = new QPushButton(QStringLiteral("▶  Пуск"));
    run_->setObjectName(QStringLiteral("run"));
    run_->setCheckable(true);
    run_->setFixedWidth(100);
    run_->setMinimumHeight(40);
    bar->addWidget(run_);
    bar->addWidget(controlStrip(QStringLiteral("ГАЗ"), throttle_), 2);
    bar->addWidget(controlStrip(QStringLiteral("НАГРУЗКА"), load_), 2);
    bar->addWidget(controlStrip(QStringLiteral("ГРОМКОСТЬ"), master_, silentFader, true), 1);
    auto* listener = new QVBoxLayout;
    listener->setSpacing(2);
    listener->addWidget(uiLabel(QStringLiteral("СЛУШАТЕЛЬ"), "caption"));
    listenerPosition_ = new QComboBox;
    for (int i = 0; i <= static_cast<int>(ListeningPosition::Distant); ++i)
        listenerPosition_->addItem(listeningPositionName(static_cast<ListeningPosition>(i)));
    listenerPosition_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    listenerPosition_->setAccessibleName(QStringLiteral("Позиция слушателя"));
    listener->addWidget(listenerPosition_);
    bar->addLayout(listener);
    auto* rpm = new QVBoxLayout;
    rpm->setSpacing(0);
    rpm->addWidget(uiLabel(QStringLiteral("ОБ / МИН"), "caption"));
    liveRpm_ = uiLabel(QStringLiteral("0"), "readout");
    liveRpm_->setMinimumWidth(68);
    rpm->addWidget(liveRpm_);
    bar->addLayout(rpm);
    connect(run_, &QPushButton::clicked, this, [this] {
        const bool wasRunning = running_ || activeProgram_ != Program::Manual;
        leaveProgram();
        running_ = !wasRunning;
        run_->setChecked(running_);
        run_->setText(running_ ? QStringLiteral("■  Стоп") : QStringLiteral("▶  Пуск"));
        sendAudioSettings();
    });
    connect(throttle_, &QSlider::valueChanged, this, [this](int v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.throttle = v / 100.0;
        submitEdit();
    });
    connect(load_, &QSlider::valueChanged, this, [this](int v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.load = v / 100.0;
        submitEdit();
    });
    connect(master_, &QSlider::valueChanged, this, [this](int v) {
        if (syncing_) return;
        preset_.settings.controls.master = faderToGain(v);
        submitEdit();
    });
    return frame;
}

QWidget* MainWindow::makeModelPanel() {
    auto* frame = panel();
    frame->setMinimumWidth(254);
    auto* stack = new QVBoxLayout(frame);
    stack->setContentsMargins(13, 12, 13, 13);
    stack->setSpacing(8);
    stack->addWidget(uiLabel(QStringLiteral("Двигатель"), "section"));
    description_ = uiLabel(QString(), "hint");
    description_->setWordWrap(true);
    description_->setMinimumHeight(35);
    stack->addWidget(description_);
    auto* diagramBar = new QHBoxLayout;
    diagramBar->addWidget(uiLabel(QStringLiteral("Схема двигателя"), "caption"));
    diagramBar->addStretch();
    auto* slow = new QToolButton;
    slow->setText(QStringLiteral("×1/30"));
    slow->setObjectName(QStringLiteral("slowMotion"));
    slow->setCheckable(true);
    slow->setChecked(true);
    slow->setToolTip(QStringLiteral("Замедлить движение схемы в 30 раз"));
    slow->setAccessibleName(QStringLiteral("Замедление схемы"));
    diagramBar->addWidget(slow);
    stack->addLayout(diagramBar);
    diagram_ = new EngineDiagram;
    connect(slow, &QToolButton::toggled, diagram_, &EngineDiagram::setSlow);
    stack->addWidget(diagram_);
    auto* tabs = new QTabWidget;
    stack->addWidget(tabs, 1);

    auto* construction = new QWidget;
    auto* layout = new QVBoxLayout(construction);
    layout->setContentsMargins(0, 11, 1, 5);
    layout->setSpacing(7);
    auto* fields = new QWidget;
    modelForm_ = formFor(fields);
    layout->addWidget(fields);
    tabs->addTab(scrollPane(construction), QStringLiteral("Конструкция"));

    kind_ = choices({QStringLiteral("Дизельный"), QStringLiteral("Бензиновый"), QStringLiteral("Газотурбинный")});
    layout_ = choices({QStringLiteral("Рядный"), QStringLiteral("V-образный"), QStringLiteral("Оппозитный"), QStringLiteral("Звездообразный")});
    cylinders_ = new QComboBox;
    cylinders_->setToolTip(QStringLiteral("Для звезды доступны нечётный ряд и два равных нечётных ряда."));
    strokes_ = choices({QStringLiteral("Четырёхтактный"), QStringLiteral("Двухтактный")});
    liters_ = numberField(0.05, 120, 2, 0.25, QStringLiteral(" л"));
    bore_ = numberField(1, 4000, 2, 0.5, QStringLiteral(" мм"), true);
    strokeMm_ = numberField(10, 650, 2, 0.5, QStringLiteral(" мм"), true);
    compression_ = numberField(3, 30, 1, 0.1, QStringLiteral(" : 1"));
    bankAngle_ = numberField(10, 180, 0, 5, QStringLiteral("°"));
    boost_ = choices({QStringLiteral("Без наддува"), QStringLiteral("Турбонаддув"), QStringLiteral("Компрессор")});
    tracts_ = choices({QStringLiteral("Один"), QStringLiteral("Два")});
    formRow(modelForm_, QStringLiteral("Тип двигателя"), kind_);
    formRow(modelForm_, QStringLiteral("Компоновка"), layout_);
    cylinderSelector_ = indexedSlider(cylinders_, QStringLiteral("Цилиндры"));
    formRow(modelForm_, QStringLiteral("Цилиндры"), cylinderSelector_);
    formRow(modelForm_, QStringLiteral("Тактность"), strokes_);
    formRow(modelForm_, QStringLiteral("Рабочий объём"), liters_);
    formRow(modelForm_, QStringLiteral("Диаметр цилиндра"), bore_);
    formRow(modelForm_, QStringLiteral("Ход поршня"), strokeMm_);
    formRow(modelForm_, QStringLiteral("Степень сжатия"), compression_);
    formRow(modelForm_, QStringLiteral("Угол развала"), bankAngle_);
    formRow(modelForm_, QStringLiteral("Наддув"), boost_);
    tractSelector_ = indexedSlider(tracts_, QStringLiteral("Тракты выпуска"));
    formRow(modelForm_, QStringLiteral("Тракты выпуска"), tractSelector_);
    recording_ = new QComboBox;
    recording_->addItems(recordingLabels());
    formRow(modelForm_, QStringLiteral("Полевая запись"), recording_);
    connect(recording_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        preset_.settings.model.recording = builtinRecording(static_cast<RecordingId>(v));
        syncMixer();
        submitEdit();
    });

    coreMax_ = numberField(5000, 65000, 0, 500, rpmUnit);
    coreIdle_ = numberField(20, 87, 0, 1, percent);
    coreLag_ = numberField(0.15, 3.50, 2, 0.1, QStringLiteral(" с"));
    compressorBlades_ = numberField(4, 48, 0, 1);
    turbineBlades_ = numberField(4, 64, 0, 1);
    formRow(modelForm_, QStringLiteral("Газогенератор, макс."), coreMax_);
    formRow(modelForm_, QStringLiteral("Газогенератор, ХХ"), coreIdle_);
    formRow(modelForm_, QStringLiteral("Раскрутка ГГ"), coreLag_);
    formRow(modelForm_, QStringLiteral("Лопатки компресс."), compressorBlades_);
    formRow(modelForm_, QStringLiteral("Лопатки ГГ"), turbineBlades_);

    orderRow_ = new QWidget;
    auto* orderLayout = new QVBoxLayout(orderRow_);
    orderLayout->setContentsMargins(0, 6, 0, 0);
    orderLayout->setSpacing(5);
    orderLayout->addWidget(uiLabel(QStringLiteral("Порядок работы цилиндров"), "caption"));
    auto* orderButtons = new QHBoxLayout;
    order_ = new QLineEdit;
    order_->setMaxLength(160);
    order_->setAccessibleName(QStringLiteral("Порядок работы цилиндров"));
    orderButtons->addWidget(order_, 1);
    auto* defaultOrder = new QToolButton;
    defaultOrder->setText(QStringLiteral("Авто"));
    defaultOrder->setToolTip(QStringLiteral("Восстановить автоматический порядок"));
    orderButtons->addWidget(defaultOrder);
    orderLayout->addLayout(orderButtons);
    orderError_ = uiLabel(QString(), "validation");
    orderError_->setWordWrap(true);
    orderError_->hide();
    orderLayout->addWidget(orderError_);
    layout->addWidget(orderRow_);
    layout->addStretch();

    auto* behavior = new QWidget;
    auto* behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->setContentsMargins(0, 11, 1, 5);
    auto* motion = new QWidget;
    behaviorLayout->addWidget(motion);
    behaviorLayout->addStretch();
    dynamicsForm_ = formFor(motion);
    tabs->addTab(scrollPane(behavior), QStringLiteral("Динамика"));
    tabs->setTabToolTip(tabs->addTab(scrollPane(makeFaultPanel()), QStringLiteral("Сбои")), QStringLiteral("Неисправности двигателя"));
    idleRpm_ = numberField(100, 5000, 1, 25, rpmUnit);
    maxRpm_ = numberField(150, 18000, 1, 50, rpmUnit);
    inertia_ = numberField(0.05, 200, 2, 0.2, QStringLiteral(" кг·м²"));
    power_ = numberField(1, 2200, 1, 10, QStringLiteral(" кВт"));
    torque_ = numberField(1, 16000, 0, 25, QStringLiteral(" Н·м"));
    loss_ = numberField(0, 100, 0, 1, percent);
    overrunDrag_ = numberField(0, 100, 0, 1, percent);
    irregularity_ = numberField(0, 100, 0, 1, percent);
    seed_ = numberField(0, 4294967295.0, 0, 1);
    formRow(dynamicsForm_, QStringLiteral("Холостой ход"), idleRpm_);
    formRow(dynamicsForm_, QStringLiteral("Макс. обороты"), maxRpm_);
    maxRpm_->setToolTip(QStringLiteral("Верхняя граница оборотов модели и диапазона экспорта"));
    limiterEnabled_ = new QCheckBox(QStringLiteral("Включить отсечку"));
    dynamicsForm_->addRow(limiterEnabled_);
    cutRpm_ = numberField(101, 18000, 1, 25, rpmUnit);
    resumeRpm_ = numberField(100, 17999, 1, 25, rpmUnit);
    formRow(dynamicsForm_, QStringLiteral("Отсечка"), cutRpm_);
    formRow(dynamicsForm_, QStringLiteral("Возврат подачи"), resumeRpm_);
    connect(limiterEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (syncing_) return;
        preset_.settings.limiter.enabled = enabled;
        updateRpmLimits();
        submitEdit();
    });
    connect(cutRpm_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.limiter.cutRpm = v;
        updateRpmLimits();
        submitEdit();
    });
    connect(resumeRpm_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.limiter.resumeRpm = v;
        submitEdit();
    });
    formRow(dynamicsForm_, QStringLiteral("Инерция"), inertia_);
    formRow(dynamicsForm_, QStringLiteral("Мощность"), power_);
    formRow(dynamicsForm_, QStringLiteral("Крутящий момент"), torque_);
    formRow(dynamicsForm_, QStringLiteral("Мех. потери"), loss_);
    formRow(dynamicsForm_, QStringLiteral("Торможение на сбросе"), overrunDrag_);
    overrunDrag_->setToolTip(QStringLiteral("Дополнительное сопротивление двигателя при отпускании газа."));
    formRow(dynamicsForm_, QStringLiteral("Неравномерность"), irregularity_);
    formRow(dynamicsForm_, QStringLiteral("Случайный seed"), seed_);
    inertia_->setToolTip(QStringLiteral("Эффективная инерция на стенде. Выше значение — медленнее разгон и выбег."));

    connect(kind_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        preset_.settings.model.kind = static_cast<EngineKind>(v);
        syncModel();
        syncMixer();
        submitEdit();
    });
    connect(layout_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        auto& m = preset_.settings.model;
        m.layout = static_cast<Layout>(v);
        if (!validCylinderCount(m.layout, m.cylinders)) m.cylinders = 9;
        m.firingOrder = {};
        syncModel();
        submitEdit();
    });
    connect(cylinders_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_ || v < 0) return;
        preset_.settings.model.cylinders = cylinders_->currentData().toInt();
        preset_.settings.model.firingOrder = {};
        syncGeometry();
        updateFiringOrder();
        syncFaults();
        submitEdit();
    });
    connect(strokes_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        auto& m = preset_.settings.model;
        m.strokes = v == 0 ? 4 : 2;
        if (!validFiringOrder(m, m.firingOrder)) m.firingOrder = {};
        updateFiringOrder();
        submitEdit();
    });
    connect(boost_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        preset_.settings.model.boost = static_cast<Boost>(v);
        syncLayer();
        submitEdit();
    });
    connect(tracts_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        preset_.settings.model.exhaustTracts = v + 1;
        submitEdit();
    });
    const auto bind = [this](NumberField* field, auto member, double factor = 1.0) {
        connect(field, &NumberField::valueChanged, this, [this, member, factor](double v) {
            if (syncing_) return;
            auto& parameter = preset_.settings.model.*member;
            parameter = static_cast<std::remove_reference_t<decltype(parameter)>>(v * factor);
            updateRpmLimits();
            submitEdit();
        });
    };
    connect(liters_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.model.liters = v;
        syncGeometry();
        submitEdit();
    });
    connect(bore_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        auto& m = preset_.settings.model;
        m.strokeMm = pistonStrokeMm(m);
        m.liters = std::clamp(displacementLiters(m.cylinders, v, m.strokeMm), 0.05, 120.0);
        syncGeometry();
        submitEdit();
    });
    connect(strokeMm_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        auto& m = preset_.settings.model;
        m.liters = std::clamp(displacementLiters(m.cylinders, cylinderBoreMm(m), v), 0.05, 120.0);
        m.strokeMm = v;
        syncGeometry();
        submitEdit();
    });
    bind(compression_, &Model::compression);
    bind(bankAngle_, &Model::bankAngle);
    bind(coreMax_, &Model::coreMaxRpm);
    bind(coreIdle_, &Model::coreIdle, 0.01);
    bind(coreLag_, &Model::coreLag);
    bind(compressorBlades_, &Model::compressorBlades);
    bind(turbineBlades_, &Model::turbineBlades);
    bind(idleRpm_, &Model::idleRpm);
    bind(maxRpm_, &Model::maxRpm);
    bind(inertia_, &Model::inertia);
    bind(power_, &Model::powerKw);
    bind(torque_, &Model::torqueNm);
    bind(loss_, &Model::loss, 0.01);
    bind(overrunDrag_, &Model::overrunDrag, 0.01);
    bind(irregularity_, &Model::irregularity, 0.01);
    bind(seed_, &Model::seed);

    connect(defaultOrder, &QToolButton::clicked, this, [this] {
        preset_.settings.model.firingOrder = {};
        updateFiringOrder();
        submitEdit();
    });
    connect(order_, &QLineEdit::editingFinished, this, [this] {
        if (syncing_) return;
        auto& m = preset_.settings.model;
        if (!parseFiringOrder(order_->text(), m, m.firingOrder)) {
            orderError_->setText(m.layout == Layout::Radial ? QStringLiteral("Допустим циклический сдвиг схемы «Авто».") :
                QStringLiteral("Перечислите каждый цилиндр от 1 до %1 один раз.").arg(m.cylinders));
            orderError_->show();
            return;
        }
        updateFiringOrder();
        submitEdit();
    });
    return frame;
}

QWidget* MainWindow::makeFaultPanel() {
    auto* page = new QWidget;
    auto* stack = new QVBoxLayout(page);
    stack->setContentsMargins(0, 11, 1, 5);
    stack->setSpacing(13);
    faultCylinderRow_ = new QWidget;
    auto* target = formFor(faultCylinderRow_);
    faultCylinder_ = new QComboBox;
    faultCylinder_->setToolTip(QStringLiteral("Цилиндр для пропусков искры или износа дизельной форсунки"));
    formRow(target, QStringLiteral("Цилиндр"), indexedSlider(faultCylinder_, QStringLiteral("Цилиндр неисправности")));
    stack->addWidget(faultCylinderRow_);
    sparkRow_ = controlStrip(QStringLiteral("Пропуски искры"), spark_);
    injectorRow_ = controlStrip(QStringLiteral("Износ форсунки"), injector_);
    spark_->setToolTip(QStringLiteral("Вероятность пропуска при каждом рабочем такте выбранного цилиндра"));
    injector_->setToolTip(QStringLiteral("Нерегулярная подача и запаздывающий жёсткий впрыск на выбранном цилиндре"));
    stack->addWidget(sparkRow_);
    stack->addWidget(injectorRow_);
    stack->addWidget(controlStrip(QStringLiteral("Подача топлива"), fuel_, -100));
    auto* scale = new QHBoxLayout;
    scale->addWidget(uiLabel(QStringLiteral("← Нехватка"), "hint"));
    scale->addStretch();
    scale->addWidget(uiLabel(QStringLiteral("Перелив →"), "hint"));
    stack->addLayout(scale);
    fuel_->setToolTip(QStringLiteral("Отрицательные значения создают нехватку топлива. Положительные переобогащают смесь."));
    stack->addWidget(controlStrip(QStringLiteral("Перегрузка"), overload_));
    overload_->setToolTip(QStringLiteral("Дополнительное сопротивление стенда и неровный ударный гул под нагрузкой"));
    stack->addWidget(controlStrip(QStringLiteral("Люфт механики"), wear_));
    wear_->setToolTip(QStringLiteral("Стук клапанного механизма и дребезг редуктора"));
    auto* restore = new QPushButton(QStringLiteral("Сбросить неисправности"));
    stack->addWidget(restore);
    stack->addStretch();

    connect(faultCylinder_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (syncing_ || index < 0) return;
        preset_.settings.faults.cylinder = index + 1;
        submitEdit();
    });
    const auto bind = [this](QSlider* field, double Faults::*member) {
        connect(field, &QSlider::valueChanged, this, [this, member](int v) {
            if (syncing_) return;
            preset_.settings.faults.*member = v / 100.0;
            submitEdit();
        });
    };
    bind(spark_, &Faults::sparkMiss);
    bind(fuel_, &Faults::fuelBias);
    bind(injector_, &Faults::injectorWear);
    bind(overload_, &Faults::overload);
    bind(wear_, &Faults::mechanicalWear);
    connect(restore, &QPushButton::clicked, this, [this] {
        preset_.settings.faults = {};
        syncFaults();
        submitEdit();
    });
    return page;
}

void MainWindow::syncFaults() {
    QScopedValueRollback guard(syncing_, true);
    const auto& m = preset_.settings.model;
    auto& f = preset_.settings.faults;
    const bool piston = m.kind != EngineKind::Turbine;
    faultCylinderRow_->setVisible(piston);
    sparkRow_->setVisible(m.kind == EngineKind::Gasoline);
    injectorRow_->setVisible(m.kind == EngineKind::Diesel);
    f.cylinder = std::clamp(f.cylinder, 1, m.cylinders);
    if (faultCylinder_->count() != m.cylinders) {
        faultCylinder_->clear();
        for (int i = 1; i <= m.cylinders; ++i) faultCylinder_->addItem(QString::number(i));
    }
    faultCylinder_->setCurrentIndex(f.cylinder - 1);
    spark_->setValue(qRound(f.sparkMiss * 100));
    fuel_->setValue(qRound(f.fuelBias * 100));
    injector_->setValue(qRound(f.injectorWear * 100));
    overload_->setValue(qRound(f.overload * 100));
    wear_->setValue(qRound(f.mechanicalWear * 100));
}

QWidget* MainWindow::makeTelemetryPanel() {
    auto* frame = panel();
    frame->setMinimumWidth(408);
    auto* outer = new QVBoxLayout(frame);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* content = new QWidget;
    auto* stack = new QVBoxLayout(content);
    outer->addWidget(scrollPane(content));
    stack->setContentsMargins(11, 8, 11, 11);
    stack->setSpacing(8);
    tach_ = new Tachometer;
    stack->addWidget(tach_, 4);

    auto* listener = new QHBoxLayout;
    acousticLabel_ = uiLabel({}, "caption");
    listener->addWidget(acousticLabel_);
    distance_ = numberField(1, 120, 1, 0.5, QStringLiteral(" м"), true);
    distance_->setFieldName(QStringLiteral("Расстояние до двигателя"));
    insulation_ = numberField(0, 100, 0, 1, percent);
    insulation_->setFieldName(QStringLiteral("Изоляция корпуса"));
    listener->addWidget(distance_, 1);
    listener->addWidget(insulation_, 1);
    stack->addLayout(listener);
    connect(listenerPosition_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        auto& s = preset_.settings.listener;
        s.position = static_cast<ListeningPosition>(v);
        s.distance = defaultDistance(s.position);
        syncListener();
        submitEdit();
    });
    connect(distance_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.listener.distance = v;
        submitEdit();
    });
    connect(insulation_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.listener.insulation = v / 100.0;
        submitEdit();
    });

    auto* drive = new QHBoxLayout;
    drive->setSpacing(6);
    hold_ = new QCheckBox(QStringLiteral("Удержание"));
    hold_->setToolTip(QStringLiteral("Задаёт обороты независимо от нагрузки."));
    holdRpm_ = numberField(100, 18000, 1, 25, rpmUnit);
    holdRpm_->setFieldName(QStringLiteral("Целевые обороты"));
    brake_ = new QCheckBox(QStringLiteral("Торможение"));
    brake_->setToolTip(QStringLiteral("Закрыть газ и добавить сопротивление трансмиссии."));
    gear_ = choices({QStringLiteral("N"), "1", "2", "3", "4", "5", "6"});
    gear_->setMaximumWidth(61);
    gear_->setAccessibleName(QStringLiteral("Передача"));
    drive->addWidget(hold_);
    drive->addWidget(holdRpm_, 1);
    stack->addLayout(drive);
    auto* transmission = new QHBoxLayout;
    transmission->addWidget(uiLabel(QStringLiteral("Передача"), "caption"));
    transmission->addWidget(indexedSlider(gear_, QStringLiteral("Передача")), 1);
    transmission->addWidget(brake_);
    stack->addLayout(transmission);

    auto* tests = new QHBoxLayout;
    tests->setSpacing(7);
    scenario_ = choices({QStringLiteral("Ручное управление"), QStringLiteral("Холостой ход"), QStringLiteral("Плавный разгон"),
        QStringLiteral("Резкий разгон"), QStringLiteral("Полная нагрузка"), QStringLiteral("Сброс газа"),
        QStringLiteral("Торможение двигателем"), QStringLiteral("Запуск и выключение"), QStringLiteral("Автоматическое испытание"),
        QStringLiteral("Поездка с переключениями")});
    scenario_->setAccessibleName(QStringLiteral("Сценарий испытания"));
    scenarioRun_ = new QPushButton(QStringLiteral("Запустить"));
    scenarioRun_->setMinimumWidth(92);
    tests->addWidget(uiLabel(QStringLiteral("Испытание"), "caption"));
    tests->addWidget(scenario_, 1);
    tests->addWidget(scenarioRun_);
    stack->addLayout(tests);
    scenarioProgress_ = new QProgressBar;
    scenarioProgress_->setRange(0, 1000);
    scenarioProgress_->setValue(0);
    scenarioProgress_->setTextVisible(false);
    scenarioProgress_->setFixedHeight(3);
    scenarioProgress_->setAccessibleName(QStringLiteral("Ход испытания"));
    stack->addWidget(scenarioProgress_);
    scope_ = new SignalView(false);
    spectrum_ = new SignalView(true);
    stack->addWidget(scope_, 2);
    stack->addWidget(spectrum_, 2);

    connect(hold_, &QCheckBox::toggled, this, [this](bool v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.hold = v;
        holdRpm_->setEnabled(v);
        submitEdit();
    });
    connect(holdRpm_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.holdRpm = v;
        submitEdit();
    });
    connect(gear_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.gear = v;
        submitEdit();
    });
    connect(brake_, &QCheckBox::toggled, this, [this](bool v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.brake = v;
        submitEdit();
    });
    connect(scenario_, &QComboBox::currentIndexChanged, this, [this](int v) {
        if (syncing_) return;
        leaveProgram();
        preset_.settings.controls.program = static_cast<Program>(v);
        scenarioRun_->setEnabled(v != 0);
        submitEdit();
    });
    connect(scenarioRun_, &QPushButton::clicked, this, [this] {
        if (activeProgram_ == Program::Manual) {
            activeProgram_ = preset_.settings.controls.program;
            ++programToken_;
            scenarioRun_->setText(QStringLiteral("Прервать"));
            scenarioProgress_->setValue(0);
            run_->setChecked(true);
            run_->setText(QStringLiteral("■  Стоп"));
        } else {
            leaveProgram();
            running_ = false;
        }
        sendAudioSettings();
    });
    return frame;
}

QWidget* MainWindow::makeMixer() {
    auto* frame = panel();
    frame->setMinimumWidth(293);
    auto* column = new QVBoxLayout(frame);
    column->setContentsMargins(13, 12, 13, 12);
    column->setSpacing(9);
    auto* mixerTitle = new QHBoxLayout;
    mixerTitle->addWidget(uiLabel(QStringLiteral("Микшер"), "section"));
    mixerTitle->addStretch();
    mixerTitle->addWidget(uiLabel(QStringLiteral("Уровень, дБ"), "caption"));
    column->addLayout(mixerTitle);
    auto* pane = new QWidget;
    auto* stack = new QVBoxLayout(pane);
    stack->setContentsMargins(0, 0, 1, 0);
    stack->setSpacing(5);
    column->addWidget(scrollPane(pane), 1);
    auto* layers = new QButtonGroup(this);
    for (std::size_t i = 0; i < layerCount; ++i) {
        auto* row = panel("mixRow");
        if (i == static_cast<std::size_t>(LayerId::Recording)) recordingMixRow_ = row;
        auto* bar = new QHBoxLayout(row);
        bar->setContentsMargins(6, 3, 5, 3);
        bar->setSpacing(5);
        auto* dot = new QLabel;
        dot->setFixedSize(5, 5);
        dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 2px;").arg(layerColors[i]));
        bar->addWidget(dot);
        layerName_[i] = new QToolButton;
        layerName_[i]->setObjectName(QStringLiteral("layerName"));
        layerName_[i]->setCheckable(true);
        layerName_[i]->setText(layerNames[i]);
        layerName_[i]->setMinimumWidth(86);
        layers->addButton(layerName_[i], static_cast<int>(i));
        bar->addWidget(layerName_[i]);
        auto* gain = layerGain_[i] = slider(maxLayerFader, layerNames[i]);
        gain->setMinimum(silentFader);
        gain->setPageStep(30);
        gain->setMinimumWidth(38);
        bar->addWidget(gain, 1);
        auto* value = uiLabel(QStringLiteral("0.0"), "numeric");
        value->setFixedWidth(39);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setToolTip(QStringLiteral("Усиление слоя в децибелах"));
        bar->addWidget(value);
        const auto button = [&](const char* key, const QString& letter, const QString& tip) {
            auto* tool = new QToolButton;
            tool->setObjectName(QLatin1String(key));
            tool->setText(letter);
            tool->setToolTip(tip);
            tool->setAccessibleName(tip + QStringLiteral(" — ") + layerNames[i]);
            tool->setFixedWidth(25);
            tool->setCheckable(true);
            bar->addWidget(tool);
            return tool;
        };
        layerSolo_[i] = button("solo", QStringLiteral("S"), QStringLiteral("Solo"));
        layerMute_[i] = button("mute", QStringLiteral("M"), QStringLiteral("Отключить слой"));
        stack->addWidget(row);

        connect(gain, &QSlider::valueChanged, this, [this, i, value](int v) {
            value->setText(faderText(v));
            if (syncing_) return;
            preset_.settings.layers[i].gain = faderToGain(v);
            submitEdit();
        });
        connect(layerSolo_[i], &QToolButton::toggled, this, [this, i](bool on) {
            if (syncing_) return;
            preset_.settings.layers[i].solo = on;
            submitEdit();
        });
        connect(layerMute_[i], &QToolButton::toggled, this, [this, i](bool on) {
            if (syncing_) return;
            preset_.settings.layers[i].mute = on;
            submitEdit();
        });
    }
    connect(layers, &QButtonGroup::idClicked, this, [this](int id) { selectedLayer_ = id; syncLayer(); });
    stack->addSpacing(6);
    layerTitle_ = uiLabel(QString(), "section");
    stack->addWidget(layerTitle_);
    auto* parameters = new QWidget;
    layerForm_ = formFor(parameters);
    layerForm_->setVerticalSpacing(6);
    resonance_ = numberField(25, 9000, 0, 5, hzUnit, true);
    cutoff_ = numberField(90, 14000, 0, 50, hzUnit, true);
    shape_ = numberField(0, 100, 0, 1, percent);
    amount_ = numberField(0, 100, 0, 1, percent);
    damping_ = numberField(0, 100, 0, 1, percent);
    drive_ = numberField(0, 100, 0, 1, percent);
    formRow(layerForm_, QStringLiteral("Резонанс"), resonance_);
    formRow(layerForm_, QStringLiteral("Фильтр НЧ"), cutoff_);
    formRow(layerForm_, QStringLiteral("Характер"), shape_);
    formRow(layerForm_, QStringLiteral("Интенсивность"), amount_);
    formRow(layerForm_, QStringLiteral("Демпфирование"), damping_);
    formRow(layerForm_, QStringLiteral("Насыщение"), drive_);
    cutoff_->setToolTip(QStringLiteral("Частота среза фильтра низких частот"));
    stack->addWidget(parameters);
    layerHint_ = uiLabel(QString(), "hint");
    layerHint_->setWordWrap(true);
    stack->addWidget(layerHint_);
    stack->addStretch();

    const auto bind = [this](NumberField* field, double Layer::*member, double factor = 1.0) {
        connect(field, &NumberField::valueChanged, this, [this, member, factor](double v) {
            if (syncing_) return;
            preset_.settings.layers[selectedLayer_].*member = v * factor;
            submitEdit();
        });
    };
    bind(resonance_, &Layer::resonance);
    bind(cutoff_, &Layer::cutoff);
    bind(amount_, &Layer::amount, 0.01);
    bind(damping_, &Layer::damping, 0.01);
    bind(drive_, &Layer::drive, 0.01);
    connect(shape_, &NumberField::valueChanged, this, [this](double v) {
        if (syncing_) return;
        preset_.settings.layers[selectedLayer_].shape = selectedLayer_ == 5 ? (v - 1.0) / 69.0 : v / 100.0;
        submitEdit();
    });
    return frame;
}

QWidget* MainWindow::makeFooter() {
    auto* footer = new QWidget;
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(1, 0, 1, 0);
    row->setSpacing(10);
    device_ = uiLabel(QStringLiteral("48 кГц · СТЕРЕО · ГОТОВ"), "status");
    peak_ = uiLabel(QStringLiteral("ПИК −∞ дБFS"), "status");
    peak_->setMinimumWidth(93);
    message_ = uiLabel(QString(), "status");
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    renderProgress_ = new QProgressBar;
    renderProgress_->setFixedSize(130, 7);
    renderProgress_->setTextVisible(false);
    renderProgress_->setAccessibleName(QStringLiteral("Ход рендера WAV"));
    renderProgress_->hide();
    cancelExport_ = new QPushButton(QStringLiteral("Отменить экспорт"));
    cancelExport_->hide();
    openFolder_ = new QPushButton(QStringLiteral("Открыть папку"));
    openFolder_->hide();
    row->addWidget(device_);
    row->addWidget(peak_);
    row->addSpacing(3);
    row->addWidget(message_, 1);
    row->addWidget(renderProgress_);
    row->addWidget(cancelExport_);
    row->addWidget(openFolder_);
    job_ = new QFutureWatcher<ExportResult>(this);
    connect(cancelExport_, &QPushButton::clicked, this, [this] {
        cancel_.store(true, std::memory_order_relaxed);
        cancelExport_->setEnabled(false);
        message_->setText(QStringLiteral("Отмена экспорта…"));
    });
    connect(openFolder_, &QPushButton::clicked, this, [this] {
        if (!exportedPath_.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(exportedPath_));
    });
    connect(job_, &QFutureWatcher<ExportResult>::finished, this, &MainWindow::finishExport);
    return footer;
}

void MainWindow::loadBuiltins(const QString& initialPreset) {
    int initial = 0;
    const QDir resources(QStringLiteral(":/presets"));
    for (const auto& file : resources.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        Preset p;
        if (!loadPreset(resources.filePath(file), p).isEmpty()) continue;
        if (p.name == initialPreset) initial = static_cast<int>(presets_.size());
        presets_.push_back(std::move(p));
    }
    if (presets_.empty()) presets_.push_back({QStringLiteral("Дизельный V12"), {}, {}});
    selectPreset(initial);
}

void MainWindow::updatePresetButton() {
    const int textWidth = presetButton_->width() - presetButton_->iconSize().width() - 36;
    auto label = presetButton_->fontMetrics().elidedText(preset_.name, Qt::ElideRight, textWidth);
    presetButton_->setText(label.replace('&', QStringLiteral("&&")));
    presetButton_->setAccessibleName(QStringLiteral("Выбрать двигатель · %1").arg(preset_.name));
    presetButton_->setToolTip(QStringLiteral("%1\nОткрыть библиотеку двигателей (Ctrl+K)").arg(preset_.name));
}

void MainWindow::selectPreset(int index) {
    if (syncing_ || index < 0 || index >= static_cast<int>(presets_.size())) return;
    leaveProgram();
    currentPreset_ = index;
    preset_ = presets_[index];
    updatePresetButton();
    if (preset_.settings.controls.program == Program::Drive)
        selectedLayer_ = static_cast<int>(LayerId::Transmission);
    description_->setText(preset_.description.isEmpty() ? QStringLiteral("Пользовательский пресет") : preset_.description);
    syncModel();
    syncBehavior();
    syncListener();
    syncMixer();
    sendAudioSettings();
    refresh();
}

void MainWindow::openPreset() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Открыть пресет"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStringLiteral("Пресет двигателя (*.json)"));
    if (path.isEmpty()) return;
    Preset opened;
    const auto error = loadPreset(path, opened);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Ошибка пресета"), error);
        return;
    }
    presets_.push_back(std::move(opened));
    selectPreset(static_cast<int>(presets_.size()) - 1);
    message_->setText(QStringLiteral("Открыт %1").arg(path));
}

void MainWindow::storePreset() {
    bool accepted = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Сохранить пресет"), QStringLiteral("Название двигателя"),
        QLineEdit::Normal, preset_.name, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    if (name.size() > 100) {
        QMessageBox::warning(this, QStringLiteral("Ошибка пресета"), QStringLiteral("Название должно содержать не более 100 символов."));
        return;
    }
    const auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить пресет"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("engine.json")),
        QStringLiteral("Пресет двигателя (*.json)"));
    if (path.isEmpty()) return;
    auto saved = preset_;
    saved.name = name;
    const auto error = savePreset(path, saved);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Ошибка сохранения"), error);
        return;
    }
    preset_ = saved;
    presets_[currentPreset_] = saved;
    updatePresetButton();
    message_->setText(QStringLiteral("Сохранён %1").arg(path));
}

void MainWindow::syncModel() {
    QScopedValueRollback guard(syncing_, true);
    const auto& m = preset_.settings.model;
    const bool piston = m.kind != EngineKind::Turbine;
    kind_->setCurrentIndex(static_cast<int>(m.kind));
    layout_->setCurrentIndex(static_cast<int>(m.layout));
    cylinders_->clear();
    for (int n = 1; n <= maxCylinders; ++n) {
        if (validCylinderCount(m.layout, n)) cylinders_->addItem(QString::number(n), n);
    }
    cylinders_->setCurrentIndex(cylinders_->findData(m.cylinders));
    strokes_->setCurrentIndex(m.strokes == 4 ? 0 : 1);
    syncGeometry();
    bankAngle_->setValue(m.bankAngle);
    boost_->setCurrentIndex(static_cast<int>(m.boost));
    tracts_->setCurrentIndex(m.exhaustTracts - 1);
    coreMax_->setValue(m.coreMaxRpm);
    coreIdle_->setValue(m.coreIdle * 100);
    coreLag_->setValue(m.coreLag);
    compressorBlades_->setValue(m.compressorBlades);
    turbineBlades_->setValue(m.turbineBlades);
    for (QWidget* widget : std::array<QWidget*, 10>{layout_, cylinderSelector_, strokes_, liters_, bore_, strokeMm_,
                                                   compression_, bankAngle_, boost_, tractSelector_})
        modelForm_->setRowVisible(widget, piston);
    modelForm_->setRowVisible(recording_, piston);
    recording_->setCurrentIndex(static_cast<int>(m.recording ? m.recording->id : RecordingId::None));
    modelForm_->setRowVisible(bankAngle_, piston && m.layout == Layout::V);
    for (QWidget* widget : std::array<QWidget*, 5>{coreMax_, coreIdle_, coreLag_, compressorBlades_, turbineBlades_})
        modelForm_->setRowVisible(widget, !piston);
    orderRow_->setVisible(piston);
    dynamicsForm_->setRowVisible(irregularity_, piston);
    updateFiringOrder();

    updateRpmLimits();
    idleRpm_->setValue(m.idleRpm);
    maxRpm_->setValue(m.maxRpm);
    inertia_->setValue(m.inertia);
    power_->setValue(m.powerKw);
    torque_->setValue(m.torqueNm);
    loss_->setValue(m.loss * 100);
    overrunDrag_->setValue(m.overrunDrag * 100);
    overrunDrag_->setEnabled(piston);
    irregularity_->setValue(m.irregularity * 100);
    seed_->setValue(m.seed);
    syncFaults();
}

void MainWindow::syncGeometry() {
    QScopedValueRollback guard(syncing_, true);
    const auto& m = preset_.settings.model;
    const double stroke = pistonStrokeMm(m), bore = cylinderBoreMm(m);
    const double perMm = displacementLiters(m.cylinders, bore, 1.0);
    strokeMm_->setRange(std::max(10.0, 0.05 / perMm), std::min(650.0, 120.0 / perMm));
    bore_->setRange(bore * std::sqrt(0.05 / m.liters), bore * std::sqrt(120.0 / m.liters));
    liters_->setValue(m.liters);
    strokeMm_->setValue(stroke);
    bore_->setValue(bore);
    compression_->setValue(compressionRatio(m));
}

void MainWindow::updateRpmLimits() {
    QScopedValueRollback guard(syncing_, true);
    const auto& m = preset_.settings.model;
    auto& c = preset_.settings.controls;
    idleRpm_->setMaximum(std::min(5000.0, m.maxRpm - 50.0));
    maxRpm_->setMinimum(m.idleRpm + 50.0);
    auto& limit = preset_.settings.limiter;
    limit.cutRpm = std::clamp(limit.cutRpm, m.idleRpm + 1.0, m.maxRpm);
    limit.resumeRpm = std::clamp(limit.resumeRpm, m.idleRpm, limit.cutRpm - 1.0);
    limiterEnabled_->setChecked(limit.enabled);
    cutRpm_->setRange(m.idleRpm + 1.0, m.maxRpm);
    cutRpm_->setValue(limit.cutRpm);
    resumeRpm_->setRange(m.idleRpm, limit.cutRpm - 1.0);
    resumeRpm_->setValue(limit.resumeRpm);
    cutRpm_->setEnabled(limit.enabled);
    resumeRpm_->setEnabled(limit.enabled);
    holdRpm_->setRange(m.idleRpm, m.maxRpm);
    c.holdRpm = std::clamp(c.holdRpm, m.idleRpm, m.maxRpm);
    holdRpm_->setValue(c.holdRpm);
}

void MainWindow::updateFiringOrder() {
    const auto& m = preset_.settings.model;
    const auto order = firingOrderText(m);
    order_->setPlaceholderText(order);
    order_->setText(m.firingOrder[0] == 0 ? QString() : order);
    order_->setToolTip(QStringLiteral("%1\nНомера через пробел. Пустое поле включает автоматическую схему.").arg(order));
    orderError_->hide();
}

void MainWindow::syncBehavior() {
    QScopedValueRollback guard(syncing_, true);
    const auto& c = preset_.settings.controls;
    throttle_->setValue(qRound(c.throttle * 100));
    load_->setValue(qRound(c.load * 100));
    master_->setValue(gainToFader(c.master));
    hold_->setChecked(c.hold);
    holdRpm_->setEnabled(c.hold);
    holdRpm_->setValue(c.holdRpm);
    brake_->setChecked(c.brake);
    gear_->setCurrentIndex(c.gear);
    scenario_->setCurrentIndex(static_cast<int>(c.program));
    scenarioRun_->setEnabled(c.program != Program::Manual);
}

void MainWindow::syncListener() {
    QScopedValueRollback guard(syncing_, true);
    const auto& s = preset_.settings.listener;
    const bool inside = s.position == ListeningPosition::Interior;
    const bool exterior = !inside && s.position != ListeningPosition::Direct;
    listenerPosition_->setCurrentIndex(static_cast<int>(s.position));
    distance_->setValue(s.distance);
    insulation_->setValue(s.insulation * 100);
    distance_->setVisible(exterior);
    insulation_->setVisible(inside);
    acousticLabel_->setText(inside ? QStringLiteral("Изоляция") : QStringLiteral("Дистанция"));
    acousticLabel_->setVisible(inside || exterior);
}

void MainWindow::syncMixer() {
    QScopedValueRollback guard(syncing_, true);
    const bool turbine = preset_.settings.model.kind == EngineKind::Turbine;
    const bool recorded = preset_.settings.model.recording && !turbine;
    recordingMixRow_->setVisible(recorded);
    if (!recorded && selectedLayer_ == static_cast<int>(LayerId::Recording)) selectedLayer_ = 1;
    for (std::size_t i = 0; i < layerCount; ++i) {
        QString name = layerNames[i];
        if (turbine && i == 0) name = QStringLiteral("Газогенератор");
        if (turbine && i == 4) name = QStringLiteral("Компрессор");
        layerName_[i]->setText(name);
        layerName_[i]->setToolTip(QStringLiteral("Настроить слой «%1»").arg(name));
        layerGain_[i]->setAccessibleName(QStringLiteral("Уровень слоя «%1»").arg(name));
        layerSolo_[i]->setAccessibleName(QStringLiteral("Solo — %1").arg(name));
        layerMute_[i]->setAccessibleName(QStringLiteral("Отключить слой — %1").arg(name));
        const auto& current = preset_.settings.layers[i];
        layerGain_[i]->setValue(gainToFader(current.gain));
        layerMute_[i]->setChecked(current.mute);
        layerSolo_[i]->setChecked(current.solo);
    }
    syncLayer();
}

void MainWindow::syncLayer() {
    QScopedValueRollback guard(syncing_, true);
    const bool turbine = preset_.settings.model.kind == EngineKind::Turbine;
    const int id = selectedLayer_;
    const bool transmission = id == static_cast<int>(LayerId::Transmission);
    const auto& layer = preset_.settings.layers[id];
    layerName_[id]->setChecked(true);
    layerTitle_->setText(layerName_[id]->text());
    const std::array shapes{QStringLiteral("Длительность импульса"), QStringLiteral("Размер выпуска"), QStringLiteral("Шум потока"),
        QStringLiteral("Шум редуктора"), QStringLiteral("Инерция ротора"), QStringLiteral("Масса корпуса"), QStringLiteral("Влияние нагрузки"),
        QStringLiteral("Вой шестерён")};
    const std::array strengths{QStringLiteral("Резкость вспышки"), QStringLiteral("Низкочастотный гул"), QStringLiteral("Всасывание"),
        QStringLiteral("Клапаны и вибрация"), QStringLiteral("Свист"), QStringLiteral("Вибрация корпуса"), QStringLiteral("Подъём резонанса"),
        QStringLiteral("Шум подшипников")};
    const auto renameField = [&](NumberField* field, const QString& name) {
        static_cast<QLabel*>(layerForm_->labelForField(field))->setText(name);
        field->setFieldName(name);
    };
    renameField(shape_, turbine && id == 4 ? QStringLiteral("Модуляция лопаток") :
        (turbine && id == 0 ? QStringLiteral("Газовый шум") : shapes[id]));
    renameField(amount_, turbine && id == 0 ? QStringLiteral("Гул камеры") :
        (turbine && id == 3 ? QStringLiteral("Звук редуктора") : strengths[id]));
    renameField(resonance_, transmission ? QStringLiteral("Тон зацепления") : QStringLiteral("Резонанс"));
    renameField(damping_, transmission ? QStringLiteral("Вой на сбросе") :
        (id == 1 ? QStringLiteral("Глушение") : (id == 4 && !turbine ? QStringLiteral("Затухание") : QStringLiteral("Демпфирование"))));
    renameField(cutoff_, QStringLiteral("Фильтр НЧ"));
    shape_->setRange(id == 5 ? 1 : 0, id == 5 ? 70 : 100);
    shape_->setSuffix(id == 5 ? QStringLiteral(" т") : percent);
    shape_->setValue(id == 5 ? 1 + 69 * layer.shape : layer.shape * 100);
    resonance_->setValue(layer.resonance);
    cutoff_->setValue(layer.cutoff);
    amount_->setValue(layer.amount * 100);
    damping_->setValue(layer.damping * 100);
    drive_->setValue(layer.drive * 100);
    if (id == 4 && !turbine && preset_.settings.model.boost == Boost::None)
        layerHint_->setText(QStringLiteral("Для этого слоя включите наддув в конструкторе."));
    else if (transmission)
        layerHint_->setText(QStringLiteral("Поднимите уровень и включите передачу 1–6 или запустите сценарий «Поездка с переключениями». Кнопка S оставляет в миксе только выбранные соло-каналы."));
    else layerHint_->clear();
    layerHint_->setVisible(!layerHint_->text().isEmpty());
}

void MainWindow::leaveProgram() {
    if (activeProgram_ == Program::Manual) return;
    const auto mode = meter_.engine.mode;
    running_ = mode != EngineMode::Off && mode != EngineMode::Stopping;
    activeProgram_ = Program::Manual;
    scenarioRun_->setText(QStringLiteral("Запустить"));
    scenarioProgress_->setValue(0);
}

void MainWindow::sendAudioSettings() {
    auto live = preset_.settings;
    live.controls.running = running_;
    live.controls.program = activeProgram_;
    live.controls.programToken = programToken_;
    output_.submit(live);
    diagram_->setData(live.model, meter_.engine, meter_.phases, meter_.ignitions);
    tach_->setData(live.model, meter_.engine, live.controls.hold && activeProgram_ == Program::Manual, live.controls.holdRpm, live.limiter);
}

void MainWindow::submitEdit() {
    if (currentPreset_ >= 0) presets_[currentPreset_] = preset_;
    sendAudioSettings();
}

void MainWindow::refresh() {
    auto next = output_.snapshot();
    if (next.serial != meter_.serial) {
        meter_ = std::move(next);
        diagram_->setData(preset_.settings.model, meter_.engine, meter_.phases, meter_.ignitions);
        scope_->setSignal(meter_);
        spectrum_->setSignal(meter_);
        const auto& c = preset_.settings.controls;
        tach_->setData(preset_.settings.model, meter_.engine, c.hold && activeProgram_ == Program::Manual, c.holdRpm, preset_.settings.limiter);
        liveRpm_->setText(QString::number(qRound(meter_.engine.rpm)));
        peak_->setText(meter_.peak > 1e-6 ? QStringLiteral("ПИК %1 дБFS").arg(QString::number(20 * std::log10(meter_.peak), 'f', 1)) :
            QStringLiteral("ПИК −∞ дБFS"));
        const bool noDevice = meter_.stream == StreamStatus::NoDevice;
        device_->setText(noDevice ? QStringLiteral("НЕТ АУДИОВЫХОДА · %1").arg(meter_.deviceError) :
            (meter_.stream == StreamStatus::Streaming ? QStringLiteral("48 кГц · СТЕРЕО · ВОСПРОИЗВЕДЕНИЕ") : QStringLiteral("48 кГц · СТЕРЕО · ГОТОВ")));
        device_->setToolTip(QStringLiteral("Пропуски буфера за сессию · %1").arg(meter_.underruns));
        if (device_->property("alert").toBool() != noDevice) {
            device_->setProperty("alert", noDevice);
            device_->style()->unpolish(device_);
            device_->style()->polish(device_);
        }
        if (activeProgram_ != Program::Manual && meter_.engine.program == activeProgram_ && meter_.engine.programToken == programToken_) {
            scenarioProgress_->setValue(qRound(meter_.engine.progress * 1000));
            if (meter_.engine.progress >= 1.0) {
                const auto selected = preset_.settings.controls.program;
                const auto end = programControls(activeProgram_, programDuration(activeProgram_), preset_.settings.controls);
                auto& controls = preset_.settings.controls;
                controls = end;
                controls.program = selected;
                running_ = end.running;
                activeProgram_ = Program::Manual;
                scenarioRun_->setText(QStringLiteral("Запустить"));
                syncBehavior();
                submitEdit();
            }
        }
    }
    const bool active = running_ || activeProgram_ != Program::Manual;
    if (run_->isChecked() != active) run_->setChecked(active);
    const auto label = active ? QStringLiteral("■  Стоп") : QStringLiteral("▶  Пуск");
    if (run_->text() != label) run_->setText(label);
}

void MainWindow::exportWav() {
    if (job_->isRunning()) return;
    auto snapshot = preset_;
    if (activeProgram_ != Program::Manual && meter_.engine.program == activeProgram_)
        snapshot.settings.controls.gear = meter_.engine.gear;
    const auto& c = snapshot.settings.controls;
    const bool playing = meter_.engine.mode != EngineMode::Off;
    const double rpm = playing ? meter_.engine.rpm : (c.hold ? c.holdRpm : snapshot.settings.model.idleRpm);
    ExportDialog dialog(snapshot, rpm, playing ? meter_.engine.load : c.load, playing ? meter_.engine.throttle : c.throttle, this);
    if (dialog.exec() != QDialog::Accepted) return;
    auto options = dialog.options();
    auto directory = dialog.directory();
    renderProgress_->setRange(0, 0);
    renderProgress_->show();
    openFolder_->hide();
    export_->setEnabled(false);
    cancelExport_->setEnabled(true);
    cancelExport_->show();
    cancel_.store(false, std::memory_order_relaxed);
    message_->setText(QStringLiteral("Рендер WAV…"));
    job_->setFuture(QtConcurrent::run([this, snapshot, options, directory] {
        return exportAudio(snapshot, options, directory, &cancel_, [this](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] {
                renderProgress_->setRange(0, total);
                renderProgress_->setValue(done);
            }, Qt::QueuedConnection);
        });
    }));
}

void MainWindow::finishExport() {
    const auto result = job_->result();
    export_->setEnabled(true);
    renderProgress_->hide();
    cancelExport_->hide();
    if (!result.error.isEmpty()) {
        message_->setText(result.error);
        QMessageBox::warning(this, QStringLiteral("Ошибка экспорта"), result.error);
    } else if (result.cancelled) {
        message_->setText(QStringLiteral("Экспорт отменён"));
    } else {
        exportedPath_ = result.directory;
        message_->setText(QStringLiteral("%1 WAV · %2").arg(result.wavCount).arg(QDir::toNativeSeparators(exportedPath_)));
        message_->setToolTip(QDir::toNativeSeparators(exportedPath_));
        openFolder_->show();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (job_->isRunning()) {
        cancel_.store(true, std::memory_order_relaxed);
        job_->waitForFinished();
    }
    QMainWindow::closeEvent(event);
}

} // namespace esl
