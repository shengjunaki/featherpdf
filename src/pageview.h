// PageView —— 自绘单页 PDF 视图：缩放 / 四种 fit / 主题配色 / 文本选择 / 链接 / 渲染缓存
#pragma once

#include <QAbstractScrollArea>
#include <QCache>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPdfLink>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

class QPdfDocument;
class QPdfLinkModel;

class PageView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum class Fit { Width, Height, Page, Content, Custom };
    Q_ENUM(Fit)

    enum class Theme { Normal, Invert, Catppuccin, TokyoNight };
    Q_ENUM(Theme)

    explicit PageView(QWidget *parent = nullptr);

    void setDocument(QPdfDocument *doc);
    QPdfDocument *document() const { return m_doc; }

    void setFit(Fit f);
    Fit fit() const { return m_fit; }

    void setTheme(Theme t);
    Theme theme() const { return m_theme; }
    void cycleTheme();

    void setZoom(qreal z);          // 切换到 Fit::Custom
    qreal zoom() const { return m_zoom; }

    int currentPage() const { return m_page; }
    int pageCount() const;

    // 文本选择
    bool hasSelection() const { return !m_selText.isEmpty(); }
    QString selectedText() const { return m_selText; }
    QPdfLink linkAtPoint(const QPointF &pagePos) const;   // pagePos：页面 pt 坐标

    static QString fitName(Fit f);
    static Fit fitFromName(const QString &name, Fit fallback = Fit::Width);

    static QString themeName(Theme t);
    static Theme themeFromName(const QString &name, Theme fallback = Theme::Normal);
    static int themeCount();
    static QColor themePaper(Theme t);   // 纸面（duotone 亮端）
    static QColor themeInk(Theme t);     // 墨色（duotone 暗端）
    static QColor themeBg(Theme t);      // 工作区背景

public slots:
    void jumpToPage(int page);
    void nextPage();
    void prevPage();
    void firstPage();
    void lastPage();
    void clearSelection();
    void selectAllPage();
    void copySelection(bool primary = false);

signals:
    void currentPageChanged(int page);
    void fitChanged(Fit fit);
    void zoomChanged(qreal zoom);
    void themeChanged(Theme theme);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void relayout();
    QRectF contentBox(int page);
    QImage renderFor(int page, QSize px);
    QImage applyTheme(QImage img) const;
    void scrollContentsBy(int dx, int dy) override;

    QPointF pageOrigin() const;                      // 页面图片左上角（视口坐标）
    QPointF viewToPage(const QPointF &viewPos) const;
    void updateSelection(const QPointF &from, const QPointF &to);

    QPdfDocument *m_doc = nullptr;
    QPdfLinkModel *m_linkModel = nullptr;
    Fit    m_fit = Fit::Width;
    Theme  m_theme = Theme::Normal;
    int    m_page = 0;
    qreal  m_zoom = 1.0;

    // 选择状态（QPdfSelection 默认构造是私有的，不能直接当成员）
    QList<QPolygonF> m_selBounds;
    QString          m_selText;
    QPointF          m_selAnchor;
    bool             m_selecting = false;
    bool             m_dragged = false;

    QCache<QString, QImage> m_cache;   // 成本单位 KB
    QHash<int, QRectF>      m_boxes;   // contentBox 缓存（pt 单位）
};
