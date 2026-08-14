#include "studio_test_runner.h"

#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace atlas::studio {
namespace {

bool isExcludedDirectory(const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    static const QSet<QString> excluded = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral("build"),
        QStringLiteral("cmake-build-debug"), QStringLiteral("cmake-build-release"),
        QStringLiteral("dist"), QStringLiteral("out"), QStringLiteral(".orvexa-test")
    };
    for (const QString &part : parts) {
        if (excluded.contains(part)) {
            return true;
        }
    }
    return false;
}

QString relativeArchivePath(const QString &root, const QString &path)
{
    const QString relative = QDir(root).relativeFilePath(path);
    return QDir::fromNativeSeparators(relative);
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Не удалось записать %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Не удалось завершить запись %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace

PluginTestResult StudioTestRunner::run(const PluginTestRequest &request)
{
    PluginTestResult result;
    const QFileInfo projectInfo(request.projectDirectory);
    if (!projectInfo.exists() || !projectInfo.isDir()) {
        result.diagnostics.append(QStringLiteral("Каталог проекта не существует"));
        return result;
    }

    const QString projectRoot = projectInfo.canonicalFilePath();
    QFile projectFile(QDir(projectRoot).filePath(QStringLiteral("atlas-project.json")));
    if (!projectFile.open(QIODevice::ReadOnly)) {
        result.diagnostics.append(QStringLiteral("Не удалось открыть atlas-project.json: %1")
                                      .arg(projectFile.errorString()));
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument projectDocument = QJsonDocument::fromJson(projectFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !projectDocument.isObject()) {
        result.diagnostics.append(QStringLiteral("Некорректный atlas-project.json: %1")
                                      .arg(parseError.errorString()));
        return result;
    }

    const QString sandbox = QDir(QDir::tempPath()).filePath(
        QStringLiteral("orvexa-studio-test-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(sandbox)) {
        result.diagnostics.append(QStringLiteral("Не удалось создать изолированный каталог теста"));
        return result;
    }
    result.sandboxDirectory = sandbox;
    result.sandboxReady = true;

    QVector<packager::AtlasPackageFile> packageFiles;
    QStringList sourceFiles;
    QDirIterator iterator(projectRoot, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = QFileInfo(iterator.next()).absoluteFilePath();
        const QFileInfo sourceInfo(sourcePath);
        if (isExcludedDirectory(sourceInfo.absolutePath())) {
            continue;
        }
        const QString relative = relativeArchivePath(projectRoot, sourcePath);
        if (relative.isEmpty() || relative == QStringLiteral("atlas-project.json")) {
            continue;
        }
        if (sourceInfo.suffix().compare(QStringLiteral("atlas"), Qt::CaseInsensitive) == 0) {
            sourceFiles.append(sourcePath);
        }
    }
    sourceFiles.sort(Qt::CaseSensitive);
    if (sourceFiles.isEmpty()) {
        result.diagnostics.append(QStringLiteral("В проекте не найдено исходников .atlas"));
        QDir(sandbox).removeRecursively();
        return result;
    }

    int programIndex = 0;
    for (const QString &sourcePath : sourceFiles) {
        QFile sourceFile(sourcePath);
        if (!sourceFile.open(QIODevice::ReadOnly)) {
            result.diagnostics.append(QStringLiteral("Не удалось открыть исходник %1: %2")
                                          .arg(relativeArchivePath(projectRoot, sourcePath), sourceFile.errorString()));
            QDir(sandbox).removeRecursively();
            return result;
        }
        const QString name = QFileInfo(sourcePath).completeBaseName();
        const auto compiled = runtime::AtlasCodeCompiler::compile(name, QString::fromUtf8(sourceFile.readAll()));
        if (!compiled.success) {
            for (const auto &diagnostic : compiled.diagnostics) {
                result.diagnostics.append(QStringLiteral("%1:%2: %3")
                                              .arg(relativeArchivePath(projectRoot, sourcePath))
                                              .arg(diagnostic.line)
                                              .arg(diagnostic.message));
            }
            QDir(sandbox).removeRecursively();
            return result;
        }
        QString encodeError;
        const QByteArray encoded = runtime::AtlasCodeCompiler::encodeAtbc(compiled.program, &encodeError);
        if (encoded.isEmpty() || !encodeError.isEmpty()) {
            result.diagnostics.append(QStringLiteral("%1: ошибка кодирования ATBC: %2")
                                          .arg(relativeArchivePath(projectRoot, sourcePath), encodeError));
            QDir(sandbox).removeRecursively();
            return result;
        }
        runtime::AtlasCodeProgram decoded;
        QString decodeError;
        if (!runtime::AtlasCodeCompiler::decodeAtbc(encoded, &decoded, &decodeError)) {
            result.diagnostics.append(QStringLiteral("%1: ошибка проверки ATBC: %2")
                                          .arg(relativeArchivePath(projectRoot, sourcePath), decodeError));
            QDir(sandbox).removeRecursively();
            return result;
        }
        const QString archivePath = QStringLiteral("program/%1.atbc").arg(programIndex == 0 ? QStringLiteral("main") : QStringLiteral("program-%1").arg(programIndex));
        const QString stagedPath = QDir(sandbox).filePath(archivePath);
        QString writeError;
        if (!QDir().mkpath(QFileInfo(stagedPath).absolutePath()) || !writeBytes(stagedPath, encoded, &writeError)) {
            result.diagnostics.append(writeError);
            QDir(sandbox).removeRecursively();
            return result;
        }
        packageFiles.append({stagedPath, archivePath, true});
        ++programIndex;
    }
    result.sourceCompiled = true;
    result.atbcValidated = true;

    QDirIterator resources(projectRoot, QDir::Files, QDirIterator::Subdirectories);
    while (resources.hasNext()) {
        const QString sourcePath = QFileInfo(resources.next()).absoluteFilePath();
        const QFileInfo sourceInfo(sourcePath);
        if (isExcludedDirectory(sourceInfo.absolutePath())) {
            continue;
        }
        const QString relative = relativeArchivePath(projectRoot, sourcePath);
        if (relative.isEmpty() || relative == QStringLiteral("atlas-project.json") ||
            sourceInfo.suffix().compare(QStringLiteral("atlas"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        packageFiles.append({sourcePath, QStringLiteral("resources/%1").arg(relative), false});
    }

    packager::AtlasPackageRequest packageRequest;
    packageRequest.projectDirectory = projectRoot;
    packageRequest.outputPath = request.outputPackagePath.isEmpty()
        ? QDir(sandbox).filePath(QStringLiteral("plugin.atp"))
        : QFileInfo(request.outputPackagePath).absoluteFilePath();
    packageRequest.files = packageFiles;
    const auto packageResult = packager::AtlasPackageBuilder::build(packageRequest);
    result.diagnostics.append(packageResult.diagnostics);
    if (!packageResult.success) {
        QDir(sandbox).removeRecursively();
        return result;
    }
    result.packageBuilt = true;
    result.packagePath = packageResult.outputPath;
    result.success = true;
    result.diagnostics.append(QStringLiteral("Локальная проверка завершена: исходники скомпилированы, ATBC проверен, пакет собран."));
    QDir(sandbox).removeRecursively();
    return result;
}

} // namespace atlas::studio
