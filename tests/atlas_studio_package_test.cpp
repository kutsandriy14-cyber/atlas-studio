#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"
#include "miniz/miniz.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {

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

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
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

class AtlasStudioPackageTest final : public QObject
{
    Q_OBJECT

private slots:
    void buildsMultiFileBinaryPackageWithoutSource();
};

void AtlasStudioPackageTest::buildsMultiFileBinaryPackageWithoutSource()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY2(temporaryDirectory.isValid(), "Temporary project directory must be available");

    const QJsonObject project = {
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("org.atlasstudio.test-plugin")},
        {QStringLiteral("name"), QStringLiteral("Studio Test Plugin")},
        {QStringLiteral("version"), QStringLiteral("1.2.3")},
        {QStringLiteral("publisher"), QStringLiteral("Atlas Studio Test")},
        {QStringLiteral("description"), QStringLiteral("A multi-file package built by the visual IDE test.")},
        {QStringLiteral("minimumLauncherVersion"), QStringLiteral("0.5.0")},
        {QStringLiteral("permissions"), QJsonArray{QStringLiteral("files.plugin-data")}},
        {QStringLiteral("pages"), QJsonArray{QStringLiteral("studio-test"), QStringLiteral("game-test")}},
        {QStringLiteral("actions"), QJsonArray{}}
    };
    const QString projectPath = temporaryDirectory.path();
    writeFile(QDir(projectPath).filePath(QStringLiteral("atlas-project.json")),
              QJsonDocument(project).toJson(QJsonDocument::Indented));
    QVERIFY(QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("src"))));
    QVERIFY(QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("assets"))));
    QVERIFY(QDir().mkpath(QDir(projectPath).filePath(QStringLiteral("build"))));

    const QString mainSource = QStringLiteral(
        "on launcher.started\n"
        "  call ui.page.create id=studio-test title=\"Studio Test\"\n"
        "end\n");
    const QString gameSource = QStringLiteral(
        "on game.started\n"
        "  call ui.page.create id=game-test title=\"Game Test\"\n"
        "end\n");
    atlas::runtime::AtlasCodeProgram mainProgram;
    atlas::runtime::AtlasCodeProgram gameProgram;
    QString compilationError;
    QVERIFY2(compileProgram(project, mainSource, &mainProgram, &compilationError), qPrintable(compilationError));
    QVERIFY2(compileProgram(project, gameSource, &gameProgram, &compilationError), qPrintable(compilationError));
    QByteArray mainAtbc;
    QByteArray gameAtbc;
    QVERIFY2(encodeProgram(mainProgram, &mainAtbc, &compilationError), qPrintable(compilationError));
    QVERIFY2(encodeProgram(gameProgram, &gameAtbc, &compilationError), qPrintable(compilationError));
    QVERIFY(!mainAtbc.contains(mainSource.toUtf8()));
    QVERIFY(!gameAtbc.contains(gameSource.toUtf8()));

    const QString mainAtbcPath = QDir(projectPath).filePath(QStringLiteral("build/main.atbc"));
    const QString gameAtbcPath = QDir(projectPath).filePath(QStringLiteral("build/game-started.atbc"));
    const QString resourcePath = QDir(projectPath).filePath(QStringLiteral("assets/defaults.json"));
    const QByteArray resource = QByteArrayLiteral("{\"schema\":1,\"theme\":\"dark\"}\n");
    writeFile(mainAtbcPath, mainAtbc);
    writeFile(gameAtbcPath, gameAtbc);
    writeFile(resourcePath, resource);

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
    QVERIFY2(result.success, qPrintable(result.diagnostics.join(QStringLiteral("\n"))));

    const QByteArray manifestBytes = readArchiveFile(packagePath, "manifest.json");
    QVERIFY(!manifestBytes.isEmpty());
    QJsonParseError manifestError;
    const QJsonDocument manifestDocument = QJsonDocument::fromJson(manifestBytes, &manifestError);
    QCOMPARE(manifestError.error, QJsonParseError::NoError);
    QVERIFY(manifestDocument.isObject());
    const QJsonObject manifest = manifestDocument.object();
    QCOMPARE(manifest.value(QStringLiteral("schemaVersion")).toInt(), 2);
    QCOMPARE(manifest.value(QStringLiteral("minimumLauncherVersion")).toString(), QStringLiteral("0.5.0"));
    QCOMPARE(manifest.value(QStringLiteral("entryPoint")).toString(), QStringLiteral("program/main.atbc"));
    QCOMPARE(manifest.value(QStringLiteral("programs")).toArray().size(), 2);
    QCOMPARE(manifest.value(QStringLiteral("resources")).toArray().size(), 1);

    QCOMPARE(readArchiveFile(packagePath, "program/main.atbc"), mainAtbc);
    QCOMPARE(readArchiveFile(packagePath, "program/game-started.atbc"), gameAtbc);
    QCOMPARE(readArchiveFile(packagePath, "assets/defaults.json"), resource);
    QVERIFY(readArchiveFile(packagePath, "src/main.atlas").isEmpty());
    QVERIFY(readArchiveFile(packagePath, "src/game-started.atlas").isEmpty());
}

QTEST_MAIN(AtlasStudioPackageTest)
#include "atlas_studio_package_test.moc"
