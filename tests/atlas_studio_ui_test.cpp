#include "studio/atlas_studio_window.h"

#include <QApplication>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTextEdit>
#include <QTreeWidget>

#include <iostream>

namespace {

bool require(const bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    atlas::studio::AtlasStudioWindow window;

    const auto *tree = window.findChild<QTreeWidget *>(QStringLiteral("projectTree"));
    const auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("atlasCodeEditor"));
    const auto *editorTabs = window.findChild<QTabWidget *>(QStringLiteral("editorTabs"));
    const auto *bottomTabs = window.findChild<QTabWidget *>(QStringLiteral("bottomTabs"));
    const auto *reference = window.findChild<QTextEdit *>(QStringLiteral("atlasCodeReference"));

    bool ok = true;
    ok &= require(tree != nullptr, "the project explorer must exist");
    ok &= require(editor != nullptr, "the Atlas Code editor must exist");
    ok &= require(editorTabs != nullptr && editorTabs->count() == 2,
                  "the editor must provide source and package tabs");
    ok &= require(bottomTabs != nullptr && bottomTabs->count() == 2,
                  "the bottom panel must provide problems and reference tabs");
    ok &= require(reference != nullptr && reference->toPlainText().contains(QStringLiteral("Atlas Code")),
                  "the embedded Atlas Code reference must be available");
    ok &= require(window.minimumSize().width() <= 820 && window.minimumSize().height() <= 560,
                  "the minimum window size must stay usable on small screens");

    return ok ? 0 : 1;
}
