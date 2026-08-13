#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace atlas::runtime {

struct AtlasCodeDiagnostic
{
    int line = 0;
    QString message;
};

struct AtlasCodeCommand
{
    int line = 0;
    QString actionId;
    QVariantMap arguments;
};

struct AtlasCodeProgram
{
    QString name;
    // Informational package metadata emitted by AtlasCompiler. Runtime never
    // treats it as authority for permissions; manifest.json remains the policy
    // boundary enforced by the host.
    QVariantMap metadata;
    QHash<QString, QVector<AtlasCodeCommand>> events;
};

struct AtlasCodeCompileResult
{
    bool success = false;
    AtlasCodeProgram program;
    QVector<AtlasCodeDiagnostic> diagnostics;
};

// Compiler and serialiser for the intentionally limited Atlas Code 1.0
// language. The result is binary ATBC 2, a deterministic data format
// interpreted by Atlas Runtime; it is not executable machine code.
class AtlasCodeCompiler final
{
public:
    static AtlasCodeCompileResult compile(const QString &name, const QString &source);
    static QByteArray encodeAtbc(const AtlasCodeProgram &program, QString *error);
    static bool decodeAtbc(const QByteArray &atbc, AtlasCodeProgram *program, QString *error);
    static QStringList supportedEvents();

private:
    static bool parseCall(const QString &line, AtlasCodeCommand *command, QString *error);
};

} // namespace atlas::runtime
