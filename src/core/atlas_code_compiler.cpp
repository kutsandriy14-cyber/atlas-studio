#include "core/atlas_code_compiler.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace atlas::runtime {
namespace {

constexpr int kMaximumSourceBytes = 128 * 1024;
constexpr int kMaximumAtbcBytes = 256 * 1024;
constexpr int kMaximumCommandsPerEvent = 64;
constexpr char kAtbcMagic[] = {'A', 'T', 'B', 'C'};
constexpr unsigned char kAtbcVersion1 = 1;
constexpr unsigned char kAtbcVersion2 = 2;
constexpr int kAtbcVersion1HeaderBytes = 9;
constexpr int kAtbcVersion2HeaderBytes = 41;

QString unquote(const QString &value, bool *ok)
{
    *ok = true;
    const QString trimmed = value.trimmed();
    if (trimmed.size() < 2 || !trimmed.startsWith(QLatin1Char('"')) || !trimmed.endsWith(QLatin1Char('"'))) {
        return trimmed;
    }

    QString result;
    bool escaped = false;
    for (int index = 1; index < trimmed.size() - 1; ++index) {
        const QChar current = trimmed.at(index);
        if (escaped) {
            if (current == QLatin1Char('"') || current == QLatin1Char('\\')) {
                result.append(current);
            } else if (current == QLatin1Char('n')) {
                result.append(QLatin1Char('\n'));
            } else if (current == QLatin1Char('t')) {
                result.append(QLatin1Char('\t'));
            } else {
                *ok = false;
                return {};
            }
            escaped = false;
        } else if (current == QLatin1Char('\\')) {
            escaped = true;
        } else {
            result.append(current);
        }
    }
    if (escaped) {
        *ok = false;
        return {};
    }
    return result;
}

QVariant parseValue(const QString &value, bool *ok)
{
    const QString trimmed = value.trimmed();
    if (trimmed.startsWith(QLatin1Char('"'))) {
        return unquote(trimmed, ok);
    }
    if (trimmed == QLatin1String("true")) {
        *ok = true;
        return true;
    }
    if (trimmed == QLatin1String("false")) {
        *ok = true;
        return false;
    }
    static const QRegularExpression integerExpression(QStringLiteral("^-?(0|[1-9][0-9]*)$"));
    if (integerExpression.match(trimmed).hasMatch()) {
        bool numberOk = false;
        const qlonglong number = trimmed.toLongLong(&numberOk);
        *ok = numberOk;
        return number;
    }
    *ok = !trimmed.isEmpty();
    return trimmed;
}

QStringList tokenize(const QString &line, bool *ok)
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    bool escaped = false;
    *ok = true;
    for (const QChar character : line) {
        if (escaped) {
            current.append(QLatin1Char('\\'));
            current.append(character);
            escaped = false;
            continue;
        }
        if (character == QLatin1Char('\\') && inQuotes) {
            escaped = true;
            continue;
        }
        if (character == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            current.append(character);
            continue;
        }
        if (character.isSpace() && !inQuotes) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
            continue;
        }
        current.append(character);
    }
    if (inQuotes || escaped) {
        *ok = false;
        return {};
    }
    if (!current.isEmpty()) {
        tokens.append(current);
    }
    return tokens;
}

bool isValidActionId(const QString &id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z][a-z0-9.-]{2,127}$"));
    return expression.match(id).hasMatch();
}

bool isValidArgumentKey(const QString &key)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,63}$"));
    return expression.match(key).hasMatch();
}

void appendUint32(QByteArray *result, quint32 value)
{
    result->append(static_cast<char>((value >> 24U) & 0xffU));
    result->append(static_cast<char>((value >> 16U) & 0xffU));
    result->append(static_cast<char>((value >> 8U) & 0xffU));
    result->append(static_cast<char>(value & 0xffU));
}

quint32 readUint32(const QByteArray &data, int offset)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    return (static_cast<quint32>(bytes[0]) << 24U) |
           (static_cast<quint32>(bytes[1]) << 16U) |
           (static_cast<quint32>(bytes[2]) << 8U) |
           static_cast<quint32>(bytes[3]);
}

QJsonObject commandToJson(const AtlasCodeCommand &command)
{
    return {
        {QStringLiteral("line"), command.line},
        {QStringLiteral("action"), command.actionId},
        {QStringLiteral("arguments"), QJsonObject::fromVariantMap(command.arguments)}
    };
}

bool commandFromJson(const QJsonValue &value, AtlasCodeCommand *command, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("ATBC содержит недопустимую команду");
        return false;
    }
    const QJsonObject object = value.toObject();
    const int line = object.value(QStringLiteral("line")).toInt(-1);
    const QString action = object.value(QStringLiteral("action")).toString();
    const QJsonValue argumentsValue = object.value(QStringLiteral("arguments"));
    if (line < 1 || !isValidActionId(action) || !argumentsValue.isObject()) {
        *error = QStringLiteral("ATBC содержит некорректные поля команды");
        return false;
    }
    const QVariantMap arguments = argumentsValue.toObject().toVariantMap();
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it) {
        const QVariant &argument = it.value();
        if (!isValidArgumentKey(it.key()) ||
            (argument.type() != QVariant::String && argument.type() != QVariant::Bool &&
             argument.type() != QVariant::Double && argument.type() != QVariant::LongLong)) {
            *error = QStringLiteral("ATBC содержит некорректный аргумент команды");
            return false;
        }
    }
    *command = {line, action, arguments};
    return true;
}

} // namespace

QStringList AtlasCodeCompiler::supportedEvents()
{
    return {QStringLiteral("launcher.started"), QStringLiteral("game.started"), QStringLiteral("game.exited"),
            QStringLiteral("instance.changed"), QStringLiteral("content.updated")};
}

bool AtlasCodeCompiler::isSupportedEvent(const QString &eventName)
{
    if (supportedEvents().contains(eventName)) {
        return true;
    }
    // Dynamic events can only originate from a declared plugin UI control.
    // The bounded grammar prevents arbitrary host-dispatch namespaces in ATBC.
    static const QRegularExpression uiEvent(
        QStringLiteral("^ui\\.[a-z][a-z0-9.-]{0,63}\\.[a-z][a-z0-9-]{0,63}\\.[a-z][a-z0-9-]{0,63}\\.clicked$"));
    return uiEvent.match(eventName).hasMatch();
}

AtlasCodeCompileResult AtlasCodeCompiler::compile(const QString &name, const QString &source)
{
    AtlasCodeCompileResult result;
    result.program.name = name.trimmed();
    if (result.program.name.isEmpty() || result.program.name.size() > 120) {
        result.diagnostics.append({0, QStringLiteral("У программы нет корректного имени")});
        return result;
    }
    if (source.toUtf8().size() > kMaximumSourceBytes) {
        result.diagnostics.append({0, QStringLiteral("Программа превышает лимит 128 KiB")});
        return result;
    }

    QString activeEvent;
    const QStringList lines = source.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const int lineNumber = index + 1;
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (line.startsWith(QStringLiteral("on "))) {
            if (!activeEvent.isEmpty()) {
                result.diagnostics.append({lineNumber, QStringLiteral("Нельзя вкладывать блок on в другой блок")});
                continue;
            }
            const QString eventName = line.mid(3).trimmed();
            if (!isSupportedEvent(eventName)) {
                result.diagnostics.append({lineNumber, QStringLiteral("Неподдерживаемое событие: %1").arg(eventName)});
                continue;
            }
            if (result.program.events.contains(eventName)) {
                result.diagnostics.append({lineNumber, QStringLiteral("Событие объявлено повторно: %1").arg(eventName)});
                continue;
            }
            activeEvent = eventName;
            result.program.events.insert(activeEvent, {});
            continue;
        }
        if (line == QLatin1String("end")) {
            if (activeEvent.isEmpty()) {
                result.diagnostics.append({lineNumber, QStringLiteral("end находится вне блока события")});
            } else {
                activeEvent.clear();
            }
            continue;
        }
        if (line.startsWith(QStringLiteral("call "))) {
            if (activeEvent.isEmpty()) {
                result.diagnostics.append({lineNumber, QStringLiteral("call должен находиться внутри блока on")});
                continue;
            }
            QVector<AtlasCodeCommand> &commands = result.program.events[activeEvent];
            if (commands.size() >= kMaximumCommandsPerEvent) {
                result.diagnostics.append({lineNumber, QStringLiteral("Превышен лимит из 64 действий на событие")});
                continue;
            }
            AtlasCodeCommand command;
            command.line = lineNumber;
            QString error;
            if (!parseCall(line.mid(5).trimmed(), &command, &error)) {
                result.diagnostics.append({lineNumber, error});
                continue;
            }
            commands.append(command);
            continue;
        }
        result.diagnostics.append({lineNumber, QStringLiteral("Неизвестная инструкция")});
    }

    if (!activeEvent.isEmpty()) {
        result.diagnostics.append({lines.size(), QStringLiteral("Блок события не закрыт инструкцией end")});
    }
    result.success = result.diagnostics.isEmpty();
    return result;
}

bool AtlasCodeCompiler::parseCall(const QString &line, AtlasCodeCommand *command, QString *error)
{
    bool tokensOk = false;
    const QStringList tokens = tokenize(line, &tokensOk);
    if (!tokensOk || tokens.isEmpty()) {
        *error = QStringLiteral("Некорректная строка вызова");
        return false;
    }
    command->actionId = tokens.first();
    if (!isValidActionId(command->actionId)) {
        *error = QStringLiteral("Некорректный идентификатор действия");
        return false;
    }
    for (int index = 1; index < tokens.size(); ++index) {
        const QString token = tokens.at(index);
        const int separator = token.indexOf(QLatin1Char('='));
        if (separator <= 0 || separator == token.size() - 1) {
            *error = QStringLiteral("Аргумент должен иметь вид key=value");
            return false;
        }
        const QString key = token.left(separator);
        if (!isValidArgumentKey(key) || command->arguments.contains(key)) {
            *error = QStringLiteral("Некорректный или повторяющийся аргумент: %1").arg(key);
            return false;
        }
        bool valueOk = false;
        const QVariant value = parseValue(token.mid(separator + 1), &valueOk);
        if (!valueOk) {
            *error = QStringLiteral("Некорректное значение аргумента: %1").arg(key);
            return false;
        }
        command->arguments.insert(key, value);
    }
    return true;
}

QByteArray AtlasCodeCompiler::encodeAtbc(const AtlasCodeProgram &program, QString *error)
{
    if (program.name.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("У программы нет имени");
        }
        return {};
    }

    QJsonObject eventsObject;
    const QStringList eventNames = program.events.keys();
    for (const QString &eventName : eventNames) {
        if (!isSupportedEvent(eventName) || program.events.value(eventName).size() > kMaximumCommandsPerEvent) {
            if (error) {
                *error = QStringLiteral("Программа содержит неподдерживаемое событие или слишком много команд");
            }
            return {};
        }
        QJsonArray commands;
        for (const AtlasCodeCommand &command : program.events.value(eventName)) {
            if (command.line < 1 || !isValidActionId(command.actionId)) {
                if (error) {
                    *error = QStringLiteral("Программа содержит некорректную команду");
                }
                return {};
            }
            commands.append(commandToJson(command));
        }
        eventsObject.insert(eventName, commands);
    }

    const QJsonObject metadataObject = QJsonObject::fromVariantMap(program.metadata);
    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("ATBC")},
        {QStringLiteral("version"), static_cast<int>(kAtbcVersion2)},
        {QStringLiteral("name"), program.name},
        {QStringLiteral("metadata"), metadataObject},
        {QStringLiteral("events"), eventsObject}
    };
    const QByteArray jsonPayload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (jsonPayload.size() > kMaximumAtbcBytes - kAtbcVersion2HeaderBytes) {
        if (error) {
            *error = QStringLiteral("Скомпилированная программа превышает лимит 256 KiB");
        }
        return {};
    }

    // ATBC 2 deliberately stores a compressed binary payload rather than source
    // text. The outer SHA-256 detects local tampering before decompression.
    const QByteArray compressedPayload = qCompress(jsonPayload, 9);
    if (compressedPayload.isEmpty() || compressedPayload.size() > kMaximumAtbcBytes - kAtbcVersion2HeaderBytes) {
        if (error) {
            *error = QStringLiteral("Не удалось создать ATBC или превышен лимит размера");
        }
        return {};
    }

    QByteArray result(kAtbcMagic, 4);
    result.append(static_cast<char>(kAtbcVersion2));
    appendUint32(&result, static_cast<quint32>(compressedPayload.size()));
    result.append(QCryptographicHash::hash(compressedPayload, QCryptographicHash::Sha256));
    result.append(compressedPayload);
    return result;
}

bool AtlasCodeCompiler::decodeAtbc(const QByteArray &atbc, AtlasCodeProgram *program, QString *error)
{
    if (!program || atbc.size() < kAtbcVersion1HeaderBytes || atbc.size() > kMaximumAtbcBytes ||
        atbc.left(4) != QByteArray(kAtbcMagic, 4)) {
        if (error) {
            *error = QStringLiteral("Файл не является поддерживаемым ATBC");
        }
        return false;
    }

    const unsigned char containerVersion = static_cast<unsigned char>(atbc.at(4));
    QByteArray jsonPayload;
    if (containerVersion == kAtbcVersion1) {
        const quint32 payloadSize = readUint32(atbc, 5);
        if (payloadSize != static_cast<quint32>(atbc.size() - kAtbcVersion1HeaderBytes)) {
            if (error) {
                *error = QStringLiteral("ATBC 1 имеет недопустимую длину");
            }
            return false;
        }
        jsonPayload = atbc.mid(kAtbcVersion1HeaderBytes);
    } else if (containerVersion == kAtbcVersion2) {
        if (atbc.size() < kAtbcVersion2HeaderBytes) {
            if (error) {
                *error = QStringLiteral("ATBC 2 имеет неполный заголовок");
            }
            return false;
        }
        const quint32 payloadSize = readUint32(atbc, 5);
        if (payloadSize != static_cast<quint32>(atbc.size() - kAtbcVersion2HeaderBytes)) {
            if (error) {
                *error = QStringLiteral("ATBC 2 имеет недопустимую длину");
            }
            return false;
        }
        const QByteArray compressedPayload = atbc.mid(kAtbcVersion2HeaderBytes);
        const QByteArray expectedHash = atbc.mid(9, 32);
        if (QCryptographicHash::hash(compressedPayload, QCryptographicHash::Sha256) != expectedHash) {
            if (error) {
                *error = QStringLiteral("ATBC 2 не прошёл проверку целостности");
            }
            return false;
        }
        // qCompress prefixes its data with the uncompressed byte count. Validate
        // that count before qUncompress to prevent a compression-bomb allocation.
        if (compressedPayload.size() < 4 || readUint32(compressedPayload, 0) >
                                               static_cast<quint32>(kMaximumAtbcBytes - kAtbcVersion2HeaderBytes)) {
            if (error) {
                *error = QStringLiteral("ATBC 2 превышает лимит распаковки");
            }
            return false;
        }
        jsonPayload = qUncompress(compressedPayload);
        if (jsonPayload.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Не удалось распаковать ATBC 2");
            }
            return false;
        }
    } else {
        if (error) {
            *error = QStringLiteral("ATBC имеет неподдерживаемую версию контейнера");
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonPayload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("ATBC содержит некорректные данные: %1").arg(parseError.errorString());
        }
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("ATBC") ||
        root.value(QStringLiteral("version")).toInt() != static_cast<int>(containerVersion)) {
        if (error) {
            *error = QStringLiteral("ATBC имеет несовместимую версию данных");
        }
        return false;
    }
    const QString name = root.value(QStringLiteral("name")).toString().trimmed();
    const QJsonValue eventsValue = root.value(QStringLiteral("events"));
    if (name.isEmpty() || name.size() > 120 || !eventsValue.isObject()) {
        if (error) {
            *error = QStringLiteral("ATBC содержит некорректный заголовок программы");
        }
        return false;
    }

    const QJsonValue metadataValue = root.value(QStringLiteral("metadata"));
    if (!metadataValue.isUndefined() && !metadataValue.isObject()) {
        if (error) {
            *error = QStringLiteral("ATBC содержит некорректные метаданные программы");
        }
        return false;
    }

    AtlasCodeProgram decoded;
    decoded.name = name;
    if (metadataValue.isObject()) {
        decoded.metadata = metadataValue.toObject().toVariantMap();
    }
    const QJsonObject eventsObject = eventsValue.toObject();
    for (auto event = eventsObject.constBegin(); event != eventsObject.constEnd(); ++event) {
        if (!isSupportedEvent(event.key()) || !event.value().isArray()) {
            if (error) {
                *error = QStringLiteral("ATBC содержит неподдерживаемое событие");
            }
            return false;
        }
        const QJsonArray commandsArray = event.value().toArray();
        if (commandsArray.size() > kMaximumCommandsPerEvent) {
            if (error) {
                *error = QStringLiteral("ATBC превышает лимит команд на событие");
            }
            return false;
        }
        QVector<AtlasCodeCommand> commands;
        commands.reserve(commandsArray.size());
        for (const QJsonValue &value : commandsArray) {
            AtlasCodeCommand command;
            QString commandError;
            if (!commandFromJson(value, &command, &commandError)) {
                if (error) {
                    *error = commandError;
                }
                return false;
            }
            commands.append(command);
        }
        decoded.events.insert(event.key(), commands);
    }

    *program = decoded;
    return true;
}

} // namespace atlas::runtime
