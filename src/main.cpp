#include "studio/atlas_studio_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Atlas Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    atlas::studio::AtlasStudioWindow window;
    window.show();
    return application.exec();
}
