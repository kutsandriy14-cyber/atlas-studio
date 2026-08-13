#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace atlas::packager {

// A concrete, already compiled or copied archive entry. Paths are portable and
// always relative to the root of the resulting .atp archive.
struct AtlasPackageFile
{
    QString sourcePath;
    QString archivePath;
    bool program = false;
};

struct AtlasPackageRequest
{
    QString projectDirectory;
    // Legacy one-program input. New callers use files; if files is empty,
    // atbcPath is archived as program/main.atbc for backward compatibility.
    QString atbcPath;
    QString outputPath;
    QVector<AtlasPackageFile> files;
};

struct AtlasPackageResult
{
    bool success = false;
    QString outputPath;
    QStringList diagnostics;
};

// Builds schema-2 Atlas Code packages. The service accepts explicitly
// declared programs and non-native resources; raw .atlas source is rejected.
class AtlasPackageBuilder final
{
public:
    static AtlasPackageResult build(const AtlasPackageRequest &request);
};

} // namespace atlas::packager
