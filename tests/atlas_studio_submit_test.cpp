// Регрессия: пункт меню «Отправить в каталог» доступен и открывает диалог
// отправки заявки на каталог неподтверждённых плагинов с обязательным исходным кодом.
#include "studio/atlas_studio_window.h"

#include <QAction>
#include <QApplication>
#include <QMenuBar>
#include <QMenu>

#include <cassert>
#include <iostream>

namespace {

QAction *findMenuAction(QMenuBar *menuBar, const QString &menuFragment, const QString &actionFragment)
{
    for (QMenu *menu : menuBar->findChildren<QMenu *>()) {
        if (!menu->title().contains(menuFragment)) {
            continue;
        }
        for (QAction *entry : menu->actions()) {
            if (entry->text().contains(actionFragment)) {
                return entry;
            }
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Atlas Studio Submit Test"));

    atlas::studio::AtlasStudioWindow window;
    window.resize(1024, 700);
    window.show();
    application.processEvents();

    auto *menuBar = window.menuBar();
    assert(menuBar != nullptr);
    assert(menuBar->actions().size() >= 3);

    QAction *submitAction = findMenuAction(menuBar, QStringLiteral("Сборка"), QStringLiteral("Отправить"));
    if (submitAction == nullptr) {
        std::cerr << "FAIL: menu item for catalog submission not found\n";
        return 1;
    }

    if (!submitAction->isEnabled()) {
        std::cerr << "FAIL: catalog submission action is disabled without an opened project\n";
        return 1;
    }

    std::cout << "PASS: catalog submission action present and enabled\n";
    return 0;
}
