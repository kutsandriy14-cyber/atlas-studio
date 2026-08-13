#include "atlas_studio_window.h"

#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace atlas::studio {
namespace {

QString defaultSource()
{
    return QStringLiteral(
        "on launcher.started\n"
        "  call ui.page.create id=welcome title=\"Hello Atlas\"\n"
        "  call ui.control.add id=welcome type=label text=\"Плагин Atlas Code собран Atlas Studio.\"\n"
        "  call ui.control.add id=welcome type=text text=\"Исходники не попадут в готовый пакет.\"\n"
        "end\n");
}

QStringList toStringList(const QJsonValue &value)
{
    QStringList values;
    if (!value.isArray()) {
        return values;
    }
    for (const QJsonValue &element : value.toArray()) {
        if (element.isString()) {
            values.append(element.toString());
        }
    }
    return values;
}

QJsonArray toJsonArray(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList commaSeparatedValues(const QString &input)
{
    QStringList values;
    for (const QString &part : input.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString value = part.trimmed();
        if (!value.isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

bool validId(const QString &value)
{
    return QRegularExpression(QStringLiteral("^[a-z][a-z0-9.-]{2,127}$")).match(value).hasMatch();
}

bool validVersion(const QString &value)
{
    return QRegularExpression(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$")).match(value).hasMatch();
}

bool safeRelativePath(const QString &value)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(value.trimmed()));
    return !normalized.isEmpty() && normalized != QStringLiteral(".") && normalized != QStringLiteral("..") &&
           !QDir::isAbsolutePath(normalized) && !normalized.startsWith(QStringLiteral("../")) &&
           !normalized.contains(QStringLiteral("/../")) && !normalized.contains(QLatin1Char(':'));
}

bool prohibitedResourceSuffix(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("atlas") || suffix == QLatin1String("atbc") ||
           suffix == QLatin1String("dll") || suffix == QLatin1String("exe") ||
           suffix == QLatin1String("bat") || suffix == QLatin1String("cmd") ||
           suffix == QLatin1String("ps1") || suffix == QLatin1String("com");
}

QString diagnosticsText(const QStringList &diagnostics)
{
    QStringList escaped;
    for (const QString &line : diagnostics) {
        escaped.append(line.toHtmlEscaped());
    }
    return escaped.join(QStringLiteral("<br>"));
}

} // namespace

AtlasStudioWindow::AtlasStudioWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    setupActions();
    resize(1180, 720);
    setMinimumSize(820, 560);
    updateWindowTitle();
}

void AtlasStudioWindow::setupUi()
{
    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    auto *toolbar = new QHBoxLayout();
    m_compileButton = new QPushButton(tr("Скомпилировать ATBC"), root);
    m_packageButton = new QPushButton(tr("Собрать .atp"), root);
    m_installButton = new QPushButton(tr("Открыть папку пакета"), root);
    toolbar->addWidget(m_compileButton);
    toolbar->addWidget(m_packageButton);
    toolbar->addWidget(m_installButton);
    toolbar->addStretch(1);
    auto *formatHint = new QLabel(tr("ATBC 2 · исходники не включаются в пакет"), root);
    formatHint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    formatHint->setToolTip(tr("Готовый .atp содержит manifest.json, объявленные ATBC и объявленные ресурсы — но не исходники .atlas."));
    formatHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    formatHint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toolbar->addWidget(formatHint, 1);
    rootLayout->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setChildrenCollapsible(false);

    auto *metadataWidget = new QWidget(splitter);
    auto *metadataLayout = new QVBoxLayout(metadataWidget);
    metadataLayout->setContentsMargins(0, 0, 5, 0);
    auto *metadataBox = new QGroupBox(tr("Сведения о плагине"), metadataWidget);
    auto *form = new QFormLayout(metadataBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_idEdit = new QLineEdit(metadataBox);
    m_idEdit->setPlaceholderText(tr("org.example.my-plugin"));
    m_nameEdit = new QLineEdit(metadataBox);
    m_nameEdit->setPlaceholderText(tr("Название плагина"));
    m_versionEdit = new QLineEdit(QStringLiteral("1.0.0"), metadataBox);
    m_authorEdit = new QLineEdit(metadataBox);
    m_authorEdit->setPlaceholderText(tr("Ваше имя или команда"));
    m_homepageEdit = new QLineEdit(metadataBox);
    m_homepageEdit->setPlaceholderText(tr("https://… (необязательно)"));
    m_minimumLauncherEdit = new QLineEdit(QStringLiteral("0.4.0"), metadataBox);
    m_pagesEdit = new QLineEdit(metadataBox);
    m_pagesEdit->setPlaceholderText(tr("welcome, settings"));
    m_pagesEdit->setToolTip(tr("Уникальные ID вкладок через запятую. Например: welcome, settings"));
    m_actionsEdit = new QLineEdit(metadataBox);
    m_actionsEdit->setPlaceholderText(tr("server.start, server.stop"));
    m_actionsEdit->setToolTip(tr("Уникальные ID действий Runtime через запятую."));
    m_packageFilesEdit = new QPlainTextEdit(metadataBox);
    m_packageFilesEdit->setPlaceholderText(tr("src/main.atlas -> program/main.atbc\nassets/info.json -> resources/info.json"));
    m_packageFilesEdit->setToolTip(tr("Одна запись на строку: путь в проекте -> путь в .atp. Файлы .atlas компилируются в ATBC; остальные разрешённые файлы копируются как ресурсы. Исходники .atlas и DLL в пакет не добавляются."));
    m_packageFilesEdit->setFixedHeight(88);
    m_categoryCombo = new QComboBox(metadataBox);
    m_categoryCombo->addItems({tr("Общее"), tr("Серверы"), tr("Интерфейс"), tr("Утилиты"), tr("Контент")});
    m_descriptionEdit = new QPlainTextEdit(metadataBox);
    m_descriptionEdit->setPlaceholderText(tr("Кратко опишите назначение расширения"));
    m_descriptionEdit->setMaximumBlockCount(20);
    m_descriptionEdit->setFixedHeight(82);
    form->addRow(tr("ID *"), m_idEdit);
    form->addRow(tr("Название *"), m_nameEdit);
    form->addRow(tr("Версия *"), m_versionEdit);
    form->addRow(tr("Автор *"), m_authorEdit);
    form->addRow(tr("Описание *"), m_descriptionEdit);
    form->addRow(tr("Категория"), m_categoryCombo);
    form->addRow(tr("Мин. Launcher"), m_minimumLauncherEdit);
    form->addRow(tr("Страницы UI"), m_pagesEdit);
    form->addRow(tr("Действия Runtime"), m_actionsEdit);
    form->addRow(tr("Файлы пакета"), m_packageFilesEdit);
    form->addRow(tr("Сайт"), m_homepageEdit);
    metadataLayout->addWidget(metadataBox);

    auto *permissionsBox = new QGroupBox(tr("Запрашиваемые разрешения"), metadataWidget);
    auto *permissionsLayout = new QVBoxLayout(permissionsBox);
    m_storagePermission = new QCheckBox(tr("Данные плагина (изолированный каталог)"), permissionsBox);
    m_storagePermission->setProperty("permission", QStringLiteral("files.plugin-data"));
    m_storagePermission->setToolTip(tr("Разрешает доступ только к собственному изолированному каталогу плагина."));
    m_networkPermission = new QCheckBox(tr("Метаданные сети (API Runtime)"), permissionsBox);
    m_networkPermission->setProperty("permission", QStringLiteral("network.metadata"));
    m_networkPermission->setToolTip(tr("Разрешает только запрос метаданных через ограниченный API Runtime."));
    m_serversControlPermission = new QCheckBox(tr("Управление сервером Minecraft"), permissionsBox);
    m_serversControlPermission->setProperty("permission", QStringLiteral("servers.control"));
    m_serversControlPermission->setToolTip(tr("Разрешает операции управления сервером только через Runtime."));
    m_serversConsolePermission = new QCheckBox(tr("Консоль сервера Minecraft"), permissionsBox);
    m_serversConsolePermission->setProperty("permission", QStringLiteral("servers.console"));
    m_serversConsolePermission->setToolTip(tr("Разрешает доступ к консоли сервера только через Runtime."));
    permissionsLayout->addWidget(m_storagePermission);
    permissionsLayout->addWidget(m_networkPermission);
    permissionsLayout->addWidget(m_serversControlPermission);
    permissionsLayout->addWidget(m_serversConsolePermission);
    auto *permissionHint = new QLabel(tr("Пользователь подтверждает их при установке .atp. Они не дают доступа к DLL, памяти или произвольным файлам."), permissionsBox);
    permissionHint->setWordWrap(true);
    permissionHint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    permissionsLayout->addWidget(permissionHint);
    metadataLayout->addWidget(permissionsBox);
    metadataLayout->addStretch(1);

    auto *metadataScroll = new QScrollArea(splitter);
    metadataScroll->setWidgetResizable(true);
    metadataScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    metadataScroll->setFrameShape(QFrame::NoFrame);
    metadataScroll->setWidget(metadataWidget);
    splitter->addWidget(metadataScroll);

    auto *editorWidget = new QWidget(splitter);
    auto *editorLayout = new QVBoxLayout(editorWidget);
    editorLayout->setContentsMargins(5, 0, 0, 0);
    auto *editorLabel = new QLabel(tr("Исходный код Atlas Code — src/main.atlas"), editorWidget);
    editorLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    editorLayout->addWidget(editorLabel);
    m_sourceEdit = new QPlainTextEdit(editorWidget);
    m_sourceEdit->setTabStopDistance(32.0);
    m_sourceEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_sourceEdit->setPlaceholderText(tr("Введите Atlas Code…"));
    m_sourceEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editorLayout->addWidget(m_sourceEdit, 1);
    auto *diagnosticsLabel = new QLabel(tr("Проверка и журнал сборки"), editorWidget);
    diagnosticsLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    editorLayout->addWidget(diagnosticsLabel);
    m_diagnosticsEdit = new QTextEdit(editorWidget);
    m_diagnosticsEdit->setReadOnly(true);
    m_diagnosticsEdit->setMinimumHeight(145);
    m_diagnosticsEdit->setHtml(tr("<span style='color:#6b7280'>Создайте или откройте проект Atlas Code.</span>"));
    editorLayout->addWidget(m_diagnosticsEdit);
    splitter->addWidget(editorWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({355, 850});
    rootLayout->addWidget(splitter, 1);

    setCentralWidget(root);
    connect(m_compileButton, &QPushButton::clicked, this, &AtlasStudioWindow::compileAtbc);
    connect(m_packageButton, &QPushButton::clicked, this, &AtlasStudioWindow::packageProject);
    connect(m_installButton, &QPushButton::clicked, this, &AtlasStudioWindow::installPackage);
    const auto changed = [this] { markModified(); };
    connect(m_idEdit, &QLineEdit::textEdited, this, changed);
    connect(m_nameEdit, &QLineEdit::textEdited, this, changed);
    connect(m_versionEdit, &QLineEdit::textEdited, this, changed);
    connect(m_authorEdit, &QLineEdit::textEdited, this, changed);
    connect(m_homepageEdit, &QLineEdit::textEdited, this, changed);
    connect(m_minimumLauncherEdit, &QLineEdit::textEdited, this, changed);
    connect(m_pagesEdit, &QLineEdit::textEdited, this, changed);
    connect(m_actionsEdit, &QLineEdit::textEdited, this, changed);
    connect(m_packageFilesEdit, &QPlainTextEdit::textChanged, this, changed);
    connect(m_categoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, changed);
    connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this, changed);
    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this, changed);
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission}) {
        connect(box, &QCheckBox::toggled, this, changed);
    }
}

void AtlasStudioWindow::setupActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&Файл"));
    m_newAction = fileMenu->addAction(tr("Новый проект…"), this, &AtlasStudioWindow::createProject, QKeySequence::New);
    m_openAction = fileMenu->addAction(tr("Открыть проект…"), this, &AtlasStudioWindow::openProject, QKeySequence::Open);
    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("Сохранить"), this, [this] { saveProject(); }, QKeySequence::Save);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Выход"), this, &QWidget::close, QKeySequence::Quit);
    statusBar()->showMessage(tr("Atlas Studio готов"));
}

void AtlasStudioWindow::createProject()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Выберите пустой каталог нового проекта"));
    if (directory.isEmpty()) {
        return;
    }
    QDir projectDirectory(directory);
    if (!projectDirectory.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty() &&
        QMessageBox::question(this, tr("Каталог не пуст"), tr("В выбранном каталоге уже есть файлы. Продолжить и использовать его?")) != QMessageBox::Yes) {
        return;
    }
    projectDirectory.mkpath(QStringLiteral("src"));
    projectDirectory.mkpath(QStringLiteral("build"));
    projectDirectory.mkpath(QStringLiteral("dist"));
    setProjectDirectory(directory);
    m_idEdit->setText(QStringLiteral("org.example.my-plugin"));
    m_nameEdit->setText(tr("Мой плагин"));
    m_versionEdit->setText(QStringLiteral("1.0.0"));
    m_authorEdit->setText(tr("Автор"));
    m_homepageEdit->clear();
    m_minimumLauncherEdit->setText(QStringLiteral("0.4.0"));
    m_pagesEdit->setText(QStringLiteral("welcome"));
    m_actionsEdit->clear();
    m_packageFilesEdit->setPlainText(QStringLiteral("src/main.atlas -> program/main.atbc"));
    m_descriptionEdit->setPlainText(tr("Первый плагин Atlas Code."));
    m_categoryCombo->setCurrentIndex(0);
    applyPermissions({});
    m_sourceEdit->setPlainText(defaultSource());
    m_modified = true;
    if (!saveProject()) {
        return;
    }
    setDiagnostics({tr("Создан новый проект. Отредактируйте код и нажмите «Собрать .atp»." )}, true);
}

void AtlasStudioWindow::openProject()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Открыть каталог Atlas Code"));
    if (directory.isEmpty()) {
        return;
    }
    QString error;
    if (!loadProject(directory, &error)) {
        QMessageBox::warning(this, tr("Не удалось открыть проект"), error);
    }
}

bool AtlasStudioWindow::saveProject()
{
    if (m_projectDirectory.isEmpty()) {
        createProject();
        return !m_projectDirectory.isEmpty() && !m_modified;
    }
    QString error;
    if (!writeProject(&error)) {
        QMessageBox::warning(this, tr("Не удалось сохранить"), error);
        return false;
    }
    m_modified = false;
    updateWindowTitle();
    statusBar()->showMessage(tr("Проект сохранён"), 2500);
    return true;
}

bool AtlasStudioWindow::declaredPackageFiles(QVector<ProjectPackageFile> *files, QStringList *diagnostics) const
{
    files->clear();
    const QStringList lines = m_packageFilesEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        diagnostics->append(tr("Укажите хотя бы один файл пакета в формате «источник -> путь в .atp»."));
        return false;
    }
    if (lines.size() > 128) {
        diagnostics->append(tr("В одном .atp допускается не более 128 явно объявленных файлов."));
        return false;
    }

    QSet<QString> archivePaths;
    int programCount = 0;
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index).trimmed();
        const QStringList parts = line.split(QStringLiteral("->"));
        if (parts.size() != 2) {
            diagnostics->append(tr("Строка файлов %1: ожидается формат «источник -> путь в .atp». ").arg(index + 1));
            continue;
        }
        const QString sourcePath = QDir::cleanPath(QDir::fromNativeSeparators(parts.at(0).trimmed()));
        const QString archivePath = QDir::cleanPath(QDir::fromNativeSeparators(parts.at(1).trimmed()));
        if (!safeRelativePath(sourcePath) || !safeRelativePath(archivePath) || archivePath == QLatin1String("manifest.json")) {
            diagnostics->append(tr("Строка файлов %1: путь должен быть безопасным и относительным.").arg(index + 1));
            continue;
        }
        const QString archiveKey = archivePath.toCaseFolded();
        if (archivePaths.contains(archiveKey)) {
            diagnostics->append(tr("Строка файлов %1: путь в .atp повторяется: %2.").arg(index + 1).arg(archivePath));
            continue;
        }
        const bool program = sourcePath.endsWith(QStringLiteral(".atlas"), Qt::CaseInsensitive);
        if (program) {
            if (!archivePath.startsWith(QStringLiteral("program/")) || !archivePath.endsWith(QStringLiteral(".atbc"), Qt::CaseInsensitive)) {
                diagnostics->append(tr("Строка файлов %1: Atlas Code должен собираться в program/*.atbc.").arg(index + 1));
                continue;
            }
            ++programCount;
        } else if (!archivePath.startsWith(QStringLiteral("resources/")) || prohibitedResourceSuffix(sourcePath)) {
            diagnostics->append(tr("Строка файлов %1: ресурс должен быть в resources/ и не может быть исходником, ATBC или исполняемым файлом.").arg(index + 1));
            continue;
        }
        archivePaths.insert(archiveKey);
        files->append({sourcePath, archivePath, program});
    }
    if (programCount == 0) {
        diagnostics->append(tr("В пакете должна быть хотя бы одна программа Atlas Code (*.atlas -> program/*.atbc)."));
    }
    if (programCount > 32) {
        diagnostics->append(tr("В одном .atp допускается не более 32 программ Atlas Code."));
    }
    return diagnostics->isEmpty();
}

bool AtlasStudioWindow::validateProject(QStringList *diagnostics) const
{
    if (m_projectDirectory.isEmpty()) {
        diagnostics->append(tr("Сначала создайте или откройте проект."));
    }
    if (!validId(m_idEdit->text().trimmed())) {
        diagnostics->append(tr("ID: используйте строчные латинские буквы, цифры, точки и дефисы; пример — org.example.plugin."));
    }
    if (m_nameEdit->text().trimmed().isEmpty() || m_nameEdit->text().trimmed().size() > 120) {
        diagnostics->append(tr("Название обязательно и не должно превышать 120 символов."));
    }
    if (!validVersion(m_versionEdit->text().trimmed()) || !validVersion(m_minimumLauncherEdit->text().trimmed())) {
        diagnostics->append(tr("Версии должны иметь формат SemVer, например 1.0.0."));
    }
    if (m_authorEdit->text().trimmed().isEmpty() || m_descriptionEdit->toPlainText().trimmed().isEmpty()) {
        diagnostics->append(tr("Автор и описание обязательны."));
    }
    if (m_descriptionEdit->toPlainText().trimmed().size() > 600) {
        diagnostics->append(tr("Описание не должно превышать 600 символов."));
    }
    const auto validateDeclarations = [diagnostics](const QStringList &values, const int maximum, const QString &label) {
        if (values.size() > maximum) {
            diagnostics->append(QObject::tr("%1: превышен лимит из %2 деклараций.").arg(label).arg(maximum));
            return;
        }
        QStringList seen;
        for (const QString &value : values) {
            if (!validId(value) || seen.contains(value)) {
                diagnostics->append(QObject::tr("%1: используйте уникальные ID из строчных латинских букв, цифр, точек и дефисов.").arg(label));
                return;
            }
            seen.append(value);
        }
    };
    validateDeclarations(commaSeparatedValues(m_pagesEdit->text()), 16, tr("Страницы UI"));
    validateDeclarations(commaSeparatedValues(m_actionsEdit->text()), 64, tr("Действия Runtime"));
    QVector<ProjectPackageFile> files;
    declaredPackageFiles(&files, diagnostics);
    if (m_sourceEdit->toPlainText().trimmed().isEmpty()) {
        diagnostics->append(tr("Исходный код Atlas Code пуст."));
    }
    return diagnostics->isEmpty();
}

bool AtlasStudioWindow::compileCurrentProject(QStringList *diagnostics, QVector<ProjectPackageFile> *compiledFiles)
{
    if (!validateProject(diagnostics)) {
        return false;
    }
    if (!saveProject()) {
        diagnostics->append(tr("Сначала исправьте ошибку сохранения проекта."));
        return false;
    }

    QVector<ProjectPackageFile> declared;
    if (!declaredPackageFiles(&declared, diagnostics)) {
        return false;
    }

    QVariantList permissions;
    for (const QString &permission : selectedPermissions()) {
        permissions.append(permission);
    }
    QDir projectRoot(m_projectDirectory);
    QVector<ProjectPackageFile> prepared;
    int programs = 0;
    for (const ProjectPackageFile &file : declared) {
        const QString inputPath = projectRoot.filePath(file.sourcePath);
        if (!file.program) {
            if (!QFileInfo(inputPath).isFile()) {
                diagnostics->append(tr("Не найден объявленный ресурс: %1").arg(file.sourcePath));
                return false;
            }
            prepared.append({inputPath, file.archivePath, false});
            continue;
        }

        QFile source(inputPath);
        if (!source.open(QIODevice::ReadOnly)) {
            diagnostics->append(tr("Не удалось открыть исходник %1: %2").arg(file.sourcePath, source.errorString()));
            return false;
        }
        const runtime::AtlasCodeCompileResult compilation = runtime::AtlasCodeCompiler::compile(
            file.sourcePath, QString::fromUtf8(source.readAll()));
        if (!compilation.success) {
            for (const runtime::AtlasCodeDiagnostic &diagnostic : compilation.diagnostics) {
                diagnostics->append(diagnostic.line > 0
                    ? tr("%1, строка %2: %3").arg(file.sourcePath).arg(diagnostic.line).arg(diagnostic.message)
                    : tr("%1: %2").arg(file.sourcePath, diagnostic.message));
            }
            return false;
        }

        runtime::AtlasCodeProgram program = compilation.program;
        program.metadata = {
            {QStringLiteral("id"), m_idEdit->text().trimmed()},
            {QStringLiteral("name"), m_nameEdit->text().trimmed()},
            {QStringLiteral("version"), m_versionEdit->text().trimmed()},
            {QStringLiteral("author"), m_authorEdit->text().trimmed()},
            {QStringLiteral("description"), m_descriptionEdit->toPlainText().trimmed()},
            {QStringLiteral("minRuntime"), QStringLiteral("0.4.0")},
            {QStringLiteral("minLauncher"), m_minimumLauncherEdit->text().trimmed()},
            {QStringLiteral("category"), m_categoryCombo->currentText()},
            {QStringLiteral("programPath"), file.archivePath},
            {QStringLiteral("pages"), commaSeparatedValues(m_pagesEdit->text())},
            {QStringLiteral("actions"), commaSeparatedValues(m_actionsEdit->text())},
            {QStringLiteral("permissions"), permissions},
            {QStringLiteral("homepage"), m_homepageEdit->text().trimmed()}
        };
        QString encodingError;
        const QByteArray atbc = runtime::AtlasCodeCompiler::encodeAtbc(program, &encodingError);
        if (atbc.isEmpty()) {
            diagnostics->append(tr("Не удалось сформировать ATBC для %1: %2").arg(file.sourcePath, encodingError));
            return false;
        }
        const QString outputPath = projectRoot.filePath(QStringLiteral("build/") + file.archivePath);
        if (!QDir().mkpath(QFileInfo(outputPath).absolutePath())) {
            diagnostics->append(tr("Не удалось подготовить каталог ATBC: %1").arg(file.archivePath));
            return false;
        }
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly) || output.write(atbc) != atbc.size() || !output.commit()) {
            diagnostics->append(tr("Не удалось сохранить ATBC %1: %2").arg(file.archivePath, output.errorString()));
            return false;
        }
        prepared.append({outputPath, file.archivePath, true});
        ++programs;
        diagnostics->append(tr("ATBC 2 создан: build/%1 (%2 байт). Исходник %3 в пакет не добавляется.")
            .arg(file.archivePath).arg(atbc.size()).arg(file.sourcePath));
    }
    if (compiledFiles) {
        *compiledFiles = prepared;
    }
    diagnostics->append(tr("Подготовлено программ: %1; ресурсов: %2.").arg(programs).arg(prepared.size() - programs));
    return true;
}

void AtlasStudioWindow::compileAtbc()
{
    QStringList diagnostics;
    QVector<ProjectPackageFile> compiledFiles;
    const bool success = compileCurrentProject(&diagnostics, &compiledFiles);
    setDiagnostics(diagnostics, success);
    if (success) {
        statusBar()->showMessage(tr("Бинарные ATBC созданы: %1").arg(compiledFiles.size()), 3000);
    }
}

void AtlasStudioWindow::packageProject()
{
    QStringList diagnostics;
    QVector<ProjectPackageFile> compiledFiles;
    if (!compileCurrentProject(&diagnostics, &compiledFiles)) {
        setDiagnostics(diagnostics, false);
        return;
    }
    packager::AtlasPackageRequest request{m_projectDirectory, atbcPath(), packagePath()};
    for (const ProjectPackageFile &file : compiledFiles) {
        request.files.append({file.sourcePath, file.archivePath, file.program});
    }
    const packager::AtlasPackageResult package = packager::AtlasPackageBuilder::build(request);
    diagnostics.append(package.diagnostics);
    setDiagnostics(diagnostics, package.success);
    if (package.success) {
        statusBar()->showMessage(tr("Готово: %1").arg(package.outputPath), 5000);
    }
}

void AtlasStudioWindow::installPackage()
{
    if (!QFileInfo::exists(packagePath())) {
        packageProject();
    }
    if (QFileInfo::exists(packagePath())) {
        const QFileInfo packageFile(packagePath());
        QDesktopServices::openUrl(QUrl::fromLocalFile(packageFile.absolutePath()));
        QMessageBox::information(this, tr("Установите пакет в Launcher"),
            tr("Папка с готовым пакетом открыта:\n%1\n\nВ Atlas Launcher откройте «Плагины → Установить .atp» и выберите файл %2.")
                .arg(QDir::toNativeSeparators(packageFile.absolutePath()), packageFile.fileName()));
    }
}

void AtlasStudioWindow::markModified()
{
    if (!m_projectDirectory.isEmpty()) {
        m_modified = true;
        updateWindowTitle();
    }
}

bool AtlasStudioWindow::confirmDiscardChanges()
{
    if (!m_modified) {
        return true;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(this, tr("Есть несохранённые изменения"),
        tr("Сохранить изменения перед продолжением?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        return saveProject();
    }
    return answer == QMessageBox::Discard;
}

void AtlasStudioWindow::closeEvent(QCloseEvent *event)
{
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void AtlasStudioWindow::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = QDir::cleanPath(directory);
    m_modified = false;
    updateWindowTitle();
}

bool AtlasStudioWindow::loadProject(const QString &directory, QString *error)
{
    QFile projectFile(QDir(directory).filePath(QStringLiteral("atlas-project.json")));
    if (!projectFile.open(QIODevice::ReadOnly)) {
        *error = tr("Не удалось открыть atlas-project.json: %1").arg(projectFile.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(projectFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = tr("atlas-project.json повреждён: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject project = document.object();
    const QString mainSourcePath = project.value(QStringLiteral("source")).toString(QStringLiteral("src/main.atlas"));
    QFile source(QDir(directory).filePath(mainSourcePath));
    if (!source.open(QIODevice::ReadOnly)) {
        *error = tr("Не удалось открыть исходник: %1").arg(source.errorString());
        return false;
    }
    setProjectDirectory(directory);
    m_idEdit->setText(project.value(QStringLiteral("id")).toString());
    m_nameEdit->setText(project.value(QStringLiteral("name")).toString());
    m_versionEdit->setText(project.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0")));
    m_authorEdit->setText(project.value(QStringLiteral("publisher")).toString());
    m_descriptionEdit->setPlainText(project.value(QStringLiteral("description")).toString());
    m_homepageEdit->setText(project.value(QStringLiteral("homepage")).toString());
    m_minimumLauncherEdit->setText(project.value(QStringLiteral("minimumLauncherVersion")).toString(QStringLiteral("0.4.0")));
    m_pagesEdit->setText(toStringList(project.value(QStringLiteral("pages"))).join(QStringLiteral(", ")));
    m_actionsEdit->setText(toStringList(project.value(QStringLiteral("actions"))).join(QStringLiteral(", ")));
    const int categoryIndex = m_categoryCombo->findText(project.value(QStringLiteral("category")).toString());
    m_categoryCombo->setCurrentIndex(categoryIndex < 0 ? 0 : categoryIndex);
    applyPermissions(toStringList(project.value(QStringLiteral("permissions"))));
    QStringList packageFiles = toStringList(project.value(QStringLiteral("packageFiles")));
    if (packageFiles.isEmpty()) {
        packageFiles.append(QStringLiteral("%1 -> %2").arg(mainSourcePath,
            project.value(QStringLiteral("entryPoint")).toString(QStringLiteral("program/main.atbc"))));
    }
    m_packageFilesEdit->setPlainText(packageFiles.join(QLatin1Char('\n')));
    m_sourceEdit->setPlainText(QString::fromUtf8(source.readAll()));
    m_modified = false;
    updateWindowTitle();
    setDiagnostics({tr("Открыт проект: %1").arg(m_projectDirectory)}, true);
    return true;
}

bool AtlasStudioWindow::writeProject(QString *error) const
{
    QDir root(m_projectDirectory);
    if (!root.mkpath(QStringLiteral("src")) || !root.mkpath(QStringLiteral("build")) || !root.mkpath(QStringLiteral("dist"))) {
        *error = tr("Не удалось подготовить структуру каталога проекта.");
        return false;
    }
    QJsonArray packageFiles;
    for (const QString &line : m_packageFilesEdit->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        packageFiles.append(line.trimmed());
    }
    const QJsonObject project = {
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("id"), m_idEdit->text().trimmed()},
        {QStringLiteral("name"), m_nameEdit->text().trimmed()},
        {QStringLiteral("version"), m_versionEdit->text().trimmed()},
        {QStringLiteral("publisher"), m_authorEdit->text().trimmed()},
        {QStringLiteral("description"), m_descriptionEdit->toPlainText().trimmed()},
        {QStringLiteral("homepage"), m_homepageEdit->text().trimmed()},
        {QStringLiteral("category"), m_categoryCombo->currentText()},
        {QStringLiteral("minimumLauncherVersion"), m_minimumLauncherEdit->text().trimmed()},
        {QStringLiteral("permissions"), toJsonArray(selectedPermissions())},
        {QStringLiteral("source"), QStringLiteral("src/main.atlas")},
        {QStringLiteral("entryPoint"), QStringLiteral("program/main.atbc")},
        {QStringLiteral("packageFiles"), packageFiles},
        {QStringLiteral("pages"), toJsonArray(commaSeparatedValues(m_pagesEdit->text()))},
        {QStringLiteral("actions"), toJsonArray(commaSeparatedValues(m_actionsEdit->text()))}
    };
    QSaveFile projectFile(projectFilePath());
    if (!projectFile.open(QIODevice::WriteOnly) || projectFile.write(QJsonDocument(project).toJson(QJsonDocument::Indented)) < 0 || !projectFile.commit()) {
        *error = tr("Не удалось сохранить atlas-project.json: %1").arg(projectFile.errorString());
        return false;
    }
    QSaveFile source(sourceFilePath());
    const QByteArray sourceBytes = m_sourceEdit->toPlainText().toUtf8();
    if (!source.open(QIODevice::WriteOnly) || source.write(sourceBytes) != sourceBytes.size() || !source.commit()) {
        *error = tr("Не удалось сохранить main.atlas: %1").arg(source.errorString());
        return false;
    }
    return true;
}

QString AtlasStudioWindow::projectFilePath() const { return QDir(m_projectDirectory).filePath(QStringLiteral("atlas-project.json")); }
QString AtlasStudioWindow::sourceFilePath() const { return QDir(m_projectDirectory).filePath(QStringLiteral("src/main.atlas")); }
QString AtlasStudioWindow::buildDirectoryPath() const { return QDir(m_projectDirectory).filePath(QStringLiteral("build")); }
QString AtlasStudioWindow::atbcPath() const { return QDir(buildDirectoryPath()).filePath(QStringLiteral("main.atbc")); }
QString AtlasStudioWindow::packagePath() const { return QDir(m_projectDirectory).filePath(QStringLiteral("dist/%1-%2.atp").arg(m_idEdit->text().trimmed(), m_versionEdit->text().trimmed())); }

QStringList AtlasStudioWindow::selectedPermissions() const
{
    QStringList result;
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission}) {
        if (box->isChecked()) {
            result.append(box->property("permission").toString());
        }
    }
    return result;
}

void AtlasStudioWindow::applyPermissions(const QStringList &permissions)
{
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission}) {
        box->setChecked(permissions.contains(box->property("permission").toString()));
    }
}

void AtlasStudioWindow::setDiagnostics(const QStringList &diagnostics, const bool success)
{
    m_diagnosticsEdit->setHtml(QStringLiteral("<div style='color:%1'>%2</div>")
        .arg(success ? QStringLiteral("#166534") : QStringLiteral("#b91c1c"), diagnosticsText(diagnostics)));
}

void AtlasStudioWindow::updateWindowTitle()
{
    const QString name = m_projectDirectory.isEmpty() ? tr("без проекта") : QFileInfo(m_projectDirectory).fileName();
    setWindowTitle(QStringLiteral("%1Atlas Studio — %2").arg(m_modified ? QStringLiteral("* ") : QString(), name));
}

} // namespace atlas::studio
