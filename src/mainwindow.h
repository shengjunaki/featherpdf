// MainWindow —— 无菜单/工具栏/状态栏/标题栏，全快捷键 + 可隐藏的 TOC 侧栏
#pragma once

#include <QMainWindow>
#include <QStringList>

class QDockWidget;
class QPdfBookmarkModel;
class QPdfDocument;
class QShortcut;
class QTimer;
class PageView;
class QTreeView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString &path);
    PageView *view() const { return m_view; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFileDialog();
    void showRecentMenu();
    void setThemeByIndex();
    void toggleToc();
    void showHelp();
    void applyThemeToToc();

private:
    void buildUi();
    void setupShortcuts();
    void saveDocState();
    void restoreDocState();
    QString docSettingsKey(const QString &path) const;

    QPdfDocument      *m_doc = nullptr;
    PageView          *m_view = nullptr;
    QPdfBookmarkModel *m_tocModel = nullptr;
    QTreeView         *m_tocView = nullptr;
    QDockWidget       *m_tocDock = nullptr;

    QShortcut *m_gKey = nullptr;
    QTimer    *m_gTimer = nullptr;

    QStringList m_recent;
    QString     m_currentPath;
};
