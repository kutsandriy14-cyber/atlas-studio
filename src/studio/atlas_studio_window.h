#pragma once

#include <QMainWindow>
#include <QColor>
#include <QHash>
#include <QVector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTextEdit;
class QTabWidget;
class QTreeWidget;

namespace atlas::studio {
class AtlasCodeHighlighter;

class AtlasStudioWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit AtlasStudioWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void createProject();
    void openProject();
    bool saveProject();
    void compileAtbc();
    void packageProject();
    void installPackage();
    void markModified();

private:
    void setupUi();
    void setupActions();
    bool confirmDiscardChanges();
    void setProjectDirectory(const QString &directory);
    bool loadProject(const QString &directory, QString *error);
    bool writeProject(QString *error) const;
    bool validateProject(QStringList *diagnostics) const;
    struct ProjectPackageFile {
        QString sourcePath;
        QString archivePath;
        bool program = false;
    };

    bool compileCurrentProject(QStringList *diagnostics, QVector<ProjectPackageFile> *compiledFiles = nullptr);
    bool declaredPackageFiles(QVector<ProjectPackageFile> *files, QStringList *diagnostics) const;
    QString projectFilePath() const;
    QString sourceFilePath() const;
    QString buildDirectoryPath() const;
    QString atbcPath() const;
    QString packagePath() const;
    QStringList selectedPermissions() const;
    void applyPermissions(const QStringList &permissions);
    void setDiagnostics(const QStringList &diagnostics, bool success = false);
    void rebuildProjectTree();
    void showAtlasCodeReference();
    void updateWindowTitle();
    void submitToCatalog();

    void animateStatusMessage(const QString &message, const QColor &color);
    void updateProblemsBadge(int count);
    QIcon fileIcon(const QString &fileName) const;
    QIcon treeIcon(const QString &name) const;
    void decorateExplorerTitle();

    QString m_projectDirectory;
    bool m_modified = false;

    QLineEdit *m_idEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_versionEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QLineEdit *m_homepageEdit = nullptr;
    QLineEdit *m_minimumLauncherEdit = nullptr;
    QLineEdit *m_pagesEdit = nullptr;
    QLineEdit *m_actionsEdit = nullptr;
    QPlainTextEdit *m_packageFilesEdit = nullptr;
    QComboBox *m_categoryCombo = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_sourceEdit = nullptr;
    QTextEdit *m_diagnosticsEdit = nullptr;
    QTabWidget *m_editorTabs = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QTreeWidget *m_projectTree = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_problemsBadge = nullptr;

    AtlasCodeHighlighter *m_highlighter = nullptr;
    QCheckBox *m_storagePermission = nullptr;
    QCheckBox *m_networkPermission = nullptr;
    QCheckBox *m_serversControlPermission = nullptr;
    QCheckBox *m_serversConsolePermission = nullptr;
    QCheckBox *m_uiFeedbackPermission = nullptr;
    QCheckBox *m_instancesReadPermission = nullptr;
    QCheckBox *m_contentReadPermission = nullptr;
    QCheckBox *m_contentRefreshPermission = nullptr;
    QCheckBox *m_launcherNavigatePermission = nullptr;
    QPushButton *m_compileButton = nullptr;
    QPushButton *m_packageButton = nullptr;
    QPushButton *m_installButton = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
};

} // namespace atlas::studio
