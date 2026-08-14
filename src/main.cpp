#include "services/update_service.h"
#include "studio/atlas_studio_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>

namespace {

constexpr auto kStudioRepository = "kutsandriy14-cyber/atlas-studio";
constexpr auto kStudioVersion = "0.2.0";

QString studioSettingsDirectory()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!directory.isEmpty() && QDir().mkpath(directory)) return QDir::cleanPath(directory);
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("settings")));
}

void scheduleUpdateCheck()
{
    const QString updaterPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AtlasUpdater.exe"));
    if (!QFileInfo::exists(updaterPath)) return;

    QString error;
    atlas::UpdateService::launchCheckProcess(QString::fromLatin1(kStudioRepository),
                                              QString::fromLatin1(kStudioVersion),
                                              QCoreApplication::applicationDirPath(),
                                              studioSettingsDirectory(), &error);
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Atlas Launcher"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("atlaslauncher.org"));
    QCoreApplication::setApplicationName(QStringLiteral("Atlas Studio"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kStudioVersion));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#studioRoot { background: #1e1e1e; color: #d4d4d4; }
        QMenuBar { background: #252526; color: #d4d4d4; border-bottom: 1px solid #3c3c3c; }
        QMenuBar::item { padding: 5px 9px; background: transparent; }
        QMenuBar::item:selected, QMenu::item:selected { background: #094771; }
        QMenu { background: #252526; color: #d4d4d4; border: 1px solid #454545; }
        QMenu::item { padding: 6px 24px 6px 20px; }
        QWidget#commandBar { background: #252526; border-bottom: 1px solid #3c3c3c; }
        QLabel#studioBrand { color: #5ee0d1; font-weight: 700; letter-spacing: 1px; }
        QLabel#projectCaption, QLabel#explorerHint, QLabel#metadataIntro, QLabel#permissionHint { color: #a0a0a0; }
        QLabel#brandIcon { background: transparent; }
        QLabel#problemsBadge, QLabel#referenceBadge { background: transparent; padding: 0 6px 0 4px; }
        QWidget#explorerPane { background: #252526; border-right: 1px solid #3c3c3c; }
        QLabel#explorerTitle { color: #d4d4d4; font-weight: 700; letter-spacing: 1.5px; font-size: 10px; }
        QPushButton#newProjectButton, QPushButton#openProjectButton { width: 100%; text-align: left; }
        QPushButton#newProjectButton:hover, QPushButton#openProjectButton:hover { background: #2a2d2e; border-color: #007acc; }
        QTreeWidget#projectTree { background: #252526; border: 0; color: #cccccc; outline: 0; }
        QTreeWidget#projectTree::item { height: 24px; padding-left: 3px; }
        QTreeWidget#projectTree::item:selected { background: #37373d; color: #ffffff; }
        QTabWidget::pane { border: 0; background: #1e1e1e; }
        QTabBar { background: #252526; }
        QTabBar::tab { background: #2d2d2d; color: #969696; border: 0; border-right: 1px solid #1e1e1e; padding: 8px 16px; min-width: 105px; margin: 0; }
        QTabBar::tab:hover { color: #ffffff; background: #2a2d2e; }
        QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-top: 1px solid #007acc; }
        QPlainTextEdit#atlasCodeEditor { background: #1e1e1e; color: #d4d4d4; border: 0; selection-background-color: #264f78; }
        QTextEdit { background: #1e1e1e; color: #d4d4d4; border: 0; selection-background-color: #264f78; }
        QScrollArea#metadataScroll { background: #1e1e1e; }
        QScrollArea#metadataScroll > QWidget > QWidget { background: #1e1e1e; }
        QGroupBox { color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 3px; margin-top: 12px; padding: 12px 8px 8px 8px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; }
        QLineEdit, QPlainTextEdit#packageFilesEditor, QComboBox { background: #3c3c3c; color: #f0f0f0; border: 1px solid #555555; border-radius: 2px; padding: 5px; selection-background-color: #264f78; }
        QLineEdit:focus, QPlainTextEdit#packageFilesEditor:focus, QComboBox:focus { border: 1px solid #007fd4; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox QAbstractItemView { background: #3c3c3c; color: #f0f0f0; selection-background-color: #094771; }
        QCheckBox { color: #d4d4d4; spacing: 7px; }
        QPushButton { background: #333333; color: #f0f0f0; border: 1px solid #4a4a4a; border-radius: 3px; padding: 6px 11px; }
        QPushButton:hover { background: #3e3e42; }
        QPushButton:pressed { background: #094771; }
        QPushButton#compileButton, QPushButton#packageButton { background: #0e639c; border-color: #1177bb; color: #ffffff; icon-size: 16px; }
        QPushButton#compileButton, QPushButton#packageButton, QPushButton#showPackageButton { padding: 6px 14px; }
        QPushButton#compileButton:hover, QPushButton#packageButton:hover { background: #1177bb; }
        QStatusBar { background: #007acc; color: #ffffff; }
        QStatusBar QLabel { color: #ffffff; background: transparent; }
        QStatusBar::item { border: 0; }
        QSplitter::handle { background: #3c3c3c; }
        QSplitter::handle:hover { background: #007acc; }
        QScrollBar:vertical { background: #1e1e1e; width: 12px; }
        QScrollBar::handle:vertical { background: #424242; min-height: 24px; border-radius: 4px; }
    )"));

    atlas::studio::AtlasStudioWindow window;
    window.show();

    if (!application.arguments().contains(QStringLiteral("--no-update-check"))) {
        QTimer::singleShot(1250, &application, []() { scheduleUpdateCheck(); });
    }

    return application.exec();
}
