#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class AtlasStudioPackageTest final : public QObject
{
    Q_OBJECT

private slots:
    void buildsBinaryPackageWithoutSource();
};

void AtlasStudioPackageTest::buildsBinaryPackageWithoutSource()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY2(temporaryDirectory.isValid(), "Temporary project directory must be available");

    const QJsonObject project = {
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), QStringLiteral("org.atlasstudio.test-plugin")},
        {QStringLiteral("name"), QStringLiteral("Studio Test Plugin")},
        {QStringLiteral("version"), QStringLiteral("1.2.3")},
        {QStringLiteral("publisher"), QStringLiteral("Atlas Studio Test")},
        {QStringLiteral("description"), QStringLiteral("A package built by the visual IDE test.")},
        {QStringLiteral("minimumLauncherVersion"), QStringLiteral("0.4.0")},
        {QStringLiteral("permissions"), QJsonArray{QStringLiteral("files.plugin-data")}},
        {QStringLiteral("pages"), QJsonArray{QStringLiteral("studio-test")}},
        {QStringLiteral("actions"), QJsonArray{}}
    };
    QFile projectFile(temporaryDirectory.filePath(QStringLiteral("atlas-project.json")));
    QVERIFY(projectFile.open(QIODevice::WriteOnly));
    projectFile.write(QJsonDocument(project).toJson(QJsonDocument::Indented));
    projectFile.close();

    const QString source = QStringLiteral(
        "on launcher.started\n"
        "  call ui.page.create id=studio-test title=\"Studio Test\"\n"
        "end\n");
    atlas::runtime::AtlasCodeCompileResult compilation =
        atlas::runtime::AtlasCodeCompiler::compile(project.value(QStringLiteral("name")).toString(), source);
    QVERIFY2(compilation.success, "The sample Atlas Code must compile");
    compilation.program.metadata = {
        {QStringLiteral("id"), project.value(QStringLiteral("id")).toString()},
        {QStringLiteral("name"), project.value(QStringLiteral("name")).toString()},
        {QStringLiteral("version"), project.value(QStringLiteral("version")).toString()},
        {QStringLiteral("publisher"), project.value(QStringLiteral("publisher")).toString()}
    };

    QString encodeError;
    const QByteArray atbc = atlas::runtime::AtlasCodeCompiler::encodeAtbc(compilation.program, &encodeError);
    QVERIFY2(!atbc.isEmpty(), qPrintable(encodeError));
    QVERIFY(atbc.startsWith("ATBC\x02"));
    QVERIFY(!atbc.contains(source.toUtf8()));

    const QString atbcPath = temporaryDirectory.filePath(QStringLiteral("build/main.atbc"));
    QDir().mkpath(temporaryDirectory.filePath(QStringLiteral("build")));
    QFile atbcFile(atbcPath);
    QVERIFY(atbcFile.open(QIODevice::WriteOnly));
    atbcFile.write(atbc);
    atbcFile.close();

    const QString packagePath = temporaryDirectory.filePath(QStringLiteral("build/studio-test.atp"));
    const atlas::packager::AtlasPackageResult result = atlas::packager::AtlasPackageBuilder::build({
        temporaryDirectory.path(), atbcPath, packagePath
    });
    QVERIFY2(result.success, qPrintable(result.diagnostics.join(QStringLiteral("\n"))));

    QFile packageFile(packagePath);
    QVERIFY(packageFile.open(QIODevice::ReadOnly));
    const QByteArray packageBytes = packageFile.readAll();
    QVERIFY(packageBytes.contains("manifest.json"));
    QVERIFY(packageBytes.contains("program/main.atbc"));
    QVERIFY(!packageBytes.contains("main.atlas"));
    QVERIFY(!packageBytes.contains(source.toUtf8()));
}

QTEST_MAIN(AtlasStudioPackageTest)
#include "atlas_studio_package_test.moc"
