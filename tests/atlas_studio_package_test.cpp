#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"
#include "miniz/miniz.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdlib>

namespace {

int fail(const QString &message)
{
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    return EXIT_FAILURE;
}

QByteArray readArchiveFile(const QString &archivePath, const char *entryName)
{
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, archivePath.toUtf8().constData(), 0)) {
        return {};
    }

    size_t extractedSize = 0;
    void *extracted = mz_zip_reader_extract_file_to_heap(&archive, entryName, &extractedSize, 0);
    mz_zip_reader_end(&archive);
    if (!extracted) {
        return {};
    }

    const QByteArray content(static_cast<const char *>(extracted), static_cast<int>(extractedSize));
    mz_free(extracted);
    return content;
}

bool writeFile(const QString &path, const QByteArray &content, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Не удалось открыть %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(content) != content.size()) {
        *error = QStringLiteral("Не удалось полностью записать %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool compileProgram(const QJsonObject &project, const QString &source,
                    atlas::runtime::AtlasCodeProgram *program, QString *error)
{
    atlas::runtime::AtlasCodeCompileResult compilation =
        atlas::runtime::AtlasCodeCompiler::compile(project.value(QStringLiteral("name")).toString(), source);
    if (!compilation.success) {
        QStringList messages;
        for (const atlas::runtime::AtlasCodeDiagnostic &diagnostic : compilation.diagnostics) {
            messages.append(QStringLiteral("%1: %2").arg(diagnostic.line).arg(diagnostic.message));
        }
        *error = messages.join(QStringLiteral("\n"));
        return false;
    }
    compilation.program.metadata = {
        {QStringLiteral("id"), project.value(QStringLiteral("id")).toString()},
        {QStringLiteral("name"), project.value(QStringLiteral("name")).toString()},
        {QStringLiteral("version"), project.value(QStringLiteral("version")).toString()},
        {QStringLiteral("publisher"), project.value(QStringLiteral("publisher")).toString()}
    };
    *program = compilation.program;
    return true;
}

bool encodeProgram(const atlas::runtime::AtlasCodeProgram &program, QByteArray *atbc, QString *error)
{
    *atbc = atlas::runtime::AtlasCodeCompiler::encodeAtbc(program, error);
    return !atbc->isEmpty() && atbc->startsWith("ATBC\x02");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        return fail(QStringLiteral("Временный каталог проекта недоступен"));
    }

    const QJsonObject project = {
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("org.atlasstudio.test-plugin")},
        {QStringLiteral("name"), QStringLiteral("Studio Test Plugin")},
        {QStringLiteral("version"), QStringLiteral("1.2.3")},
        {QStringLiteral("publisher"), QStringLiteral("Orvexa Studio Test")},
        {QStringLiteral("description"), QStringLiteral("A multi-file package built by the visual IDE test.")},
        {QStringLiteral("minimumLauncherVersion"), QStringLiteral("0.7.5")},
        {QStringLiteral("permissions"), QJsonArray{QStringLiteral("files.plugin-data"),
                                                   QStringLiteral("ui.feedback")}},
        {QStringLiteral("pages"), QJsonArray{QStringLiteral("studio-test"), QStringLiteral("game-test")}},
        {QStringLiteral("actions"), QJsonArray{}}
    };
    const QString projectPath = temporaryDirectory.path();
    QString error;
    if (!writeFile(QDir(projectPath).filePath(QStringLiteral("atlas-project.json")),
                   QJsonDocument(project).toJson(QJsonDocument::Indented), &error)) {
        return fail(error);
    }
    if (!QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("src"))) ||
        !QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("assets"))) ||
        !QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("build")))) {
        return fail(QStringLiteral("Не удалось подготовить структуру временного проекта"));
    }

    const QString mainSource = QStringLiteral(
        "on launcher.started\n"
        "  call ui.page.create id=studio-test title=\"Studio Test\"\n"
        "end\n"
        "\n"
        "on ui.org.atlasstudio.test-plugin.studio-test.refresh.clicked\n"
        "  call ui.feedback.notify message=\"Refreshed\" severity=info\n"
        "end\n");
    const QString gameSource = QStringLiteral(
        "on game.started\n"
        "  call ui.page.create id=game-test title=\"Game Test\"\n"
        "end\n");
    atlas::runtime::AtlasCodeProgram mainProgram;
    atlas::runtime::AtlasCodeProgram gameProgram;
    if (!compileProgram(project, mainSource, &mainProgram, &error) ||
        !compileProgram(project, gameSource, &gameProgram, &error)) {
        return fail(QStringLiteral("Не удалось скомпилировать Orvexa Code: %1").arg(error));
    }
    QByteArray mainAtbc;
    QByteArray gameAtbc;
    if (!encodeProgram(mainProgram, &mainAtbc, &error) ||
        !encodeProgram(gameProgram, &gameAtbc, &error)) {
        return fail(QStringLiteral("Не удалось закодировать ATBC 2: %1").arg(error));
    }
    if (mainAtbc.contains(mainSource.toUtf8()) || gameAtbc.contains(gameSource.toUtf8())) {
        return fail(QStringLiteral("Бинарный ATBC содержит исходный Orvexa Code"));
    }
    atlas::runtime::AtlasCodeProgram decodedMainProgram;
    if (!atlas::runtime::AtlasCodeCompiler::decodeAtbc(mainAtbc, &decodedMainProgram, &error) ||
        !decodedMainProgram.events.contains(QStringLiteral("ui.org.atlasstudio.test-plugin.studio-test.refresh.clicked"))) {
        return fail(QStringLiteral("ATBC не сохранил безопасное динамическое UI-событие: %1").arg(error));
    }

    const QString mainAtbcPath = QDir(projectPath).filePath(QStringLiteral("build/main.atbc"));
    const QString gameAtbcPath = QDir(projectPath).filePath(QStringLiteral("build/game-started.atbc"));
    const QString resourcePath = QDir(projectPath).filePath(QStringLiteral("assets/defaults.json"));
    const QByteArray resource = QByteArrayLiteral("{\"schema\":1,\"theme\":\"dark\"}\n");
    if (!writeFile(mainAtbcPath, mainAtbc, &error) ||
        !writeFile(gameAtbcPath, gameAtbc, &error) ||
        !writeFile(resourcePath, resource, &error)) {
        return fail(error);
    }

    const QString packagePath = QDir(projectPath).filePath(QStringLiteral("build/studio-test.atp"));
    atlas::packager::AtlasPackageRequest request;
    request.projectDirectory = projectPath;
    request.outputPath = packagePath;
    request.files = {
        {mainAtbcPath, QStringLiteral("program/main.atbc"), true},
        {gameAtbcPath, QStringLiteral("program/game-started.atbc"), true},
        {resourcePath, QStringLiteral("assets/defaults.json"), false}
    };
    const atlas::packager::AtlasPackageResult result = atlas::packager::AtlasPackageBuilder::build(request);
    if (!result.success) {
        return fail(QStringLiteral("Сборщик отклонил корректный многофайловый пакет: %1")
                        .arg(result.diagnostics.join(QStringLiteral("\n"))));
    }

    const QByteArray manifestBytes = readArchiveFile(packagePath, "manifest.json");
    if (manifestBytes.isEmpty()) {
        return fail(QStringLiteral("В архиве отсутствует manifest.json"));
    }
    QJsonParseError manifestError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestBytes, &manifestError);
    if (manifestError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
        return fail(QStringLiteral("manifest.json некорректен: %1").arg(manifestError.errorString()));
    }
    const QJsonObject manifest = manifestDocument.object();
    if (manifest.value(QStringLiteral("schemaVersion")).toInt() != 2 ||
        manifest.value(QStringLiteral("minimumLauncherVersion")).toString() != QLatin1String("0.7.5") ||
        manifest.value(QStringLiteral("entryPoint")).toString() != QLatin1String("program/main.atbc") ||
        manifest.value(QStringLiteral("programs")).toArray().size() != 2 ||
        manifest.value(QStringLiteral("resources")).toArray().size() != 1) {
        return fail(QStringLiteral("manifest.json не описывает ожидаемый schema 2 многофайловый пакет"));
    }
    if (readArchiveFile(packagePath, "program/main.atbc") != mainAtbc ||
        readArchiveFile(packagePath, "program/game-started.atbc") != gameAtbc ||
        readArchiveFile(packagePath, "assets/defaults.json") != resource) {
        return fail(QStringLiteral("Содержимое одного из объявленных файлов не совпадает с исходными байтами"));
    }
    if (!readArchiveFile(packagePath, "src/main.atlas").isEmpty() ||
        !readArchiveFile(packagePath, "src/game-started.atlas").isEmpty()) {
        return fail(QStringLiteral("Исходный Orvexa Code включён в готовый .atp"));
    }

    QTextStream(stdout) << "PASS: Orvexa Studio multi-file package regression" << Qt::endl;
    return EXIT_SUCCESS;
}
