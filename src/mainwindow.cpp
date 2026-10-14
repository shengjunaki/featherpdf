#include "mainwindow.h"
#include "pageview.h"

#include <QCloseEvent>
#include <QCryptographicHash>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPalette>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QSettings>
#include <QShortcut>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    setupShortcuts();

    QSettings s;
    m_recent = s.value(QStringLiteral("recent")).toStringList();
}

MainWindow::~MainWindow() = default;

QString MainWindow::docSettingsKey(const QString &path) const
{
    // 注意：qHash(QString) 每次进程随机加盐，不能拿来做持久化键
    const QByteArray digest =
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("docs/") + QString::fromLatin1(digest);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("FeatherPDF"));
    setWindowFlag(Qt::FramelessWindowHint);   // 无标题栏，交给 niri 边框
    resize(1100, 820);
    setAcceptDrops(true);

    m_doc = new QPdfDocument(this);
    m_view = new PageView(this);
    m_view->setDocument(m_doc);
    setCentralWidget(m_view);

    // ---- TOC 侧栏：唯一保留的界面元素，标题栏也去掉 ----
    m_tocModel = new QPdfBookmarkModel(this);
    m_tocModel->setDocument(m_doc);
    m_tocView = new QTreeView(this);
    m_tocView->setModel(m_tocModel);
    m_tocView->setHeaderHidden(true);
    m_tocView->setUniformRowHeights(true);
    m_tocView->setAnimated(true);
    connect(m_tocView, &QTreeView::activated, this, [this](const QModelIndex &idx) {
        m_view->jumpToPage(idx.data(int(QPdfBookmarkModel::Role::Page)).toInt());
    });

    m_tocDock = new QDockWidget(QStringLiteral("toc"), this);
    m_tocDock->setObjectName(QStringLiteral("tocDock"));
    m_tocDock->setWidget(m_tocView);
    m_tocDock->setTitleBarWidget(new QWidget(m_tocDock));   // 空标题栏
    m_tocDock->setFeatures(QDockWidget::DockWidgetClosable);
    m_tocDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_tocDock);
    m_tocDock->hide();

    connect(m_view, &PageView::themeChanged, this, &MainWindow::applyThemeToToc);
    applyThemeToToc();
}

void MainWindow::applyThemeToToc()
{
    const PageView::Theme th = m_view->theme();
    QPalette pal = m_tocView->palette();
    pal.setColor(QPalette::Base, PageView::themePaper(th));
    pal.setColor(QPalette::AlternateBase, PageView::themePaper(th).darker(108));
    pal.setColor(QPalette::Text, PageView::themeInk(th));
    pal.setColor(QPalette::Window, PageView::themeBg(th));
    pal.setColor(QPalette::WindowText, PageView::themeInk(th));
    pal.setColor(QPalette::Highlight, PageView::themeInk(th));
    pal.setColor(QPalette::HighlightedText, PageView::themePaper(th));
    m_tocView->setPalette(pal);
    m_tocDock->setPalette(pal);
}

void MainWindow::setupShortcuts()
{
    auto bind = [this](const QKeySequence &seq, auto fn) {
        QShortcut *sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };
    using Fit = PageView::Fit;

    // 文件
    bind(QKeySequence::Open,          &MainWindow::openFileDialog);
    bind(QKeySequence::Quit,          &QWidget::close);
    bind(QKeySequence(QStringLiteral("Ctrl+R")), &MainWindow::showRecentMenu);

    // 文本选择
    bind(QKeySequence::Copy,  [this] { m_view->copySelection(false); });
    bind(QKeySequence::SelectAll, [this] { m_view->selectAllPage(); });
    bind(QKeySequence(Qt::Key_Escape), [this] { m_view->clearSelection(); });

    // 翻页
    bind(QKeySequence(Qt::Key_J),        [this] { m_view->nextPage(); });
    bind(QKeySequence(Qt::Key_Space),    [this] { m_view->nextPage(); });
    bind(QKeySequence(Qt::Key_PageDown), [this] { m_view->nextPage(); });
    bind(QKeySequence(Qt::Key_Right),    [this] { m_view->nextPage(); });
    bind(QKeySequence(Qt::Key_K),        [this] { m_view->prevPage(); });
    bind(QKeySequence(Qt::Key_PageUp),   [this] { m_view->prevPage(); });
    bind(QKeySequence(Qt::Key_Left),     [this] { m_view->prevPage(); });
    bind(QKeySequence(Qt::Key_Home),     [this] { m_view->firstPage(); });
    bind(QKeySequence(Qt::Key_End),      [this] { m_view->lastPage(); });
    bind(QKeySequence(Qt::SHIFT | Qt::Key_G), [this] { m_view->lastPage(); });

    // gg → 首页（双击 g 序列）
    m_gTimer = new QTimer(this);
    m_gTimer->setSingleShot(true);
    m_gTimer->setInterval(800);
    m_gKey = new QShortcut(QKeySequence(Qt::Key_G), this);
    m_gKey->setContext(Qt::ApplicationShortcut);
    connect(m_gKey, &QShortcut::activated, this, [this] {
        if (m_gTimer->isActive()) {
            m_gTimer->stop();
            m_view->firstPage();
        } else {
            m_gTimer->start();
        }
    });

    // 适应模式
    bind(QKeySequence(Qt::Key_W), [this] { m_view->setFit(Fit::Width); });
    bind(QKeySequence(Qt::Key_H), [this] { m_view->setFit(Fit::Height); });
    bind(QKeySequence(Qt::Key_F), [this] { m_view->setFit(Fit::Page); });
    bind(QKeySequence(Qt::Key_C), [this] { m_view->setFit(Fit::Content); });

    // 缩放
    bind(QKeySequence(Qt::Key_Plus),  [this] { m_view->setZoom(m_view->zoom() * 1.2); });
    bind(QKeySequence(Qt::Key_Equal), [this] { m_view->setZoom(m_view->zoom() * 1.2); });
    bind(QKeySequence(Qt::Key_Minus), [this] { m_view->setZoom(m_view->zoom() / 1.2); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_0), [this] { m_view->setFit(Fit::Page); });

    // 配色：i 循环，Ctrl+1..4 直选
    bind(QKeySequence(Qt::Key_I), [this] { m_view->cycleTheme(); });
    for (int i = 0; i < PageView::themeCount(); ++i) {
        QShortcut *sc = new QShortcut(
            QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this);
        sc->setContext(Qt::ApplicationShortcut);
        sc->setProperty("themeIndex", i);
        connect(sc, &QShortcut::activated, this, &MainWindow::setThemeByIndex);
    }

    // TOC / 帮助
    bind(QKeySequence(Qt::Key_T), &MainWindow::toggleToc);
    bind(QKeySequence(Qt::Key_F1), &MainWindow::showHelp);
    bind(QKeySequence(Qt::SHIFT | Qt::Key_Slash), &MainWindow::showHelp);   // ?
}

void MainWindow::setThemeByIndex()
{
    QShortcut *sc = qobject_cast<QShortcut *>(sender());
    if (!sc)
        return;
    m_view->setTheme(PageView::Theme(sc->property("themeIndex").toInt()));
}

void MainWindow::toggleToc()
{
    const bool hasToc = m_tocModel->rowCount(QModelIndex()) > 0;
    if (!hasToc)
        return;
    m_tocDock->setVisible(!m_tocDock->isVisible());
}

void MainWindow::showHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("FeatherPDF 快捷键"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *label = new QLabel(QStringLiteral(
        "<table cellspacing=6>"
        "<tr><th align=left>文件</th></tr>"
        "<tr><td>Ctrl+O / Ctrl+Q</td><td>打开 / 退出</td></tr>"
        "<tr><td>Ctrl+R</td><td>最近文档菜单（弹出后手点选择，1-9 直选）</td></tr>"
        "<tr><th align=left>文本选择</th></tr>"
        "<tr><td>鼠标拖选</td><td>选中即复制（primary selection，中键粘贴）</td></tr>"
        "<tr><td>Ctrl+C / Ctrl+A / Esc</td><td>复制 / 全选本页 / 取消选择</td></tr>"
        "<tr><td>点击链接</td><td>外链自动用系统默认程序打开，内链跳页</td></tr>"
        "<tr><th align=left>翻页</th></tr>"
        "<tr><td>j / Space / PgDn / →</td><td>下一页（滚轮到底也翻页）</td></tr>"
        "<tr><td>k / PgUp / ←</td><td>上一页</td></tr>"
        "<tr><td>gg / G / Home / End</td><td>首页 / 末页</td></tr>"
        "<tr><th align=left>视图</th></tr>"
        "<tr><td>w / h / f / c</td><td>适应宽度 / 高度 / 整页 / 内容</td></tr>"
        "<tr><td>+ / - / Ctrl+滚轮 / Ctrl+0</td><td>放大 / 缩小 / 缩放 / 回到整页</td></tr>"
        "<tr><td>i</td><td>循环配色（正常→反色→Catppuccin→Tokyo Night）</td></tr>"
        "<tr><td>Ctrl+1 … Ctrl+4</td><td>直接选配色</td></tr>"
        "<tr><td>t</td><td>目录侧栏</td></tr>"
        "<tr><td>Enter</td><td>目录跳页</td></tr>"
        "</table>"), &dlg);
    label->setTextFormat(Qt::RichText);
    lay->addWidget(label);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(bb);
    dlg.exec();
}

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开 PDF"), QString(),
        QStringLiteral("PDF 文件 (*.pdf);;所有文件 (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::showRecentMenu()
{
    QMenu menu(this);
    int i = 0;
    for (const QString &path : std::as_const(m_recent)) {
        const QString label = QStringLiteral("%1  %2  —  %3")
                                  .arg(i + 1)
                                  .arg(QFileInfo(path).fileName(), path);
        QAction *a = menu.addAction(label);
        a->setData(path);
        if (i < 9)
            a->setShortcut(QKeySequence(QString::number(i + 1)));
        ++i;
    }
    if (m_recent.isEmpty()) {
        QAction *a = menu.addAction(QStringLiteral("（最近文档为空）"));
        a->setEnabled(false);
    }
    menu.addSeparator();
    QAction *clearAct = menu.addAction(QStringLiteral("清空列表"));
    connect(clearAct, &QAction::triggered, this, [this] {
        m_recent.clear();
        QSettings s;
        s.setValue(QStringLiteral("recent"), m_recent);
    });

    // 弹在鼠标位置，手点选择
    QAction *chosen = menu.exec(QCursor::pos());
    if (chosen && chosen->data().isValid())
        openFile(chosen->data().toString());
}

bool MainWindow::openFile(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    saveDocState();

    m_doc->close();
    const QPdfDocument::Error err = m_doc->load(abs);
    if (err != QPdfDocument::Error::None) {
        // 无界面设计：失败只改窗口标题（无标题栏时看任务切换器），不弹模态框
        setWindowTitle(QStringLiteral("无法打开 %1").arg(QFileInfo(abs).fileName()));
        return false;
    }

    m_currentPath = abs;
    m_recent.removeAll(abs);
    m_recent.prepend(abs);
    while (m_recent.size() > 10)
        m_recent.removeLast();
    QSettings s;
    s.setValue(QStringLiteral("recent"), m_recent);
    s.setValue(QStringLiteral("lastFile"), abs);

    // 每文档记忆：页码 / 主题 / fit
    restoreDocState();

    const bool hasToc = m_tocModel->rowCount(QModelIndex()) > 0;
    m_tocDock->setEnabled(hasToc);
    if (!hasToc && m_tocDock->isVisible())
        m_tocDock->hide();

    setWindowTitle(QStringLiteral("%1 — FeatherPDF").arg(QFileInfo(abs).fileName()));
    m_view->setFocus();
    return true;
}

void MainWindow::saveDocState()
{
    if (m_currentPath.isEmpty())
        return;
    QSettings s;
    s.beginGroup(docSettingsKey(m_currentPath));
    s.setValue(QStringLiteral("page"), m_view->currentPage());
    s.setValue(QStringLiteral("theme"), PageView::themeName(m_view->theme()));
    s.setValue(QStringLiteral("fit"), PageView::fitName(m_view->fit()));
    s.setValue(QStringLiteral("zoom"), m_view->zoom());
    s.endGroup();
}

void MainWindow::restoreDocState()
{
    QSettings s;
    s.beginGroup(docSettingsKey(m_currentPath));
    m_view->setTheme(
        PageView::themeFromName(s.value(QStringLiteral("theme"), QStringLiteral("normal")).toString()));
    const PageView::Fit fit =
        PageView::fitFromName(s.value(QStringLiteral("fit"), QStringLiteral("width")).toString());
    m_view->setFit(fit);
    if (fit == PageView::Fit::Custom)
        m_view->setZoom(s.value(QStringLiteral("zoom"), 1.0).toReal());
    m_view->jumpToPage(s.value(QStringLiteral("page"), 0).toInt());
    s.endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveDocState();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
            openFile(u.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}
