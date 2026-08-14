#include "core/atlas_code_compiler.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

namespace {

const QSet<QString> kAllowedPermissions = {
    QStringLiteral("servers.control"),
    QStringLiteral("servers.console"),
    QStringLiteral("files.plugin-data"),
    QStringLiteral("network.metadata"),
    QStringLiteral("ui.feedback"),
    QStringLiteral("instances.read"),
    QStringLiteral("content.read"),
    QStringLiteral("content.refresh"),
    QStringLiteral("launcher.navigation")
};

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

void printError(const QString &message)
{
    QTextStream(stderr) << "OrvexaCompiler: " << message << Qt::endl;
}

QStringList parsePermissions(const QString &input, QString *error)
{
    QStringList permissions;
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return permissions;
    }

    for (const QString &item : trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString permission = item.trimmed();
        if (permission.isEmpty() || !kAllowedPermissions.contains(permission)) {
            *error = QStringLiteral("неподдерживаемое разрешение: %1").arg(permission);
            return {};
        }
        if (permissions.contains(permission)) {
            *error = QStringLiteral("разрешение повторяется: %1").arg(permission);
            return {};
        }
        permissions.append(permission);
    }
    return permissions;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("OrvexaCompiler"));
    application.setApplicationVersion(QStringLiteral("0.3.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Компилирует Orvexa Code в проверяемый бинарный формат ATBC 2. "
                       "Выходной файл не содержит исходный .atlas-код."));
    parser.addHelpOption();

    const QCommandLineOption idOption(QStringLiteral("id"), QStringLiteral("Идентификатор плагина (например, org.example.plugin)."), QStringLiteral("id"));
    const QCommandLineOption nameOption(QStringLiteral("name"), QStringLiteral("Отображаемое имя программы Orvexa Code."), QStringLiteral("name"));
    const QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Версия плагина в SemVer-формате."), QStringLiteral("version"));
    const QCommandLineOption publisherOption(QStringLiteral("publisher"), QStringLiteral("Автор или издатель плагина."), QStringLiteral("publisher"));
    const QCommandLineOption descriptionOption(QStringLiteral("description"), QStringLiteral("Краткое описание плагина."), QStringLiteral("description"));
    const QCommandLineOption permissionsOption(
        QStringLiteral("permissions"),
        QStringLiteral("Разрешения через запятую: servers.control, servers.console, files.plugin-data, network.metadata, ui.feedback, instances.read, content.read, content.refresh, launcher.navigation."),
        QStringLiteral("list"), QString());
    const QCommandLineOption minimumLauncherOption(QStringLiteral("min-launcher"), QStringLiteral("Минимальная версия Orvexa Launcher в SemVer-формате."), QStringLiteral("version"));

    parser.addOption(idOption);
    parser.addOption(nameOption);
    parser.addOption(versionOption);
    parser.addOption(publisherOption);
    parser.addOption(descriptionOption);
    parser.addOption(permissionsOption);
    parser.addOption(minimumLauncherOption);
    parser.addPositionalArgument(QStringLiteral("input.atlas"), QStringLiteral("Исходный файл Orvexa Code."));
    parser.addPositionalArgument(QStringLiteral("output.atbc"), QStringLiteral("Выходной бинарный файл ATBC 2."));
    parser.process(application);

    const QList<QCommandLineOption> requiredOptions = {
        idOption, nameOption, versionOption, publisherOption, descriptionOption, minimumLauncherOption
    };
    for (const QCommandLineOption &option : requiredOptions) {
        if (!parser.isSet(option) || parser.value(option).trimmed().isEmpty()) {
            printError(QStringLiteral("обязательный параметр --%1 не задан").arg(option.names().first()));
            return 2;
        }
    }

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.size() != 2) {
        printError(QStringLiteral("нужно указать input.atlas и output.atbc"));
        parser.showHelp(2);
    }

    const QString id = parser.value(idOption).trimmed();
    const QString name = parser.value(nameOption).trimmed();
    const QString version = parser.value(versionOption).trimmed();
    const QString publisher = parser.value(publisherOption).trimmed();
    const QString description = parser.value(descriptionOption).trimmed();
    const QString minimumLauncherVersion = parser.value(minimumLauncherOption).trimmed();
    if (!validId(id)) {
        printError(QStringLiteral("--id должен содержать 3–128 символов: строчные буквы, цифры, точки или дефисы"));
        return 2;
    }
    if (!validVersion(version) || !validVersion(minimumLauncherVersion)) {
        printError(QStringLiteral("--version и --min-launcher должны быть версиями SemVer, например 1.0.0"));
        return 2;
    }
    if (name.isEmpty() || name.size() > 120 || publisher.isEmpty() || publisher.size() > 120 ||
        description.isEmpty() || description.size() > 2048) {
        printError(QStringLiteral("некорректная длина --name, --publisher или --description"));
        return 2;
    }

    QString permissionsError;
    const QStringList permissions = parsePermissions(parser.value(permissionsOption), &permissionsError);
    if (!permissionsError.isEmpty()) {
        printError(permissionsError);
        return 2;
    }

    const QFileInfo inputInfo(positionalArguments.at(0));
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        printError(QStringLiteral("исходный файл не найден: %1").arg(inputInfo.filePath()));
        return 2;
    }
    QFile sourceFile(inputInfo.filePath());
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        printError(QStringLiteral("не удалось прочитать исходный файл: %1").arg(sourceFile.errorString()));
        return 1;
    }
    const QString source = QString::fromUtf8(sourceFile.readAll());
    if (sourceFile.error() != QFile::NoError) {
        printError(QStringLiteral("не удалось прочитать исходный файл: %1").arg(sourceFile.errorString()));
        return 1;
    }

    atlas::runtime::AtlasCodeCompileResult result = atlas::runtime::AtlasCodeCompiler::compile(name, source);
    if (!result.success) {
        for (const atlas::runtime::AtlasCodeDiagnostic &diagnostic : result.diagnostics) {
            QTextStream(stderr) << inputInfo.filePath() << ':' << diagnostic.line << ": " << diagnostic.message << Qt::endl;
        }
        return 1;
    }

    result.program.metadata = {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("version"), version},
        {QStringLiteral("publisher"), publisher},
        {QStringLiteral("description"), description},
        {QStringLiteral("permissions"), permissions},
        {QStringLiteral("minimumLauncherVersion"), minimumLauncherVersion}
    };
    QString encodingError;
    const QByteArray atbc = atlas::runtime::AtlasCodeCompiler::encodeAtbc(result.program, &encodingError);
    if (atbc.isEmpty()) {
        printError(QStringLiteral("не удалось создать ATBC: %1").arg(encodingError));
        return 1;
    }

    const QFileInfo outputInfo(positionalArguments.at(1));
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        printError(QStringLiteral("не удалось создать каталог: %1").arg(outputInfo.absolutePath()));
        return 1;
    }
    QSaveFile outputFile(outputInfo.filePath());
    if (!outputFile.open(QIODevice::WriteOnly) || outputFile.write(atbc) != atbc.size() || !outputFile.commit()) {
        printError(QStringLiteral("не удалось записать ATBC: %1").arg(outputFile.errorString()));
        return 1;
    }

    QTextStream(stdout) << "Создан ATBC 2: " << outputInfo.filePath() << " (" << atbc.size() << " байт)" << Qt::endl;
    return 0;
}
