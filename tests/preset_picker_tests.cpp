#include "app/preset_library.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTest>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    require(app.arguments().size() == 2, "Supply the factory preset directory");
    QDir dir(app.arguments()[1]);
    std::vector<esl::Preset> presets;
    for (const auto& file : dir.entryList({"*.json"}, QDir::Files)) {
        esl::Preset p;
        require(esl::loadPreset(dir.filePath(file), p).isEmpty(), "Cannot read factory preset");
        presets.push_back(std::move(p));
    }
    require(!presets.empty(), "No presets to browse");
    presets.push_back({QStringLiteral("Custom V6"), QStringLiteral("Imported test preset"), {}});
    const int imported = static_cast<int>(presets.size()) - 1;
    QWidget parent;
    parent.show();
    parent.activateWindow();
    require(QTest::qWaitForWindowActive(&parent), "Test window did not become active");
    QTimer::singleShot(30, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog && dialog->isModal(), "The picker must be modal");
        auto* query = dialog->findChild<QLineEdit*>();
        auto* list = dialog->findChild<QListWidget*>();
        auto* choose = dialog->findChild<QPushButton*>("primary");
        require(query && list && choose, "Missing search, results or confirmation control");
        require(query->hasFocus(), "Search must have initial focus");
        require(list->count() == int(presets.size()) && list->verticalScrollBar()->maximum() > 0,
                "The full library must fit in a scrolling viewport");
        require(list->currentItem()->data(Qt::UserRole).toInt() == imported, "Lost the current or imported preset");
        query->setText(QStringLiteral("unmatched-07129"));
        require(!choose->isEnabled() && !list->currentItem(), "An empty search must not be selectable");
        QTest::keyClick(query, Qt::Key_Return);
        require(dialog->isVisible(), "An empty search closed the picker");
        query->setText(QStringLiteral("Т-90М"));
        require(list->findItems("T-90M", Qt::MatchExactly).size() == 1, "Cyrillic model search failed");
        query->setText(QStringLiteral("ошкош"));
        require(list->count() > 1, "Expected several Oshkosh variants");
        QTest::keyClick(query, Qt::Key_Down);
        require(list->currentRow() == 1 && query->hasFocus(), "Arrow navigation interrupted search");
        QTest::keyClick(query, Qt::Key_Up);
        require(list->currentRow() == 0, "Up arrow did not move the highlight");
        query->setText(QStringLiteral("TR-92 Rassvet"));
        require(list->count() == 1 && list->currentItem()->text() == "BTR-90", "Game alias search failed");
        QTest::keyClick(query, Qt::Key_Return);
    });
    const int selected = esl::chooseLibraryPreset(&parent, presets, imported);
    require(selected >= 0 && presets.at(selected).name == "BTR-90", "Enter selected the wrong preset after sorting");
    QTimer::singleShot(30, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        require(dialog, "Picker did not reopen");
        auto* query = dialog->findChild<QLineEdit*>();
        query->setText(QStringLiteral("Custom V6"));
        QTest::keyClick(query, Qt::Key_Escape);
    });
    require(esl::chooseLibraryPreset(&parent, presets, selected) == -1, "Cancel committed a preset change");
    return 0;
}
