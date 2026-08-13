#pragma once

#include <QString>
#include <QStringList>

namespace atlas::packager {

struct AtlasPackageRequest
{
    QString projectDirectory;
    QString atbcPath;
    QString outputPath;
};

struct AtlasPackageResult
{
    bool success = false;
    QString outputPath;
    QStringList diagnostics;
};

// Standalone schema 2 .atp writer. It intentionally produces only manifest.json
// and program/main.atbc: source files are never copied into the package.
class AtlasPackageBuilder final
{
public:
    static AtlasPackageResult build(const AtlasPackageRequest &request);
};

} // namespace atlas::packager
