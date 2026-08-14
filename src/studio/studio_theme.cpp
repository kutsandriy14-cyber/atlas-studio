#include "studio_theme.h"

#include <QFile>
#include <QMutex>
#include <QPainter>
#include <QSvgRenderer>

namespace atlas::studio {

QMutex StudioTheme::s_mutex;
QHash<QString, QPixmap> StudioTheme::s_cache;

QColor StudioTheme::background() { return QColor(QStringLiteral("#1e1e1e")); }
QColor StudioTheme::sidebar() { return QColor(QStringLiteral("#252526")); }
QColor StudioTheme::border() { return QColor(QStringLiteral("#3c3c3c")); }
QColor StudioTheme::accent() { return QColor(QStringLiteral("#178f87")); }
QColor StudioTheme::accentLight() { return QColor(QStringLiteral("#5ee0d1")); }
QColor StudioTheme::selection() { return QColor(QStringLiteral("#37373d")); }
QColor StudioTheme::focus() { return QColor(QStringLiteral("#007acc")); }
QColor StudioTheme::success() { return QColor(QStringLiteral("#89d185")); }
QColor StudioTheme::error() { return QColor(QStringLiteral("#f14c4c")); }
QColor StudioTheme::text() { return QColor(QStringLiteral("#d4d4d4")); }
QColor StudioTheme::textMuted() { return QColor(QStringLiteral("#a0a0a0")); }

QPixmap StudioTheme::cached(const QString &key)
{
    const QMutexLocker locker(&s_mutex);
    return s_cache.value(key);
}

QIcon StudioTheme::icon(const QString &name, int size)
{
    if (size <= 0) {
        return QIcon();
    }
    const QString key = QStringLiteral("%1@%2").arg(name).arg(size);
    QPixmap pixmap = cached(key);
    if (pixmap.isNull()) {
        pixmap = raster(name, size);
        if (!pixmap.isNull()) {
            const QMutexLocker locker(&s_mutex);
            s_cache.insert(key, pixmap);
        }
    }
    return QIcon(pixmap);
}

QPixmap StudioTheme::raster(const QString &name, int size)
{
    const QString path = QStringLiteral(":/orvexa-studio/icons/%1.svg").arg(name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QPixmap();
    }
    const QByteArray svgBytes = file.readAll();
    file.close();

    QSvgRenderer renderer(svgBytes);
    if (!renderer.isValid()) {
        return QPixmap();
    }
    QPixmap pixmap(QSize(size, size));
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRect(0, 0, size, size));
    return pixmap;
}

} // namespace atlas::studio
