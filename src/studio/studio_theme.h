#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

#include <QMutex>
#include <QHash>
#include <QString>

namespace atlas::studio {

// Единый источник иконок и фирменных цветов Atlas Studio.
// SVG рендерится в растр через QSvgRenderer и кэшируется; в Windows CI
// вместе с Qt5Svg.dll всегда поставляется этот модуль.
class StudioTheme final
{
public:
    static QIcon icon(const QString &name, int size = 16);

    // Фирменная палитра (VS Code Dark + бирюзовый Atlas).
    static QColor background();        // #1e1e1e
    static QColor sidebar();           // #252526
    static QColor border();            // #3c3c3c
    static QColor accent();            // #178f87
    static QColor accentLight();       // #5ee0d1
    static QColor selection();         // #37373d
    static QColor focus();             // #007acc
    static QColor success();           // #89d185
    static QColor error();             // #f14c4c
    static QColor text();              // #d4d4d4
    static QColor textMuted();         // #a0a0a0

private:
    static QPixmap raster(const QString &name, int size);
    static QPixmap cached(const QString &key);

    static QMutex s_mutex;
    static QHash<QString, QPixmap> s_cache;
};

} // namespace atlas::studio
