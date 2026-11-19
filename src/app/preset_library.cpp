#include "app/preset_library.hpp"

#include <QCollator>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <numeric>
#include <utility>

namespace esl {
namespace {

QString searchKey(QString term) {
    term = term.toCaseFolded();
    static const std::pair<QString, QString> aliases[]{
        {"камаз", "kamaz"}, {"бтр", "btr"}, {"бмп", "bmp"}, {"ошкош", "oshkosh"}, {"уаз", "uaz"},
        {"урал", "ural"}, {"краз", "kraz"}, {"бредли", "bradley"}, {"брэдли", "bradley"},
        {"леопард", "leopard"}, {"страйкер", "stryker"}, {"пума", "puma"}, {"мардер", "marder"},
        {"ариете", "ariete"}, {"рено", "renault"}, {"мтлб", "mtlb"}
    };
    for (const auto& [native, latin] : aliases) term.replace(native, latin);
    // Model names are often typed with mixed alphabets, e.g. Т-72Б3 and T-72B3.
    term.replace(u'т', u't').replace(u'м', u'm').replace(u'б', u'b').replace(u'в', u'v').replace(u'а', u'a');
    static const QRegularExpression punctuation(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return term.remove(punctuation);
}

} // namespace

int chooseLibraryPreset(QWidget* parent, const std::vector<Preset>& presets, int current) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Библиотека двигателей"));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setSizeGripEnabled(true);
    dialog.resize(QSize(530, 475).boundedTo(dialog.screen()->availableGeometry().size() - QSize(32, 64)));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);
    auto* query = new QLineEdit;
    query->setObjectName(QStringLiteral("presetSearch"));
    query->setPlaceholderText(QStringLiteral("Поиск по машине, двигателю или названию в игре"));
    query->setAccessibleName(QStringLiteral("Поиск по библиотеке"));
    query->setClearButtonEnabled(true);
    layout->addWidget(query);
    auto* count = new QLabel;
    count->setObjectName(QStringLiteral("caption"));
    layout->addWidget(count);
    auto* results = new QStackedWidget;
    auto* list = new QListWidget;
    list->setObjectName(QStringLiteral("presetResults"));
    list->setAccessibleName(QStringLiteral("Найденные пресеты"));
    list->setUniformItemSizes(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results->addWidget(list);
    auto* empty = new QLabel(QStringLiteral("Совпадений нет"));
    empty->setAlignment(Qt::AlignCenter);
    results->addWidget(empty);
    layout->addWidget(results, 1);
    auto* detail = new QLabel;
    detail->setObjectName(QStringLiteral("hint"));
    detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail->setWordWrap(true);
    detail->setTextFormat(Qt::PlainText);
    layout->addWidget(detail);
    auto* buttons = new QDialogButtonBox;
    auto* choose = buttons->addButton(QStringLiteral("Выбрать"), QDialogButtonBox::AcceptRole);
    choose->setObjectName(QStringLiteral("primary"));
    buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    QHash<QString, QStringList> names, ids;
    QFile catalog(QStringLiteral(":/library/fleet.json"));
    if (catalog.open(QIODevice::ReadOnly)) {
        const auto rows = QJsonDocument::fromJson(catalog.readAll()).array();
        for (const auto& entry : rows) {
            const auto row = entry.toObject();
            const auto preset = row.value("preset").toString();
            names[preset] << row.value("name").toString();
            ids[preset] << row.value("id").toString() << row.value("slot").toString();
        }
    }
    QStringList keys;
    for (const auto& p : presets)
        keys << searchKey(p.name + ' ' + p.description + ' ' + names.value(p.name).join(' ') + ' ' + ids.value(p.name).join(' '));
    std::vector<int> order(presets.size());
    std::iota(order.begin(), order.end(), 0);
    QCollator collator(QLocale{QLocale::Russian});
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return collator.compare(presets[a].name, presets[b].name) < 0;
    });
    auto filter = [&] {
        auto words = query->text().simplified().split(' ', Qt::SkipEmptyParts);
        for (auto& word : words) word = searchKey(word);
        const int previous = list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : current;
        list->clear();
        QListWidgetItem* selected = nullptr;
        for (int i : order) {
            if (!std::all_of(words.begin(), words.end(), [&](const QString& word) { return keys[i].contains(word); })) continue;
            auto* item = new QListWidgetItem(presets[i].name, list);
            item->setData(Qt::UserRole, i);
            if (i == previous) selected = item;
        }
        const int found = list->count();
        count->setText(words.isEmpty() ? QStringLiteral("Все пресеты · %1").arg(found) :
            QStringLiteral("Найдено %1 из %2").arg(found).arg(keys.size()));
        results->setCurrentIndex(found ? 0 : 1);
        choose->setEnabled(found > 0);
        if (found) {
            list->setCurrentItem(selected ? selected : list->item(0));
            list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtCenter);
        } else {
            detail->clear();
        }
    };
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem* item) {
        if (!item) return;
        const auto& p = presets[item->data(Qt::UserRole).toInt()];
        const auto matches = names.value(p.name);
        detail->setText(matches.isEmpty() ? p.description :
            p.description + QStringLiteral("\nВ игре · ") + matches.join(QStringLiteral(", ")));
    });
    QObject::connect(query, &QLineEdit::textChanged, &dialog, filter);
    for (const int step : {-1, 1}) {
        auto* arrow = new QShortcut(QKeySequence(step < 0 ? Qt::Key_Up : Qt::Key_Down), query);
        arrow->setContext(Qt::WidgetShortcut);
        QObject::connect(arrow, &QShortcut::activated, list, [list, step] {
            const int row = list->currentRow() + step;
            if (row >= 0 && row < list->count()) list->setCurrentRow(row);
        });
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    choose->setDefault(true);
    filter();
    query->setFocus();
    return dialog.exec() == QDialog::Accepted && list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : -1;
}
} // namespace esl
