#include "services/update_service.h"
#include "miniz/miniz.h"

#include <QApplication>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString argumentValue(const QStringList &arguments, const QString &name)
{
    const int index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

qint64 argumentPid(const QStringList &arguments, const QString &name)
{
    bool ok = false;
    const qint64 value = argumentValue(arguments, name).toLongLong(&ok);
    return ok && value > 0 ? value : -1;
}

bool waitForProcessExit(qint64 pid, int timeoutMilliseconds)
{
    if (pid <= 0) return true;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!process) return true;
    const DWORD result = WaitForSingleObject(process, static_cast<DWORD>(timeoutMilliseconds));
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
#else
    Q_UNUSED(timeoutMilliseconds)
    return true;
#endif
}

#ifdef Q_OS_WIN
struct CloseWindowRequest
{
    DWORD processId = 0;
};

BOOL CALLBACK closeProcessWindow(HWND window, LPARAM parameter)
{
    auto *request = reinterpret_cast<CloseWindowRequest *>(parameter);
    DWORD ownerProcessId = 0;
    GetWindowThreadProcessId(window, &ownerProcessId);
    if (request && ownerProcessId == request->processId && IsWindowVisible(window)) {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}
#endif

void requestLauncherClose(qint64 pid)
{
#ifdef Q_OS_WIN
    if (pid <= 0) return;
    CloseWindowRequest request;
    request.processId = static_cast<DWORD>(pid);
    EnumWindows(closeProcessWindow, reinterpret_cast<LPARAM>(&request));
#else
    Q_UNUSED(pid)
#endif
}

bool copyDirectoryContents(const QString &sourceDirectory, const QString &targetDirectory, QString *error)
{
    const QDir source(sourceDirectory);
    if (!source.exists()) {
        if (error) *error = QStringLiteral("Не найдена распакованная папка обновления.");
        return false;
    }
    if (!QDir().mkpath(targetDirectory)) {
        if (error) *error = QStringLiteral("Не удалось создать папку установки.");
        return false;
    }
    QDirIterator iterator(sourceDirectory, QDir::NoDotAndDotDot | QDir::AllEntries,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString sourcePath = iterator.next();
        const QFileInfo sourceInfo(sourcePath);
        const QString relativePath = QDir::cleanPath(source.relativeFilePath(sourcePath));
        if (sourceInfo.isSymLink() || QDir::isAbsolutePath(relativePath) ||
            relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../"))) {
            if (error) *error = QStringLiteral("Архив содержит недопустимый путь: %1").arg(relativePath);
            return false;
        }
        const QString targetPath = QDir(targetDirectory).filePath(relativePath);
        if (sourceInfo.isDir()) {
            if (!QDir().mkpath(targetPath)) {
                if (error) *error = QStringLiteral("Не удалось создать папку: %1").arg(relativePath);
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            if (error) *error = QStringLiteral("Не удалось подготовить папку: %1").arg(relativePath);
            return false;
        }
        QFile::remove(targetPath);
        if (!QFile::copy(sourcePath, targetPath)) {
            if (error) *error = QStringLiteral("Не удалось заменить файл: %1").arg(relativePath);
            return false;
        }
    }
    return true;
}

bool normalizedArchivePath(const QString &entryName, QString *relativePath)
{
    QString normalized = entryName;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    normalized = QDir::cleanPath(normalized);
    if (normalized == QStringLiteral(".")) {
        relativePath->clear();
        return true;
    }
    const bool hasDrivePrefix = normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':');
    if (normalized.isEmpty() || normalized.startsWith(QLatin1Char('/')) ||
        normalized.startsWith(QStringLiteral("../")) || normalized == QStringLiteral("..") ||
        QDir::isAbsolutePath(normalized) || hasDrivePrefix) {
        return false;
    }
    *relativePath = normalized;
    return true;
}

bool extractArchive(const QString &archivePath, const QString &temporaryDirectory,
                    QString *extractedRoot, QString *error)
{
    constexpr mz_uint kMaximumArchiveEntries = 4096;
    constexpr quint64 kMaximumExtractedBytes = 1024ull * 1024ull * 1024ull;

    mz_zip_archive archive{};
    if (!mz_zip_reader_init_file(&archive, archivePath.toUtf8().constData(), 0)) {
        if (error) *error = QStringLiteral("Не удалось открыть ZIP обновления.");
        return false;
    }

    bool ok = true;
    const mz_uint entryCount = mz_zip_reader_get_num_files(&archive);
    QSet<QString> extractedPaths;
    quint64 extractedBytes = 0;
    if (entryCount == 0 || entryCount > kMaximumArchiveEntries) {
        if (error) *error = QStringLiteral("ZIP обновления содержит недопустимое число файлов.");
        ok = false;
    }

    for (mz_uint index = 0; ok && index < entryCount; ++index) {
        mz_zip_archive_file_stat stat{};
        QString relativePath;
        if (!mz_zip_reader_file_stat(&archive, index, &stat) ||
            !normalizedArchivePath(QString::fromUtf8(stat.m_filename), &relativePath) ||
            relativePath.isEmpty() || extractedPaths.contains(relativePath) ||
            mz_zip_reader_is_file_encrypted(&archive, index)) {
            if (error) *error = QStringLiteral("ZIP обновления содержит небезопасную или повреждённую запись.");
            ok = false;
            break;
        }
        extractedPaths.insert(relativePath);
        if (!mz_zip_reader_is_file_a_directory(&archive, index)) {
            if (stat.m_uncomp_size > kMaximumExtractedBytes ||
                extractedBytes > kMaximumExtractedBytes - stat.m_uncomp_size) {
                if (error) *error = QStringLiteral("Распакованный размер обновления превышает допустимый лимит.");
                ok = false;
                break;
            }
            extractedBytes += stat.m_uncomp_size;
        }
    }

    for (mz_uint index = 0; ok && index < entryCount; ++index) {
        mz_zip_archive_file_stat stat{};
        QString relativePath;
        if (!mz_zip_reader_file_stat(&archive, index, &stat) ||
            !normalizedArchivePath(QString::fromUtf8(stat.m_filename), &relativePath)) {
            if (error) *error = QStringLiteral("Не удалось прочитать запись ZIP обновления.");
            ok = false;
            break;
        }
        const QString destination = QDir(temporaryDirectory).filePath(relativePath);
        if (mz_zip_reader_is_file_a_directory(&archive, index)) {
            ok = QDir().mkpath(destination);
        } else {
            ok = QDir().mkpath(QFileInfo(destination).absolutePath()) &&
                 mz_zip_reader_extract_to_file(&archive, index, destination.toUtf8().constData(), 0);
        }
        if (!ok && error) *error = QStringLiteral("Не удалось безопасно распаковать ZIP обновления.");
    }

    mz_zip_reader_end(&archive);
    if (!ok) return false;

    const QFileInfoList entries = QDir(temporaryDirectory).entryInfoList(
        QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    *extractedRoot = entries.size() == 1 && entries.first().isDir()
                         ? entries.first().absoluteFilePath()
                         : temporaryDirectory;
    return true;
}

bool launchTemporaryUpdater(const QString &archivePath, const QString &installDirectory,
                            const QString &restartPath, qint64 targetPid, QString *error)
{
    const QString sourceUpdater = QDir(installDirectory).filePath(QStringLiteral("OrvexaUpdater.exe"));
    if (!QFileInfo::exists(sourceUpdater)) {
        if (error) *error = QStringLiteral("OrvexaUpdater.exe не найден в папке установки.");
        return false;
    }
    const QString stagingDirectory = QDir::temp().filePath(
        QStringLiteral("OrvexaUpdater-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    if (!QDir().mkpath(stagingDirectory)) {
        if (error) *error = QStringLiteral("Не удалось создать временную папку updater.");
        return false;
    }
    const QString temporaryUpdater = QDir(stagingDirectory).filePath(QStringLiteral("OrvexaUpdater.exe"));
    if (!QFile::copy(sourceUpdater, temporaryUpdater)) {
        if (error) *error = QStringLiteral("Не удалось подготовить отдельный updater.exe.");
        return false;
    }
    const QDir sourceDirectory(installDirectory);
    const QFileInfoList dlls = sourceDirectory.entryInfoList({QStringLiteral("*.dll")}, QDir::Files);
    for (const QFileInfo &dll : dlls) {
        if (!QFile::copy(dll.absoluteFilePath(), QDir(stagingDirectory).filePath(dll.fileName()))) {
            if (error) *error = QStringLiteral("Не удалось подготовить библиотеку updater: %1").arg(dll.fileName());
            return false;
        }
    }
    const QString sourcePlugins = sourceDirectory.filePath(QStringLiteral("plugins"));
    if (QDir(sourcePlugins).exists() &&
        !copyDirectoryContents(sourcePlugins, QDir(stagingDirectory).filePath(QStringLiteral("plugins")), error)) {
        return false;
    }
    QStringList arguments;
    arguments << QStringLiteral("--apply")
              << QStringLiteral("--archive") << archivePath
              << QStringLiteral("--install-dir") << installDirectory
              << QStringLiteral("--restart") << restartPath
              << QStringLiteral("--target-pid") << QString::number(targetPid);
    if (!QProcess::startDetached(temporaryUpdater, arguments, stagingDirectory)) {
        if (error) *error = QStringLiteral("Не удалось запустить отдельный процесс установки.");
        return false;
    }

    // Отдельный updater уже запущен из временной папки и ждёт PID Atlas Studio.
    // Закрываем основное приложение через его цикл событий: WM_CLOSE может быть
    // перехвачен оконным менеджером Windows или не дойти до окна при модальном диалоге.
    requestLauncherClose(targetPid);
    QTimer::singleShot(0, []() { QCoreApplication::quit(); });
    return true;
}

int applyUpdate(const QStringList &arguments)
{
    const QString archivePath = argumentValue(arguments, QStringLiteral("--archive"));
    const QString installDirectory = argumentValue(arguments, QStringLiteral("--install-dir"));
    const QString restartPath = argumentValue(arguments, QStringLiteral("--restart"));
    const qint64 targetPid = argumentPid(arguments, QStringLiteral("--target-pid"));
    if (archivePath.isEmpty() || installDirectory.isEmpty() || !QFileInfo::exists(archivePath)) {
        QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"),
                              QStringLiteral("Не найден проверенный архив обновления."));
        return 2;
    }
    if (!waitForProcessExit(targetPid, 10 * 60 * 1000)) {
        QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"),
                              QStringLiteral("IDE не закрылся за 10 минут. Обновление не применено."));
        return 3;
    }

    QTemporaryDir temporaryDirectory(QDir::temp().filePath(QStringLiteral("OrvexaUpdateExtract-XXXXXX")));
    if (!temporaryDirectory.isValid()) {
        QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"),
                              QStringLiteral("Не удалось создать временную папку распаковки."));
        return 4;
    }
    QString extractedRoot;
    QString error;
    if (!extractArchive(archivePath, temporaryDirectory.path(), &extractedRoot, &error) ||
        !copyDirectoryContents(extractedRoot, installDirectory, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"),
                              QStringLiteral("Обновление не применено: %1").arg(error));
        return 5;
    }
    if (!restartPath.isEmpty() && QFileInfo::exists(restartPath)) {
        QProcess::startDetached(restartPath, {}, installDirectory);
    }
    return 0;
}

int checkForUpdate(QApplication &application, const QStringList &arguments)
{
    const QString repository = argumentValue(arguments, QStringLiteral("--repo"));
    const QString currentVersion = argumentValue(arguments, QStringLiteral("--current-version"));
    const QString installDirectory = argumentValue(arguments, QStringLiteral("--install-dir"));
    const QString settingsDirectory = argumentValue(arguments, QStringLiteral("--settings-dir"));
    const qint64 parentPid = argumentPid(arguments, QStringLiteral("--parent-pid"));
    if (repository.isEmpty() || currentVersion.isEmpty() || installDirectory.isEmpty() || settingsDirectory.isEmpty()) {
        return 2;
    }

#ifdef Q_OS_WIN
    // В системе может работать только одна проверка обновлений Studio. Если
    // процесс уже показывает окно, новый экземпляр завершается без второго окна.
    HANDLE checkMutex = CreateMutexW(nullptr, FALSE, L"Local\\OrvexaStudioUpdaterCheck");
    if (!checkMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (checkMutex) CloseHandle(checkMutex);
        return 0;
    }
#endif

    QSettings preference(QDir(settingsDirectory).filePath(QStringLiteral("updater.ini")), QSettings::IniFormat);
    atlas::UpdateService service;
    QObject::connect(&service, &atlas::UpdateService::noUpdateAvailable, &application, [&application]() {
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateCheckError, &application, [&application](const QString &) {
        // A background check must remain unobtrusive if the network or GitHub is unavailable.
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateAvailable, &application,
                     [&application, &service, &preference, installDirectory, settingsDirectory, parentPid](const atlas::UpdateRelease &release) {
        if (preference.value(QStringLiteral("ignoredVersion")).toString() == release.version) {
            application.quit();
            return;
        }
        QMessageBox prompt;
        prompt.setIcon(QMessageBox::Information);
        prompt.setWindowTitle(QStringLiteral("Доступно обновление Orvexa Studio"));
        prompt.setText(QStringLiteral("Доступна версия %1.").arg(release.version));
        prompt.setInformativeText(QStringLiteral("Обновление будет скачано с GitHub Releases и проверено по SHA-256. После согласия Orvexa Studio закроется, файлы заменятся, затем IDE запустится снова."));
        QAbstractButton *update = prompt.addButton(QStringLiteral("Обновить"), QMessageBox::AcceptRole);
        QAbstractButton *later = prompt.addButton(QStringLiteral("Позже"), QMessageBox::RejectRole);
        QAbstractButton *ignore = prompt.addButton(QStringLiteral("Игнорировать эту версию"), QMessageBox::DestructiveRole);
        prompt.exec();
        if (prompt.clickedButton() == update) {
            auto *progress = new QProgressDialog(nullptr);
            progress->setWindowTitle(QStringLiteral("Orvexa Studio Updater"));
            progress->setLabelText(QStringLiteral("Скачивается обновление %1…").arg(release.version));
            progress->setCancelButton(nullptr);
            progress->setWindowModality(Qt::ApplicationModal);
            progress->setMinimumDuration(0);
            progress->setRange(0, 0);
            progress->show();

            QObject::connect(&service, &atlas::UpdateService::updateDownloadProgress, &application,
                             [progress](qint64 received, qint64 total) {
                if (!progress) return;
                if (total > 0) {
                    progress->setRange(0, 1000);
                    progress->setValue(int((received * 1000) / total));
                    progress->setLabelText(QStringLiteral("Скачивается обновление: %1 из %2 МБ")
                                           .arg(QString::number(received / (1024.0 * 1024.0), 'f', 1))
                                           .arg(QString::number(total / (1024.0 * 1024.0), 'f', 1)));
                }
            });
            QObject::connect(&service, &atlas::UpdateService::updateDownloadError, &application,
                             [progress](const QString &) {
                if (progress) progress->close();
            });
            QObject::connect(&service, &atlas::UpdateService::updateReadyToInstall, &application,
                             [progress](const QString &, const atlas::UpdateRelease &) {
                if (!progress) return;
                progress->setRange(0, 0);
                progress->setLabelText(QStringLiteral("Проверка завершена. Запускается установка…"));
            });

            const QString updatesDirectory = QDir(settingsDirectory).filePath(QStringLiteral("updates"));
            service.downloadUpdate(release, updatesDirectory);
        } else if (prompt.clickedButton() == ignore) {
            preference.setValue(QStringLiteral("ignoredVersion"), release.version);
            preference.sync();
            application.quit();
        } else if (prompt.clickedButton() == later) {
            application.quit();
        }
    });
    QObject::connect(&service, &atlas::UpdateService::updateDownloadError, &application,
                     [&application](const QString &message) {
        QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"), message);
        application.quit();
    });
    QObject::connect(&service, &atlas::UpdateService::updateReadyToInstall, &application,
                     [&application, installDirectory, parentPid](const QString &archivePath, const atlas::UpdateRelease &) {
        QString error;
        const QString restartPath = QDir(installDirectory).filePath(QStringLiteral("OrvexaStudio.exe"));
        if (!launchTemporaryUpdater(archivePath, installDirectory, restartPath, parentPid, &error)) {
            QMessageBox::critical(nullptr, QStringLiteral("Orvexa Studio Updater"), error);
        }
        application.quit();
    });
    service.checkForUpdate(repository, currentVersion);
    const int result = application.exec();
#ifdef Q_OS_WIN
    CloseHandle(checkMutex);
#endif
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Orvexa Studio Updater"));
    const QStringList arguments = application.arguments();
    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        QTextStream(stdout) << "OrvexaUpdater --check --repo owner/repository --current-version x.y.z "
                               "--install-dir path --settings-dir path --parent-pid pid\\n"
                               "OrvexaUpdater --apply --archive file.zip --install-dir path "
                               "--restart OrvexaStudio.exe --target-pid pid\\n";
        return 0;
    }
    if (arguments.contains(QStringLiteral("--apply"))) return applyUpdate(arguments);
    if (arguments.contains(QStringLiteral("--check"))) return checkForUpdate(application, arguments);
    return 1;
}
