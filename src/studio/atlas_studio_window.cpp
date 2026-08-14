#include "atlas_studio_window.h"

#include "core/atlas_code_compiler.h"
#include "packager/atlas_package_builder.h"
#include "studio/atlas_code_highlighter.h"
#include "studio/studio_theme.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QPropertyAnimation>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextStream>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace atlas::studio {
namespace {

QString defaultSource()
{
    return QStringLiteral(
        "on launcher.started\n"
        "  call ui.page.create id=welcome title=\"Статус Orvexa\" icon=folder\n"
        "  call ui.control.add id=welcome controlId=content-state type=label text=\"Сводка модов доступна через Менеджер контента.\"\n"
        "  call ui.control.add id=welcome controlId=refresh-mods type=button text=\"Обновить сводку модов\"\n"
        "  call content.summary category=mods\n"
        "  call ui.feedback.notify message=\"Плагин Orvexa готов.\" severity=success\n"
        "end\n"
        "\n"
        "on content.updated\n"
        "  call ui.control.update id=welcome controlId=content-state text=\"Сводка контента обновлена.\"\n"
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

class CodeEditor;

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodeEditor *m_editor = nullptr;
};

class CodeEditor final : public QPlainTextEdit
{
public:
    explicit CodeEditor(QWidget *parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this))
    {
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setTabStopDistance(32.0);
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &rect, const int dy) {
            if (dy != 0) {
                m_lineNumberArea->scroll(0, dy);
            } else {
                m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
            }
            if (rect.contains(viewport()->rect())) {
                updateLineNumberAreaWidth();
            }
        });
        updateLineNumberAreaWidth();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int maximum = qMax(1, blockCount());
        while (maximum >= 10) {
            maximum /= 10;
            ++digits;
        }
        return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor(QStringLiteral("#252526")));
        painter.setPen(QColor(QStringLiteral("#858585")));

        QTextBlock block = firstVisibleBlock();
        int number = block.blockNumber() + 1;
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                painter.drawText(0, top, m_lineNumberArea->width() - 6, fontMetrics().height(),
                    Qt::AlignRight, QString::number(number));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++number;
        }
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
    }

private:
    void updateLineNumberAreaWidth()
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    LineNumberArea *m_lineNumberArea = nullptr;

    friend class LineNumberArea;
};

LineNumberArea::LineNumberArea(CodeEditor *editor)
    : QWidget(editor), m_editor(editor)
{
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_editor ? m_editor->lineNumberAreaWidth() : 0, 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event)
{
    if (m_editor) {
        m_editor->lineNumberAreaPaintEvent(event);
    }
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
    root->setObjectName(QStringLiteral("studioRoot"));
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *commandBar = new QWidget(root);
    commandBar->setObjectName(QStringLiteral("commandBar"));
    auto *commandLayout = new QHBoxLayout(commandBar);
    commandLayout->setContentsMargins(14, 7, 14, 7);
    commandLayout->setSpacing(8);
    auto *brandIcon = new QLabel(commandBar);
    brandIcon->setObjectName(QStringLiteral("brandIcon"));
    brandIcon->setPixmap(StudioTheme::icon(QStringLiteral("orvexa-mark"), 22).pixmap(QSize(22, 22)));
    auto *brand = new QLabel(tr("ORVEXA STUDIO"), commandBar);
    brand->setObjectName(QStringLiteral("studioBrand"));
    auto *projectCaption = new QLabel(tr("Создавайте безопасные расширения Orvexa Code"), commandBar);
    projectCaption->setObjectName(QStringLiteral("projectCaption"));
    projectCaption->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_compileButton = new QPushButton(StudioTheme::icon(QStringLiteral("compile"), 14), tr("Скомпилировать"), commandBar);
    m_compileButton->setObjectName(QStringLiteral("compileButton"));
    m_packageButton = new QPushButton(StudioTheme::icon(QStringLiteral("package"), 14), tr("Собрать .atp"), commandBar);
    m_packageButton->setObjectName(QStringLiteral("packageButton"));
    m_installButton = new QPushButton(StudioTheme::icon(QStringLiteral("upload"), 14), tr("Показать пакет"), commandBar);
    m_installButton->setObjectName(QStringLiteral("showPackageButton"));
    commandLayout->addWidget(brandIcon);
    commandLayout->addWidget(brand);
    commandLayout->addWidget(projectCaption, 1);
    commandLayout->addWidget(m_compileButton);
    commandLayout->addWidget(m_packageButton);
    commandLayout->addWidget(m_installButton);
    rootLayout->addWidget(commandBar);

    auto *mainSplitter = new QSplitter(Qt::Horizontal, root);
    mainSplitter->setObjectName(QStringLiteral("workbenchSplitter"));
    mainSplitter->setChildrenCollapsible(false);

    auto *explorer = new QWidget(mainSplitter);
    explorer->setObjectName(QStringLiteral("explorerPane"));
    auto *explorerLayout = new QVBoxLayout(explorer);
    explorerLayout->setContentsMargins(9, 10, 8, 8);
    explorerLayout->setSpacing(7);
    auto *explorerTitle = new QLabel(explorer);
    explorerTitle->setObjectName(QStringLiteral("explorerTitle"));
    explorerTitle->setText(QStringLiteral("%1 %2").arg(
        StudioTheme::icon(QStringLiteral("module"), 12).pixmap(QSize(12, 12)).toImage().isNull() ? QString() : QString()));
    explorerTitle->setPixmap(StudioTheme::icon(QStringLiteral("module"), 12).pixmap(QSize(12, 12)));
    explorerTitle->setText(tr("ПРОВОДНИК"));
    auto *explorerHint = new QLabel(tr("Проект Orvexa Code"), explorer);
    explorerHint->setObjectName(QStringLiteral("explorerHint"));
    explorerHint->setWordWrap(true);
    m_projectTree = new QTreeWidget(explorer);
    m_projectTree->setObjectName(QStringLiteral("projectTree"));
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->setIconSize(QSize(16, 16));
    m_projectTree->setIndentation(14);
    m_projectTree->setMinimumWidth(190);
    auto *newProjectButton = new QPushButton(StudioTheme::icon(QStringLiteral("file-folder"), 14), tr("Новый проект…"), explorer);
    auto *openProjectButton = new QPushButton(StudioTheme::icon(QStringLiteral("upload"), 14), tr("Открыть проект…"), explorer);
    newProjectButton->setObjectName(QStringLiteral("newProjectButton"));
    openProjectButton->setObjectName(QStringLiteral("openProjectButton"));
    explorerLayout->addWidget(explorerTitle);
    explorerLayout->addWidget(explorerHint);
    explorerLayout->addWidget(m_projectTree, 1);
    explorerLayout->addWidget(newProjectButton);
    explorerLayout->addWidget(openProjectButton);
    mainSplitter->addWidget(explorer);

    auto *workbench = new QSplitter(Qt::Vertical, mainSplitter);
    workbench->setObjectName(QStringLiteral("editorSplitter"));
    workbench->setChildrenCollapsible(false);
    m_editorTabs = new QTabWidget(workbench);
    m_editorTabs->setObjectName(QStringLiteral("editorTabs"));
    m_editorTabs->setDocumentMode(true);
    m_editorTabs->setTabsClosable(false);

    auto *sourceTab = new QWidget(m_editorTabs);
    auto *sourceLayout = new QVBoxLayout(sourceTab);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(0);
    m_sourceEdit = new CodeEditor(sourceTab);
    m_sourceEdit->setObjectName(QStringLiteral("atlasCodeEditor"));
    m_sourceEdit->setPlaceholderText(tr("Введите Orvexa Code…"));
    m_sourceEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sourceEdit->setTabStopDistance(24.0);
    m_highlighter = new AtlasCodeHighlighter(m_sourceEdit->document());
    sourceLayout->addWidget(m_sourceEdit);
    m_editorTabs->addTab(sourceTab, tr("main.atlas"));

    auto *metadataWidget = new QWidget(m_editorTabs);
    auto *metadataLayout = new QVBoxLayout(metadataWidget);
    metadataLayout->setContentsMargins(12, 12, 12, 12);
    metadataLayout->setSpacing(10);
    auto *metadataIntro = new QLabel(tr("Сведения пакета определяют, как Launcher покажет расширение и какие безопасные возможности оно запросит."), metadataWidget);
    metadataIntro->setObjectName(QStringLiteral("metadataIntro"));
    metadataIntro->setWordWrap(true);
    metadataLayout->addWidget(metadataIntro);

    auto *metadataBox = new QGroupBox(tr("Метаданные расширения"), metadataWidget);
    auto *form = new QFormLayout(metadataBox);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_idEdit = new QLineEdit(metadataBox);
    m_idEdit->setPlaceholderText(tr("org.example.my-plugin"));
    m_nameEdit = new QLineEdit(metadataBox);
    m_nameEdit->setPlaceholderText(tr("Название плагина"));
    m_versionEdit = new QLineEdit(QStringLiteral("1.0.0"), metadataBox);
    m_authorEdit = new QLineEdit(metadataBox);
    m_authorEdit->setPlaceholderText(tr("Ваше имя или команда"));
    m_homepageEdit = new QLineEdit(metadataBox);
    m_homepageEdit->setPlaceholderText(tr("https://… (необязательно)"));
    m_minimumLauncherEdit = new QLineEdit(QStringLiteral("0.7.5"), metadataBox);
    m_pagesEdit = new QLineEdit(metadataBox);
    m_pagesEdit->setPlaceholderText(tr("welcome, settings"));
    m_pagesEdit->setToolTip(tr("Уникальные ID вкладок через запятую. Например: welcome, settings."));
    m_actionsEdit = new QLineEdit(metadataBox);
    m_actionsEdit->setPlaceholderText(tr("server.start, server.stop"));
    m_actionsEdit->setToolTip(tr("Уникальные ID действий Runtime через запятую."));
    m_packageFilesEdit = new QPlainTextEdit(metadataBox);
    m_packageFilesEdit->setObjectName(QStringLiteral("packageFilesEditor"));
    m_packageFilesEdit->setPlaceholderText(tr("src/main.atlas -> program/main.atbc\nassets/info.json -> resources/info.json"));
    m_packageFilesEdit->setToolTip(tr("Одна запись на строку: путь в проекте -> путь в .atp. Исходники .atlas компилируются в ATBC; ресурсы копируются только в resources/."));
    m_packageFilesEdit->setFixedHeight(102);
    m_categoryCombo = new QComboBox(metadataBox);
    m_categoryCombo->addItems({tr("Общее"), tr("Серверы"), tr("Интерфейс"), tr("Утилиты"), tr("Контент")});
    m_descriptionEdit = new QPlainTextEdit(metadataBox);
    m_descriptionEdit->setPlaceholderText(tr("Кратко опишите назначение расширения"));
    m_descriptionEdit->setMaximumBlockCount(20);
    m_descriptionEdit->setFixedHeight(84);
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

    auto *permissionsBox = new QGroupBox(tr("Разрешения, видимые пользователю при установке"), metadataWidget);
    auto *permissionsLayout = new QVBoxLayout(permissionsBox);
    m_storagePermission = new QCheckBox(tr("Данные плагина — только изолированный каталог"), permissionsBox);
    m_storagePermission->setProperty("permission", QStringLiteral("files.plugin-data"));
    m_networkPermission = new QCheckBox(tr("Метаданные сети — через ограниченный API Runtime"), permissionsBox);
    m_networkPermission->setProperty("permission", QStringLiteral("network.metadata"));
    m_serversControlPermission = new QCheckBox(tr("Управление сервером Minecraft"), permissionsBox);
    m_serversControlPermission->setProperty("permission", QStringLiteral("servers.control"));
    m_serversConsolePermission = new QCheckBox(tr("Консоль сервера Minecraft"), permissionsBox);
    m_serversConsolePermission->setProperty("permission", QStringLiteral("servers.console"));
    m_uiFeedbackPermission = new QCheckBox(tr("Уведомления и сообщения внутри Launcher"), permissionsBox);
    m_uiFeedbackPermission->setProperty("permission", QStringLiteral("ui.feedback"));
    m_instancesReadPermission = new QCheckBox(tr("Метаданные игровых профилей — без локальных путей"), permissionsBox);
    m_instancesReadPermission->setProperty("permission", QStringLiteral("instances.read"));
    m_contentReadPermission = new QCheckBox(tr("Сводка модов, миров, ресурсов и шейдеров — без списка файлов"), permissionsBox);
    m_contentReadPermission->setProperty("permission", QStringLiteral("content.read"));
    m_contentRefreshPermission = new QCheckBox(tr("Запрос обновления сводки контента"), permissionsBox);
    m_contentRefreshPermission->setProperty("permission", QStringLiteral("content.refresh"));
    m_launcherNavigatePermission = new QCheckBox(tr("Переход к существующим разделам Launcher"), permissionsBox);
    m_launcherNavigatePermission->setProperty("permission", QStringLiteral("launcher.navigation"));
    auto *permissionHint = new QLabel(tr("Разрешения не дают Orvexa Code доступа к DLL, памяти процесса или произвольным файлам. Пользователь видит и подтверждает их перед установкой пакета."), permissionsBox);
    permissionHint->setObjectName(QStringLiteral("permissionHint"));
    permissionHint->setWordWrap(true);
    permissionsLayout->addWidget(m_storagePermission);
    permissionsLayout->addWidget(m_networkPermission);
    permissionsLayout->addWidget(m_serversControlPermission);
    permissionsLayout->addWidget(m_serversConsolePermission);
    permissionsLayout->addWidget(m_uiFeedbackPermission);
    permissionsLayout->addWidget(m_instancesReadPermission);
    permissionsLayout->addWidget(m_contentReadPermission);
    permissionsLayout->addWidget(m_contentRefreshPermission);
    permissionsLayout->addWidget(m_launcherNavigatePermission);
    permissionsLayout->addWidget(permissionHint);
    metadataLayout->addWidget(permissionsBox);
    metadataLayout->addStretch(1);

    auto *metadataScroll = new QScrollArea(m_editorTabs);
    metadataScroll->setObjectName(QStringLiteral("metadataScroll"));
    metadataScroll->setWidgetResizable(true);
    metadataScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    metadataScroll->setFrameShape(QFrame::NoFrame);
    metadataScroll->setWidget(metadataWidget);
    m_editorTabs->addTab(metadataScroll, tr("Пакет и разрешения"));
    workbench->addWidget(m_editorTabs);

    m_bottomTabs = new QTabWidget(workbench);
    m_bottomTabs->setObjectName(QStringLiteral("bottomTabs"));
    m_bottomTabs->setDocumentMode(true);
    m_diagnosticsEdit = new QTextEdit(m_bottomTabs);
    m_diagnosticsEdit->setObjectName(QStringLiteral("problemsOutput"));
    m_diagnosticsEdit->setReadOnly(true);
    m_diagnosticsEdit->setHtml(tr("<span style='color:#a0a0a0'>Создайте или откройте проект Orvexa Code.</span>"));
    m_bottomTabs->addTab(m_diagnosticsEdit, QStringLiteral(" "));
    m_problemsBadge = new QLabel(m_bottomTabs);
    m_problemsBadge->setObjectName(QStringLiteral("problemsBadge"));
    m_bottomTabs->tabBar()->setTabButton(0, QTabBar::LeftSide, m_problemsBadge);
    auto *reference = new QTextEdit(m_bottomTabs);
    reference->setObjectName(QStringLiteral("atlasCodeReference"));
    reference->setReadOnly(true);
    reference->setHtml(tr("<h3>Orvexa Code: быстрый старт</h3>"
        "<p><b>1.</b> Создайте проект. Основной код находится в <code>src/main.atlas</code>.</p>"
        "<p><b>2.</b> Опишите действие через блок события:</p>"
        "<pre>on launcher.started\n  call ui.page.create id=welcome title=\"Привет\"\n  call ui.control.add id=welcome type=label text=\"Мой плагин\"\nend</pre>"
        "<p><b>3.</b> Во вкладке «Пакет и разрешения» заполните ID, описание, список файлов и только нужные разрешения.</p>"
        "<p><b>4.</b> Нажмите «Скомпилировать», затем «Собрать .atp». В архив попадут <code>manifest.json</code>, ATBC и явно объявленные ресурсы; исходные <code>.atlas</code> не включаются.</p>"
        "<p><b>Безопасность.</b> Orvexa Code не выполняет произвольный C++, не загружает DLL и не получает доступ к файловой системе без объявленного разрешения.</p>"));
    m_bottomTabs->addTab(reference, QStringLiteral(" "));
    auto *referenceBadge = new QLabel(m_bottomTabs);
    referenceBadge->setObjectName(QStringLiteral("referenceBadge"));
    referenceBadge->setPixmap(StudioTheme::icon(QStringLiteral("book"), 14).pixmap(QSize(14, 14)));
    referenceBadge->setToolTip(tr("Справочник Orvexa Code"));
    m_bottomTabs->tabBar()->setTabButton(1, QTabBar::LeftSide, referenceBadge);
    workbench->addWidget(m_bottomTabs);
    workbench->setStretchFactor(0, 1);
    workbench->setStretchFactor(1, 0);
    workbench->setSizes({520, 170});
    mainSplitter->addWidget(workbench);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({245, 935});
    rootLayout->addWidget(mainSplitter, 1);
    setCentralWidget(root);

    connect(m_compileButton, &QPushButton::clicked, this, &AtlasStudioWindow::compileAtbc);
    connect(m_packageButton, &QPushButton::clicked, this, &AtlasStudioWindow::packageProject);
    connect(m_installButton, &QPushButton::clicked, this, &AtlasStudioWindow::installPackage);
    connect(newProjectButton, &QPushButton::clicked, this, &AtlasStudioWindow::createProject);
    connect(openProjectButton, &QPushButton::clicked, this, &AtlasStudioWindow::openProject);
    connect(m_projectTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const QString target = item->data(0, Qt::UserRole).toString();
        if (target == QLatin1String("source")) {
            m_editorTabs->setCurrentIndex(0);
            m_sourceEdit->setFocus();
        } else if (target == QLatin1String("manifest")) {
            m_editorTabs->setCurrentIndex(1);
        }
    });

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
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission,
                            m_uiFeedbackPermission, m_instancesReadPermission, m_contentReadPermission,
                            m_contentRefreshPermission, m_launcherNavigatePermission}) {
        connect(box, &QCheckBox::toggled, this, changed);
    }
    rebuildProjectTree();
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

    QMenu *buildMenu = menuBar()->addMenu(tr("&Сборка"));
    buildMenu->addAction(tr("Скомпилировать ATBC"), this, &AtlasStudioWindow::compileAtbc, QKeySequence(Qt::CTRL | Qt::Key_B));
    buildMenu->addAction(tr("Собрать .atp"), this, &AtlasStudioWindow::packageProject, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    buildMenu->addAction(tr("Показать готовый пакет"), this, &AtlasStudioWindow::installPackage);
    buildMenu->addSeparator();
    buildMenu->addAction(tr("Отправить в каталог…"), this, &AtlasStudioWindow::submitToCatalog);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Справка"));
    helpMenu->addAction(tr("Справочник Orvexa Code"), this, &AtlasStudioWindow::showAtlasCodeReference, QKeySequence::HelpContents);
    statusBar()->showMessage(tr("Готово. Создайте проект или откройте существующий Orvexa Code проект."));
}

void AtlasStudioWindow::submitToCatalog()
{
    if (m_projectDirectory.isEmpty()) {
        QMessageBox::information(this, tr("Отправка в каталог"), tr("Сначала откройте проект плагина в Orvexa Studio."));
        return;
    }
    const QString package = packagePath();
    if (!QFileInfo::exists(package)) {
        QMessageBox::information(this, tr("Отправка в каталог"), tr("Сначала соберите пакет .atp (Сборка → Собрать .atp). Заявка отправляется только с готовым пакетом."));
        return;
    }
    const QString srcDir = QDir(m_projectDirectory).filePath(QStringLiteral("src"));
    const QStringList sourceFiles = QDir(srcDir).entryList({QStringLiteral("*.atlas")}, QDir::Files);
    if (sourceFiles.isEmpty() || !QFileInfo::exists(QDir(m_projectDirectory).filePath(QStringLiteral("atlas-project.json")))) {
        QMessageBox::warning(this, tr("Отправка в каталог"), tr("Без исходного кода заявка не принимается. Убедитесь, что в каталоге проекта есть файлы .atlas и atlas-project.json."));
        return;
    }
    const QString pluginName = m_nameEdit->text().trimmed();
    const QString pluginVersion = m_versionEdit->text().trimmed();
    const QString authorName = m_authorEdit->text().trimmed();
    QString pluginId = m_idEdit->text().trimmed();
    pluginId.replace(QStringLiteral("."), QStringLiteral("/"));
    const QString description = m_descriptionEdit->toPlainText().trimmed();
    if (pluginName.isEmpty() || pluginVersion.isEmpty() || pluginId.isEmpty()) {
        QMessageBox::information(this, tr("Отправка в каталог"), tr("Заполните название, идентификатор и версию плагина в метаданных проекта."));
        return;
    }

    QDialog dialog(this, Qt::WindowCloseButtonHint);
    dialog.setWindowTitle(tr("Отправить плагин в каталог"));
    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *tokenEdit = new QLineEdit(&dialog);
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setPlaceholderText(QStringLiteral("ghp_... (только для этой отправки, не сохраняется)"));
    form->addRow(tr("GitHub токен:"), tokenEdit);
    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(box);
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString token = tokenEdit->text().trimmed();
    if (token.isEmpty()) {
        QMessageBox::information(this, tr("Отправка в каталог"), tr("Токен не введён. Отправка отменена, токен не сохранён."));
        return;
    }

    QFile atpFile(package);
    if (!atpFile.open(QIODevice::ReadOnly)) {
        setDiagnostics({tr("Не удалось открыть собранный пакет для проверки SHA-256: %1").arg(package)}, false);
        return;
    }
    const QByteArray atpData = atpFile.readAll();
    atpFile.close();
    const QString sha256 = QString::fromLatin1(QCryptographicHash::hash(atpData, QCryptographicHash::Sha256).toHex().constData());

    const QString issueTitle = QStringLiteral("Заявка: %1 v%2").arg(pluginName, pluginVersion);
    const QString issueBody = QStringLiteral(
        "**Плагин:** %1\n\n"
        "**Идентификатор:** `%3`\n\n"
        "**Версия:** %2\n\n"
        "**Автор:** %4\n\n"
        "**Описание:** %5\n\n"
        "**SHA-256 пакета .atp:**\n\n`%6`\n\n"
        "Пакет `plugin.atp` вместе с исходным кодом (`src/*.atlas` и `atlas-project.json`)"
        " автор обязан положить в `submissions/%4/%7/` этого репозитория перед рассмотрением заявки.\n\n"
        "*Заявка отправлена из Orvexa Studio.*"
    ).arg(pluginName, pluginVersion, pluginId, authorName.isEmpty() ? QStringLiteral("не указан") : authorName,
          description.isEmpty() ? QStringLiteral("без описания") : description, sha256,
          pluginId.replace(QStringLiteral("."), QStringLiteral("/")));

    QUrl apiIssues(QStringLiteral("https://api.github.com/repos/kutsandriy14-cyber/atlas-plugin-submissions/issues"));
    QNetworkRequest request(apiIssues);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(token).toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QJsonObject payload;
    payload.insert(QStringLiteral("title"), issueTitle);
    payload.insert(QStringLiteral("body"), issueBody);
    QJsonObject labels;
    labels.insert(QStringLiteral("name"), QStringLiteral("plugin-submission"));
    payload.insert(QStringLiteral("labels"), QJsonArray{labels});

    QNetworkAccessManager manager;
    manager.setTransferTimeout(30000);
    QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        const QString errorMessage = reply->errorString();
        reply->deleteLater();
        setDiagnostics({tr("Не удалось отправить заявку: %1").arg(errorMessage)}, false);
        animateStatusMessage(tr("Отправка не удалась"), StudioTheme::error());
        return;
    }
    const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    if (!result.contains(QStringLiteral("html_url"))) {
        setDiagnostics({tr("Не удалось отправить заявку: сервер не вернул ссылку на issue.")}, false);
        animateStatusMessage(tr("Отправка не удалась"), StudioTheme::error());
        return;
    }
    const QString issueUrl = result.value(QStringLiteral("html_url")).toString();
    setDiagnostics({tr("Заявка отправлена: %1. Положите plugin.atp и каталог src/ с исходниками в submissions/ репозитория заявок.").arg(issueUrl)}, true);
    animateStatusMessage(tr("Заявка отправлена в каталог"), StudioTheme::success());
    QDesktopServices::openUrl(QUrl(issueUrl));
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
    m_minimumLauncherEdit->setText(QStringLiteral("0.7.5"));
    m_pagesEdit->setText(QStringLiteral("welcome"));
    m_actionsEdit->clear();
    m_packageFilesEdit->setPlainText(QStringLiteral("src/main.atlas -> program/main.atbc"));
    m_descriptionEdit->setPlainText(tr("Первый плагин Orvexa Code."));
    m_categoryCombo->setCurrentIndex(0);
    applyPermissions({QStringLiteral("ui.feedback"), QStringLiteral("content.read")});
    m_sourceEdit->setPlainText(defaultSource());
    updateProblemsBadge(0);
    m_modified = true;
    if (!saveProject()) {
        return;
    }
    setDiagnostics({tr("Создан новый проект. Отредактируйте код и нажмите «Собрать .atp»." )}, true);
    animateStatusMessage(tr("Новый проект создан"), StudioTheme::success());
}

void AtlasStudioWindow::openProject()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Открыть каталог Orvexa Code"));
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
    rebuildProjectTree();
    updateWindowTitle();
    animateStatusMessage(tr("Проект сохранён"), StudioTheme::success());
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
                diagnostics->append(tr("Строка файлов %1: Orvexa Code должен собираться в program/*.atbc.").arg(index + 1));
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
        diagnostics->append(tr("В пакете должна быть хотя бы одна программа Orvexa Code (*.atlas -> program/*.atbc)."));
    }
    if (programCount > 32) {
        diagnostics->append(tr("В одном .atp допускается не более 32 программ Orvexa Code."));
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
        diagnostics->append(tr("Исходный код Orvexa Code пуст."));
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
            tr("Папка с готовым пакетом открыта:\n%1\n\nВ Orvexa Launcher откройте «Плагины → Установить .atp» и выберите файл %2.")
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

void AtlasStudioWindow::rebuildProjectTree()
{
    if (!m_projectTree) {
        return;
    }
    m_projectTree->clear();
    if (m_projectDirectory.isEmpty()) {
        auto *placeholder = new QTreeWidgetItem(m_projectTree, {tr("Нет открытого проекта")});
        placeholder->setDisabled(true);
        return;
    }

    const QFileInfo projectInfo(m_projectDirectory);
    auto *root = new QTreeWidgetItem(m_projectTree, {projectInfo.fileName().isEmpty() ? m_projectDirectory : projectInfo.fileName()});
    root->setIcon(0, treeIcon(QStringLiteral("module")));
    root->setExpanded(true);
    auto *manifest = new QTreeWidgetItem(root, {QStringLiteral("atlas-project.json")});
    manifest->setIcon(0, fileIcon(QStringLiteral("atlas-project.json")));
    manifest->setData(0, Qt::UserRole, QStringLiteral("manifest"));
    auto *sourceFolder = new QTreeWidgetItem(root, {QStringLiteral("src")});
    sourceFolder->setIcon(0, treeIcon(QStringLiteral("file-folder")));
    sourceFolder->setExpanded(true);
    auto *source = new QTreeWidgetItem(sourceFolder, {QStringLiteral("main.atlas")});
    source->setIcon(0, fileIcon(QStringLiteral("main.atlas")));
    source->setData(0, Qt::UserRole, QStringLiteral("source"));
    auto *buildFolder = new QTreeWidgetItem(root, {QStringLiteral("build")});
    buildFolder->setIcon(0, treeIcon(QStringLiteral("file-folder")));
    auto *distFolder = new QTreeWidgetItem(root, {QStringLiteral("dist")});
    distFolder->setIcon(0, treeIcon(QStringLiteral("file-folder")));
    if (QFileInfo::exists(atbcPath())) {
        auto *atbc = new QTreeWidgetItem(buildFolder, {QStringLiteral("main.atbc")});
        atbc->setIcon(0, fileIcon(QStringLiteral("main.atbc")));
        buildFolder->setExpanded(true);
    }
    const QDir dist(QDir(m_projectDirectory).filePath(QStringLiteral("dist")));
    for (const QFileInfo &file : dist.entryInfoList(QStringList{QStringLiteral("*.atp")}, QDir::Files, QDir::Name)) {
        auto *package = new QTreeWidgetItem(distFolder, {file.fileName()});
        package->setIcon(0, fileIcon(file.fileName()));
    }
    if (distFolder->childCount() > 0) {
        distFolder->setExpanded(true);
    }
    m_projectTree->setCurrentItem(source);
}

void AtlasStudioWindow::showAtlasCodeReference()
{
    if (m_bottomTabs) {
        m_bottomTabs->setCurrentIndex(1);
    }
    animateStatusMessage(tr("Открыт встроенный справочник Orvexa Code."), StudioTheme::accent());;
}

void AtlasStudioWindow::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = QDir::cleanPath(directory);
    m_modified = false;
    rebuildProjectTree();
    updateWindowTitle();
    updateProblemsBadge(0);
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
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission,
                            m_uiFeedbackPermission, m_instancesReadPermission, m_contentReadPermission,
                            m_contentRefreshPermission, m_launcherNavigatePermission}) {
        if (box->isChecked()) {
            result.append(box->property("permission").toString());
        }
    }
    return result;
}

void AtlasStudioWindow::applyPermissions(const QStringList &permissions)
{
    for (QCheckBox *box : {m_storagePermission, m_networkPermission, m_serversControlPermission, m_serversConsolePermission,
                            m_uiFeedbackPermission, m_instancesReadPermission, m_contentReadPermission,
                            m_contentRefreshPermission, m_launcherNavigatePermission}) {
        box->setChecked(permissions.contains(box->property("permission").toString()));
    }
}

void AtlasStudioWindow::setDiagnostics(const QStringList &diagnostics, const bool success)
{
    m_diagnosticsEdit->setHtml(QStringLiteral("<div style='color:%1'>%2</div>")
        .arg(success ? QStringLiteral("#89d185") : QStringLiteral("#f14c4c"), diagnosticsText(diagnostics)));
    updateProblemsBadge(success ? 0 : diagnostics.size());
    if (m_bottomTabs) {
        m_bottomTabs->setCurrentIndex(0);
    }
}

QIcon AtlasStudioWindow::fileIcon(const QString &fileName) const
{
    const QString lower = fileName.toLower();
    if (lower.endsWith(QLatin1String(".atlas"))) {
        return treeIcon(QStringLiteral("file-atlas"));
    }
    if (lower.endsWith(QLatin1String(".atbc"))) {
        return treeIcon(QStringLiteral("file-atbc"));
    }
    if (lower.endsWith(QLatin1String(".atp"))) {
        return treeIcon(QStringLiteral("file-atp"));
    }
    if (lower.endsWith(QLatin1String(".json"))) {
        return treeIcon(QStringLiteral("file-json"));
    }
    return treeIcon(QStringLiteral("file-atlas"));
}

QIcon AtlasStudioWindow::treeIcon(const QString &name) const
{
    return StudioTheme::icon(name, 16);
}

void AtlasStudioWindow::updateProblemsBadge(int count)
{
    if (!m_problemsBadge) {
        return;
    }
    if (count <= 0) {
        m_problemsBadge->setPixmap(StudioTheme::icon(QStringLiteral("ok-dot"), 10).pixmap(QSize(10, 10)));
        m_problemsBadge->setToolTip(tr("Ошибок нет"));
    } else {
        m_problemsBadge->setPixmap(StudioTheme::icon(QStringLiteral("error-dot"), 10).pixmap(QSize(10, 10)));
        m_problemsBadge->setToolTip(tr("Ошибок: %1").arg(count));
    }
}

void AtlasStudioWindow::animateStatusMessage(const QString &message, const QColor &color)
{
    QStatusBar *bar = statusBar();
    if (!bar) {
        return;
    }
    bar->clearMessage();
    bar->showMessage(message);
    auto *animation = new QPropertyAnimation(bar, "styleSheet", bar);
    animation->setDuration(1400);
    animation->setStartValue(QStringLiteral("QStatusBar { background: %1; } ").arg(color.darker(120).name()));
    animation->setEndValue(QString());
    animation->setEasingCurve(QEasingCurve::OutQuad);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void AtlasStudioWindow::updateWindowTitle()
{
    const QString name = m_projectDirectory.isEmpty() ? tr("без проекта") : QFileInfo(m_projectDirectory).fileName();
    setWindowTitle(QStringLiteral("%1Orvexa Studio — %2").arg(m_modified ? QStringLiteral("* ") : QString(), name));
}

} // namespace atlas::studio
