#pragma once

#include <QString>
#include <QStringList>

namespace atlas::studio {

struct PluginTestRequest
{
    QString projectDirectory;
    QString outputPackagePath;
};

struct PluginTestResult
{
    bool success = false;
    bool sourceCompiled = false;
    bool atbcValidated = false;
    bool packageBuilt = false;
    bool sandboxReady = false;
    QString packagePath;
    QString sandboxDirectory;
    QStringList diagnostics;
};

// Local, deterministic pre-publication verification. ATBC is data interpreted by
// Runtime, so this runner deliberately validates and packages it without
// pretending to execute arbitrary code in the Studio process.
class StudioTestRunner final
{
public:
    static PluginTestResult run(const PluginTestRequest &request);
};

} // namespace atlas::studio
