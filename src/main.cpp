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
constexpr auto kStudioVersion = "0.1.0";

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

    atlas::studio::AtlasStudioWindow window;
    window.show();

    if (!application.arguments().contains(QStringLiteral("--no-update-check"))) {
        QTimer::singleShot(1250, &application, []() { scheduleUpdateCheck(); });
    }

    return application.exec();
}
