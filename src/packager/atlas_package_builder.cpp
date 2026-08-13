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
#include <QTemporaryFile>

namespace atlas::packager {
namespace {

constexpr qint64 kMaximumAtbcBytes = 256LL * 1024;
const QSet<QString> kAllowedPermissions = {
    QStringLiteral("servers.control"),
    QStringLiteral("servers.console"),
    QStringLiteral("files.plugin-data"),
    QStringLiteral("network.metadata")
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

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

bool writeZip(const QString &path, const QByteArray &manifest, const QByteArray &atbc, QString *error)
{
    const QFileInfo outputInfo(path);
    QDir().mkpath(outputInfo.absolutePath());

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
    if (ok) {
        ok = mz_zip_writer_add_mem(&archive, "program/main.atbc", atbc.constData(),
                                   static_cast<size_t>(atbc.size()), MZ_BEST_COMPRESSION) != 0;
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
    const QString minimumLauncherVersion = project.value(QStringLiteral("minimumLauncherVersion"))
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

    QFile atbcFile(request.atbcPath);
    if (!atbcFile.open(QIODevice::ReadOnly)) {
        result.diagnostics.append(QStringLiteral("Не удалось открыть скомпилированный ATBC: %1").arg(atbcFile.errorString()));
        return result;
    }
    const QByteArray atbc = atbcFile.read(kMaximumAtbcBytes + 1);
    if (atbc.isEmpty() || atbc.size() > kMaximumAtbcBytes) {
        result.diagnostics.append(QStringLiteral("ATBC отсутствует или превышает 256 КиБ"));
        return result;
    }

    runtime::AtlasCodeProgram program;
    QString decodeError;
    if (!runtime::AtlasCodeCompiler::decodeAtbc(atbc, &program, &decodeError)) {
        result.diagnostics.append(QStringLiteral("ATBC не прошёл проверку: %1").arg(decodeError));
        return result;
    }
    const QVariantMap metadata = program.metadata;
    if (metadata.value(QStringLiteral("id")).toString() != id ||
        metadata.value(QStringLiteral("name")).toString() != name ||
        metadata.value(QStringLiteral("version")).toString() != version) {
        result.diagnostics.append(QStringLiteral("Метаданные ATBC не совпадают с atlas-project.json; сначала скомпилируйте проект заново"));
        return result;
    }

    const QString sha256 = QString::fromLatin1(QCryptographicHash::hash(atbc, QCryptographicHash::Sha256).toHex());
    const QJsonObject manifest = {
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("version"), version},
        {QStringLiteral("apiVersion"), QStringLiteral("1.0")},
        {QStringLiteral("minimumLauncherVersion"), minimumLauncherVersion},
        {QStringLiteral("kind"), QStringLiteral("atlas-code")},
        {QStringLiteral("platform"), QStringLiteral("win64-mingw81-qt5.15")},
        {QStringLiteral("entryPoint"), QStringLiteral("program/main.atbc")},
        {QStringLiteral("entryPointSha256"), sha256},
        {QStringLiteral("description"), description},
        {QStringLiteral("permissions"), toJsonArray(permissions)},
        {QStringLiteral("pages"), toJsonArray(pages)},
        {QStringLiteral("actions"), toJsonArray(actions)},
        {QStringLiteral("publisher"), publisher},
        {QStringLiteral("homepage"), homepage}
    };
    QString zipError;
    if (!writeZip(request.outputPath, QJsonDocument(manifest).toJson(QJsonDocument::Compact), atbc, &zipError)) {
        result.diagnostics.append(zipError);
        return result;
    }

    result.success = true;
    result.outputPath = QFileInfo(request.outputPath).absoluteFilePath();
    result.diagnostics.append(QStringLiteral("Пакет собран. Внутри только manifest.json и бинарный program/main.atbc; исходники не добавлены."));
    return result;
}

} // namespace atlas::packager
