#include "atlas_package_builder.h"

#include "core/atlas_code_compiler.h"
#include "miniz/miniz.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QVersionNumber>

namespace atlas::packager {
namespace {

constexpr qint64 kMaximumAtbcBytes = 256LL * 1024;
constexpr qint64 kMaximumResourceBytes = 8LL * 1024 * 1024;
constexpr qint64 kMaximumPackageInputBytes = 64LL * 1024 * 1024;
constexpr int kMaximumPackageFiles = 128;
constexpr int kMaximumPrograms = 32;
const QSet<QString> kAllowedPermissions = {
    QStringLiteral("servers.control"),
    QStringLiteral("servers.console"),
    QStringLiteral("files.plugin-data"),
    QStringLiteral("network.metadata")
};

struct PreparedFile {
    QString sourcePath;
    QString archivePath;
    QByteArray content;
    QString sha256;
    bool program = false;
};

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList result;
    if (!value.isArray()) {
        return result;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString()) {
            return {};
        }
        const QString text = item.toString().trimmed();
        if (text.isEmpty() || result.contains(text)) {
            return {};
        }
        result.append(text);
    }
    return result;
}

bool readProject(const QString &projectDirectory, QJsonObject *project, QStringList *diagnostics)
{
    QFile input(QDir(projectDirectory).filePath(QStringLiteral("atlas-project.json")));
    if (!input.open(QIODevice::ReadOnly)) {
        diagnostics->append(QStringLiteral("Не удалось открыть atlas-project.json: %1").arg(input.errorString()));
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        diagnostics->append(QStringLiteral("Некорректный atlas-project.json: %1").arg(error.errorString()));
        return false;
    }
    *project = document.object();
    return true;
}

bool validId(const QString &id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z][a-z0-9.-]{2,127}$"));
    return expression.match(id).hasMatch();
}

bool validVersion(const QString &version)
{
    static const QRegularExpression expression(
        QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$"));
    return expression.match(version).hasMatch();
}

bool isSafeArchivePath(const QString &path)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    return !normalized.isEmpty() && normalized != QStringLiteral(".") && normalized != QStringLiteral("..") &&
           !QDir::isAbsolutePath(normalized) && !normalized.startsWith(QStringLiteral("../")) &&
           !normalized.contains(QStringLiteral("/../")) && !normalized.startsWith(QLatin1Char('/'));
}

bool isInsideProject(const QString &projectDirectory, const QFileInfo &fileInfo)
{
    const QString root = QFileInfo(projectDirectory).canonicalFilePath();
    const QString file = fileInfo.canonicalFilePath();
    if (root.isEmpty() || file.isEmpty()) {
        return false;
    }
    const QString prefix = root.endsWith(QDir::separator()) ? root : root + QDir::separator();
    return file.startsWith(prefix);
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QJsonArray entriesToJson(const QVector<PreparedFile> &files, bool programs)
{
    QJsonArray entries;
    for (const PreparedFile &file : files) {
        if (file.program == programs) {
            entries.append(QJsonObject{{QStringLiteral("path"), file.archivePath},
                                       {QStringLiteral("sha256"), file.sha256}});
        }
    }
    return entries;
}

bool writeZip(const QString &path, const QByteArray &manifest, const QVector<PreparedFile> &files, QString *error)
{
    const QFileInfo outputInfo(path);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        *error = QStringLiteral("Не удалось создать каталог назначения .atp");
        return false;
    }

    QTemporaryFile temporary(outputInfo.absolutePath() + QStringLiteral("/.atlas-studio-XXXXXX.atp"));
    temporary.setAutoRemove(false);
    if (!temporary.open()) {
        *error = QStringLiteral("Не удалось подготовить временный .atp: %1").arg(temporary.errorString());
        return false;
    }
    const QString temporaryPath = temporary.fileName();
    temporary.close();

    mz_zip_archive archive{};
    bool ok = mz_zip_writer_init_file(&archive, temporaryPath.toUtf8().constData(), 0) != 0;
    if (ok) {
        ok = mz_zip_writer_add_mem(&archive, "manifest.json", manifest.constData(),
                                   static_cast<size_t>(manifest.size()), MZ_BEST_COMPRESSION) != 0;
    }
    for (const PreparedFile &file : files) {
        if (ok) {
            ok = mz_zip_writer_add_mem(&archive, file.archivePath.toUtf8().constData(), file.content.constData(),
                                       static_cast<size_t>(file.content.size()), MZ_BEST_COMPRESSION) != 0;
        }
    }
    if (ok) {
        ok = mz_zip_writer_finalize_archive(&archive) != 0;
    }
    mz_zip_writer_end(&archive);

    if (!ok) {
        QFile::remove(temporaryPath);
        *error = QStringLiteral("Не удалось создать ZIP-архив .atp");
        return false;
    }

    QFile::remove(path);
    if (!QFile::rename(temporaryPath, path)) {
        QFile::remove(temporaryPath);
        *error = QStringLiteral("Не удалось переместить готовый .atp в каталог назначения");
        return false;
    }
    return true;
}

} // namespace

AtlasPackageResult AtlasPackageBuilder::build(const AtlasPackageRequest &request)
{
    AtlasPackageResult result;
    const QFileInfo projectInfo(request.projectDirectory);
    if (!projectInfo.exists() || !projectInfo.isDir()) {
        result.diagnostics.append(QStringLiteral("Каталог проекта Atlas Code недоступен"));
        return result;
    }
    if (request.outputPath.trimmed().isEmpty()) {
        result.diagnostics.append(QStringLiteral("Не задан путь готового .atp"));
        return result;
    }

    QJsonObject project;
    if (!readProject(request.projectDirectory, &project, &result.diagnostics)) {
        return result;
    }
    const QString id = project.value(QStringLiteral("id")).toString().trimmed();
    const QString name = project.value(QStringLiteral("name")).toString().trimmed();
    const QString version = project.value(QStringLiteral("version")).toString().trimmed();
    const QString publisher = project.value(QStringLiteral("publisher")).toString().trimmed();
    const QString description = project.value(QStringLiteral("description")).toString().trimmed();
    const QString homepage = project.value(QStringLiteral("homepage")).toString().trimmed();
    QString minimumLauncherVersion = project.value(QStringLiteral("minimumLauncherVersion"))
                                               .toString(QStringLiteral("0.4.0")).trimmed();
    const QStringList permissions = jsonStringList(project.value(QStringLiteral("permissions")));
    const QStringList pages = jsonStringList(project.value(QStringLiteral("pages")));
    const QStringList actions = jsonStringList(project.value(QStringLiteral("actions")));

    if (!validId(id)) {
        result.diagnostics.append(QStringLiteral("ID должен состоять из строчных латинских букв, цифр, точек и дефисов"));
    }
    if (name.isEmpty() || name.size() > 120 || !validVersion(version) || !validVersion(minimumLauncherVersion)) {
        result.diagnostics.append(QStringLiteral("Название или версия проекта некорректны"));
    }
    if (description.size() > 2000 || publisher.size() > 120 || homepage.size() > 512) {
        result.diagnostics.append(QStringLiteral("Описание, автор или ссылка проекта слишком длинные"));
    }
    for (const QString &permission : permissions) {
        if (!kAllowedPermissions.contains(permission)) {
            result.diagnostics.append(QStringLiteral("Неподдерживаемое разрешение: %1").arg(permission));
        }
    }
    if (pages.size() > 16 || actions.size() > 64) {
        result.diagnostics.append(QStringLiteral("Превышен лимит деклараций страниц или действий"));
    }
    if (!result.diagnostics.isEmpty()) {
        return result;
    }

    QVector<AtlasPackageFile> requestedFiles = request.files;
    if (requestedFiles.isEmpty()) {
        requestedFiles.append({request.atbcPath, QStringLiteral("program/main.atbc"), true});
    }
    if (requestedFiles.size() > kMaximumPackageFiles) {
        result.diagnostics.append(QStringLiteral("Превышен лимит 128 явно объявленных файлов пакета"));
        return result;
    }

    QVector<PreparedFile> files;
    files.reserve(requestedFiles.size());
    QSet<QString> archivePaths;
    qint64 totalBytes = 0;
    int programCount = 0;
    for (const AtlasPackageFile &requested : requestedFiles) {
        const QString archivePath = QDir::cleanPath(QDir::fromNativeSeparators(requested.archivePath.trimmed()));
        const QFileInfo sourceInfo(requested.sourcePath);
        const QString pathKey = archivePath.toCaseFolded();
        if (!isSafeArchivePath(archivePath) || archivePath == QLatin1String("manifest.json") ||
            archivePaths.contains(pathKey) || !sourceInfo.isFile() || sourceInfo.isSymLink() ||
            !isInsideProject(request.projectDirectory, sourceInfo)) {
            result.diagnostics.append(QStringLiteral("Недопустимый объявленный файл пакета: %1").arg(requested.archivePath));
            continue;
        }
        if (sourceInfo.suffix().compare(QStringLiteral("atlas"), Qt::CaseInsensitive) == 0 ||
            sourceInfo.suffix().compare(QStringLiteral("dll"), Qt::CaseInsensitive) == 0) {
            result.diagnostics.append(QStringLiteral("Исходники Atlas Code и DLL нельзя включать в .atp: %1").arg(sourceInfo.fileName()));
            continue;
        }
        if (requested.program && (!archivePath.startsWith(QStringLiteral("program/")) ||
                                  !archivePath.endsWith(QStringLiteral(".atbc"), Qt::CaseInsensitive))) {
            result.diagnostics.append(QStringLiteral("Программа должна быть ATBC в каталоге program/: %1").arg(archivePath));
            continue;
        }
        if (!requested.program && archivePath.endsWith(QStringLiteral(".atbc"), Qt::CaseInsensitive)) {
            result.diagnostics.append(QStringLiteral("ATBC должен быть объявлен как программа: %1").arg(archivePath));
            continue;
        }
        const qint64 maximum = requested.program ? kMaximumAtbcBytes : kMaximumResourceBytes;
        if (sourceInfo.size() <= 0 || sourceInfo.size() > maximum || totalBytes > kMaximumPackageInputBytes - sourceInfo.size()) {
            result.diagnostics.append(QStringLiteral("Размер объявленного файла недопустим: %1").arg(archivePath));
            continue;
        }
        QFile input(sourceInfo.absoluteFilePath());
        if (!input.open(QIODevice::ReadOnly)) {
            result.diagnostics.append(QStringLiteral("Не удалось прочитать %1: %2").arg(archivePath, input.errorString()));
            continue;
        }
        const QByteArray content = input.readAll();
        if (content.size() != sourceInfo.size()) {
            result.diagnostics.append(QStringLiteral("Не удалось полностью прочитать %1").arg(archivePath));
            continue;
        }
        if (requested.program) {
            runtime::AtlasCodeProgram program;
            QString decodeError;
            if (!runtime::AtlasCodeCompiler::decodeAtbc(content, &program, &decodeError)) {
                result.diagnostics.append(QStringLiteral("ATBC %1 не прошёл проверку: %2").arg(archivePath, decodeError));
                continue;
            }
            const QVariantMap metadata = program.metadata;
            if (metadata.value(QStringLiteral("id")).toString() != id ||
                metadata.value(QStringLiteral("name")).toString() != name ||
                metadata.value(QStringLiteral("version")).toString() != version) {
                result.diagnostics.append(QStringLiteral("Метаданные ATBC %1 не совпадают с atlas-project.json").arg(archivePath));
                continue;
            }
            ++programCount;
        }
        archivePaths.insert(pathKey);
        totalBytes += content.size();
        files.append({sourceInfo.absoluteFilePath(), archivePath, content,
                      QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()),
                      requested.program});
    }
    if (!result.diagnostics.isEmpty()) {
        return result;
    }
    if (programCount == 0 || programCount > kMaximumPrograms) {
        result.diagnostics.append(QStringLiteral("Пакет должен содержать от 1 до 32 программ ATBC"));
        return result;
    }

    QString entryPoint;
    QString entryPointSha256;
    for (const PreparedFile &file : files) {
        if (file.program && (file.archivePath == QLatin1String("program/main.atbc") || entryPoint.isEmpty())) {
            entryPoint = file.archivePath;
            entryPointSha256 = file.sha256;
        }
    }
    if (programCount > 1 && QVersionNumber::fromString(minimumLauncherVersion) < QVersionNumber(0, 5, 0)) {
        minimumLauncherVersion = QStringLiteral("0.5.0");
    }
    const QJsonObject manifest = {
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("version"), version},
        {QStringLiteral("apiVersion"), QStringLiteral("1.0")},
        {QStringLiteral("minimumLauncherVersion"), minimumLauncherVersion},
        {QStringLiteral("kind"), QStringLiteral("atlas-code")},
        {QStringLiteral("platform"), QStringLiteral("win64-mingw81-qt5.15")},
        {QStringLiteral("entryPoint"), entryPoint},
        {QStringLiteral("entryPointSha256"), entryPointSha256},
        {QStringLiteral("programs"), entriesToJson(files, true)},
        {QStringLiteral("resources"), entriesToJson(files, false)},
        {QStringLiteral("description"), description},
        {QStringLiteral("permissions"), toJsonArray(permissions)},
        {QStringLiteral("pages"), toJsonArray(pages)},
        {QStringLiteral("actions"), toJsonArray(actions)},
        {QStringLiteral("publisher"), publisher},
        {QStringLiteral("homepage"), homepage}
    };
    QString zipError;
    if (!writeZip(request.outputPath, QJsonDocument(manifest).toJson(QJsonDocument::Compact), files, &zipError)) {
        result.diagnostics.append(zipError);
        return result;
    }

    result.success = true;
    result.outputPath = QFileInfo(request.outputPath).absoluteFilePath();
    result.diagnostics.append(QStringLiteral("Пакет собран: %1 программ и %2 ресурсов. Исходники Atlas Code не включены.")
                                  .arg(programCount).arg(files.size() - programCount));
    return result;
}

} // namespace atlas::packager
