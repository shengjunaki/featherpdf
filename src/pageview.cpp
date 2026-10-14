#include "pageview.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfLinkModel>
#include <QPdfSelection>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kMargin = 12;                 // 页面周围留白（逻辑像素）
constexpr int kCacheCostKB = 32 << 10;      // 渲染缓存上限：32 MB（单页视图足够）
constexpr int kProbeWidth = 200;            // fit-content 探测图宽度
constexpr int kMaxRenderDim = 8192;         // 单页渲染像素上限，防 OOM
constexpr int kDragThreshold = 3;           // 超过这个位移才算拖选

struct ThemeInfo {
    QRgb paper;   // 纸面（亮度 255 映射到）
    QRgb ink;     // 墨色（亮度 0 映射到）
    QRgb bg;      // 工作区背景
};

const ThemeInfo &themeInfo(PageView::Theme t)
{
    static const ThemeInfo table[] = {
        {qRgb(0xff, 0xff, 0xff), qRgb(0x00, 0x00, 0x00), qRgb(0xbd, 0xbd, 0xbd)}, // Normal
        {qRgb(0x00, 0x00, 0x00), qRgb(0xf2, 0xf2, 0xf2), qRgb(0x26, 0x26, 0x26)}, // Invert
        {qRgb(0x1e, 0x1e, 0x2e), qRgb(0xcd, 0xd6, 0xf4), qRgb(0x11, 0x11, 0x1b)}, // Catppuccin Mocha
        {qRgb(0x1a, 0x1b, 0x26), qRgb(0xc0, 0xca, 0xf5), qRgb(0x16, 0x16, 0x1e)}, // Tokyo Night
    };
    return table[int(t)];
}

// 每主题 256 项灰度→颜色 LUT（duotone 映射），懒初始化
const QVector<QRgb> &themeLut(PageView::Theme t)
{
    static QVector<QRgb> cache[4];
    QVector<QRgb> &lut = cache[int(t)];
    if (lut.isEmpty()) {
        const ThemeInfo &info = themeInfo(t);
        lut.resize(256);
        const int pr = qRed(info.paper), pg = qGreen(info.paper), pb = qBlue(info.paper);
        const int ir = qRed(info.ink), ig = qGreen(info.ink), ib = qBlue(info.ink);
        for (int i = 0; i < 256; ++i) {
            lut[i] = qRgb((ir * (255 - i) + pr * i) / 255,
                          (ig * (255 - i) + pg * i) / 255,
                          (ib * (255 - i) + pb * i) / 255);
        }
    }
    return lut;
}
} // namespace

PageView::PageView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_cache.setMaxCost(kCacheCostKB);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    horizontalScrollBar()->setSingleStep(20);
    verticalScrollBar()->setSingleStep(40);
    viewport()->setMouseTracking(true);
}

void PageView::setDocument(QPdfDocument *doc)
{
    m_doc = doc;
    if (!m_linkModel) {
        m_linkModel = new QPdfLinkModel(this);
        connect(m_linkModel, &QPdfLinkModel::documentChanged,
                this, [this] { viewport()->update(); });
    }
    m_linkModel->setDocument(doc);
    m_page = 0;
    m_cache.clear();
    m_boxes.clear();
    clearSelection();
    relayout();
}

int PageView::pageCount() const
{
    return m_doc ? m_doc->pageCount() : 0;
}

QString PageView::fitName(Fit f)
{
    switch (f) {
    case Fit::Width:   return QStringLiteral("width");
    case Fit::Height:  return QStringLiteral("height");
    case Fit::Page:    return QStringLiteral("page");
    case Fit::Content: return QStringLiteral("content");
    case Fit::Custom:  return QStringLiteral("custom");
    }
    return QStringLiteral("width");
}

PageView::Fit PageView::fitFromName(const QString &name, Fit fallback)
{
    if (name == QLatin1String("width"))   return Fit::Width;
    if (name == QLatin1String("height"))  return Fit::Height;
    if (name == QLatin1String("page"))    return Fit::Page;
    if (name == QLatin1String("content")) return Fit::Content;
    if (name == QLatin1String("custom"))  return Fit::Custom;
    return fallback;
}

QString PageView::themeName(Theme t)
{
    switch (t) {
    case Theme::Normal:     return QStringLiteral("normal");
    case Theme::Invert:     return QStringLiteral("invert");
    case Theme::Catppuccin: return QStringLiteral("catppuccin");
    case Theme::TokyoNight: return QStringLiteral("tokyonight");
    }
    return QStringLiteral("normal");
}

PageView::Theme PageView::themeFromName(const QString &name, Theme fallback)
{
    if (name == QLatin1String("invert"))     return Theme::Invert;
    if (name == QLatin1String("catppuccin")) return Theme::Catppuccin;
    if (name == QLatin1String("tokyonight")) return Theme::TokyoNight;
    if (name == QLatin1String("normal"))     return Theme::Normal;
    return fallback;
}

int PageView::themeCount()
{
    return 4;
}

QColor PageView::themePaper(Theme t) { return QColor::fromRgb(themeInfo(t).paper); }
QColor PageView::themeInk(Theme t)   { return QColor::fromRgb(themeInfo(t).ink); }
QColor PageView::themeBg(Theme t)    { return QColor::fromRgb(themeInfo(t).bg); }

void PageView::setTheme(Theme t)
{
    if (m_theme == t)
        return;
    m_theme = t;
    m_cache.clear();          // 主题进缓存 key，直接整表作废最省事
    emit themeChanged(m_theme);
    viewport()->update();
}

void PageView::cycleTheme()
{
    setTheme(Theme((int(m_theme) + 1) % themeCount()));
}

void PageView::setFit(Fit f)
{
    const bool changed = (m_fit != f);
    m_fit = f;
    relayout();
    if (changed)
        emit fitChanged(m_fit);
}

void PageView::setZoom(qreal z)
{
    z = std::clamp(z, 0.05, 20.0);
    const bool fitChanged_ = (m_fit != Fit::Custom);
    const bool zoomChanged_ = !qFuzzyCompare(m_zoom, z);
    m_zoom = z;
    if (fitChanged_) {
        m_fit = Fit::Custom;
        emit fitChanged(m_fit);
    }
    relayout();
    if (zoomChanged_)
        emit zoomChanged(m_zoom);
}

void PageView::jumpToPage(int page)
{
    if (!m_doc || m_doc->pageCount() <= 0)
        return;
    page = std::clamp(page, 0, m_doc->pageCount() - 1);
    if (page == m_page)
        return;
    m_page = page;
    clearSelection();
    relayout();
    emit currentPageChanged(m_page);
}

void PageView::nextPage() { jumpToPage(m_page + 1); }
void PageView::prevPage() { jumpToPage(m_page - 1); }
void PageView::firstPage() { jumpToPage(0); }
void PageView::lastPage() { jumpToPage(pageCount() - 1); }

// ---------------- 文本选择 ----------------

void PageView::clearSelection()
{
    if (m_selBounds.isEmpty() && m_selText.isEmpty())
        return;
    m_selBounds.clear();
    m_selText.clear();
    viewport()->update();
}

void PageView::updateSelection(const QPointF &from, const QPointF &to)
{
    if (!m_doc)
        return;
    const QPdfSelection sel = m_doc->getSelection(m_page, from, to);
    m_selBounds = sel.bounds();
    m_selText = sel.text();
    viewport()->update();
}

void PageView::selectAllPage()
{
    if (!m_doc || m_doc->pageCount() <= 0)
        return;
    const QPdfSelection sel = m_doc->getAllText(m_page);
    m_selBounds = sel.bounds();
    m_selText = sel.text();
    if (!m_selText.isEmpty())
        copySelection(true);
    viewport()->update();
}

void PageView::copySelection(bool primary)
{
    if (m_selText.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(
        m_selText, primary ? QClipboard::Selection : QClipboard::Clipboard);
}

QPdfLink PageView::linkAtPoint(const QPointF &pagePos) const
{
    if (!m_linkModel)
        return {};
    for (int r = 0; r < m_linkModel->rowCount(QModelIndex()); ++r) {
        const QModelIndex idx = m_linkModel->index(r, 0);
        const QPdfLink link = idx.data(int(QPdfLinkModel::Role::Link)).value<QPdfLink>();
        // 注意：QPdfLink::isValid() 对 URI 链接返回 false，只能看 url / page
        const bool usable = (!link.url().isEmpty()) || link.page() >= 0;
        if (!usable)
            continue;
        for (const QRectF &rc : link.rectangles())
            if (rc.contains(pagePos))
                return link;
    }
    return {};
}

// ---------------- 坐标换算 ----------------

QPointF PageView::pageOrigin() const
{
    const QSizeF pts = m_doc ? m_doc->pagePointSize(m_page) : QSizeF();
    const QSizeF pagePx = pts * m_zoom;
    qreal x = std::max(0.0, (viewport()->width() - pagePx.width()) / 2.0)
              - horizontalScrollBar()->value();
    qreal y = std::max(0.0, (viewport()->height() - pagePx.height()) / 2.0)
              - verticalScrollBar()->value();

    if (m_doc && m_fit == Fit::Content) {   // 内容包围盒居中，而非纸张居中
        auto *self = const_cast<PageView *>(this);
        const QRectF box = self->contentBox(m_page);
        x += pagePx.width() / 2.0 - (box.x() + box.width() / 2.0) * m_zoom;
        y += pagePx.height() / 2.0 - (box.y() + box.height() / 2.0) * m_zoom;
    }
    return {x, y};
}

QPointF PageView::viewToPage(const QPointF &viewPos) const
{
    const QPointF o = pageOrigin();
    return {(viewPos.x() - o.x()) / m_zoom, (viewPos.y() - o.y()) / m_zoom};
}

// ---------------- 选择 / 链接的鼠标交互 ----------------

void PageView::wheelEvent(QWheelEvent *ev)
{
    if (ev->modifiers() & Qt::ControlModifier) {
        setZoom(m_zoom * std::pow(1.0015, ev->angleDelta().y()));
        ev->accept();
        return;
    }

    const int delta = ev->angleDelta().y();
    if (delta == 0) {
        QAbstractScrollArea::wheelEvent(ev);
        return;
    }

    QScrollBar *v = verticalScrollBar();
    const int old = v->value();
    v->setValue(old - delta / 2);
    if (v->value() == old) {          // 到顶/到底 → 翻页
        if (delta < 0)
            nextPage();
        else
            prevPage();
    }
    ev->accept();
}

void PageView::mousePressEvent(QMouseEvent *ev)
{
    if (m_doc && m_doc->pageCount() > 0 && ev->button() == Qt::LeftButton) {
        m_selAnchor = viewToPage(ev->position());
        m_selecting = true;
        m_dragged = false;
    }
    QAbstractScrollArea::mousePressEvent(ev);
}

void PageView::mouseMoveEvent(QMouseEvent *ev)
{
    if (m_selecting && (ev->buttons() & Qt::LeftButton)) {
        if (!m_dragged) {
            const QPointF d = ev->position() - (pageOrigin() + m_selAnchor * m_zoom);
            if (std::abs(d.x()) + std::abs(d.y()) > kDragThreshold)
                m_dragged = true;
        }
        if (m_dragged)
            updateSelection(m_selAnchor, viewToPage(ev->position()));
    } else if (m_doc && m_doc->pageCount() > 0) {
        // 悬停在链接上 → 手型光标 + tooltip
        const QPdfLink link = linkAtPoint(viewToPage(ev->position()));
        const bool over = !link.url().isEmpty() || link.page() >= 0;
        viewport()->setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
        const QString tip = over ? link.url().toString() : QString();
        if (viewport()->toolTip() != tip)
            viewport()->setToolTip(tip);
    }
    QAbstractScrollArea::mouseMoveEvent(ev);
}

void PageView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton && m_selecting) {
        if (m_dragged) {
            copySelection(true);        // 选中即写入 primary selection（中键粘贴）
        } else {
            clearSelection();
            const QPdfLink link = linkAtPoint(viewToPage(ev->position()));
            if (!link.url().isEmpty())
                QDesktopServices::openUrl(link.url());   // 外链：交给系统默认程序打开
            else if (link.page() >= 0)
                jumpToPage(link.page());                 // 内链：跳页
        }
        m_selecting = false;
        m_dragged = false;
    }
    QAbstractScrollArea::mouseReleaseEvent(ev);
}

// ---------------- 渲染 ----------------

QRectF PageView::contentBox(int page)
{
    if (m_boxes.contains(page))
        return m_boxes.value(page);

    const QSizeF pts = m_doc->pagePointSize(page);
    if (pts.isEmpty() || pts.width() <= 0)
        return {QPointF(0, 0), pts};

    const int pw = kProbeWidth;
    const int ph = std::max(1, int(kProbeWidth * pts.height() / pts.width()));
    const QImage probe = m_doc->render(page, QSize(pw, ph));

    int l = probe.width(), t = probe.height(), r = -1, b = -1;
    for (int y = 0; y < probe.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(probe.constScanLine(y));
        for (int x = 0; x < probe.width(); ++x) {
            const QRgb px = line[x];
            // render() 背景是透明的，必须看 alpha；白色实底填充不算内容
            if (qAlpha(px) > 8 && qGray(px) < 0xF0) {
                l = std::min(l, x);
                r = std::max(r, x);
                t = std::min(t, y);
                b = std::max(b, y);
            }
        }
    }

    QRectF box;
    if (r < l) {                                  // 空白页
        box = QRectF(QPointF(0, 0), pts);
    } else {
        box = QRectF(l * pts.width() / probe.width(),
                     t * pts.height() / probe.height(),
                     (r - l + 1) * pts.width() / probe.width(),
                     (b - t + 1) * pts.height() / probe.height());
    }
    m_boxes.insert(page, box);
    return box;
}

QImage PageView::applyTheme(QImage img) const
{
    if (m_theme == Theme::Normal)
        return img;

    const QVector<QRgb> &lut = themeLut(m_theme);
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            line[x] = (px & 0xFF000000) | (lut[qGray(px)] & 0x00FFFFFF);
        }
    }
    return img;
}

QImage PageView::renderFor(int page, QSize px)
{
    px.setWidth(std::clamp(px.width(), 1, kMaxRenderDim));
    px.setHeight(std::clamp(px.height(), 1, kMaxRenderDim));

    const QString key = QStringLiteral("%1:%2x%3:%4")
                            .arg(page).arg(px.width()).arg(px.height())
                            .arg(themeName(m_theme));
    if (const QImage *cached = m_cache.object(key))
        return *cached;

    QImage img = m_doc->render(page, px);          // PDFium 同步渲染
    img = applyTheme(std::move(img));              // duotone 重着色
    m_cache.insert(key, new QImage(img), std::max(1, int(img.sizeInBytes() >> 10)));
    return img;
}

void PageView::relayout()
{
    if (m_linkModel)
        m_linkModel->setPage(m_page);

    if (!m_doc || m_doc->pageCount() <= 0) {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
        viewport()->update();
        return;
    }

    m_page = std::clamp(m_page, 0, m_doc->pageCount() - 1);
    const QSizeF pts = m_doc->pagePointSize(m_page);
    const QSizeF vr = viewport()->size();

    QRectF box(QPointF(0, 0), pts);
    if (m_fit == Fit::Content)
        box = contentBox(m_page);

    if (!pts.isEmpty() && !vr.isEmpty()) {
        switch (m_fit) {
        case Fit::Width:
            m_zoom = (vr.width() - 2 * kMargin) / box.width();
            break;
        case Fit::Height:
            m_zoom = (vr.height() - 2 * kMargin) / box.height();
            break;
        case Fit::Page:
        case Fit::Content:
            m_zoom = std::min((vr.width() - 2 * kMargin) / box.width(),
                              (vr.height() - 2 * kMargin) / box.height());
            break;
        case Fit::Custom:
            break;
        }
        m_zoom = std::clamp(m_zoom, 0.05, 20.0);
    }

    const QSizeF pagePx = pts * m_zoom;
    horizontalScrollBar()->setRange(0, std::max(0, int(pagePx.width() + 2 * kMargin - vr.width())));
    horizontalScrollBar()->setPageStep(int(vr.width()));
    verticalScrollBar()->setRange(0, std::max(0, int(pagePx.height() + 2 * kMargin - vr.height())));
    verticalScrollBar()->setPageStep(int(vr.height()));

    emit zoomChanged(m_zoom);
    viewport()->update();
}

void PageView::resizeEvent(QResizeEvent *ev)
{
    QAbstractScrollArea::resizeEvent(ev);
    relayout();
}

void PageView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

void PageView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), themeBg(m_theme));

    if (!m_doc || m_doc->pageCount() <= 0)
        return;

    const QSizeF pts = m_doc->pagePointSize(m_page);
    if (pts.isEmpty())
        return;

    const qreal dpr = devicePixelRatioF();
    const QSize px = (pts * m_zoom * dpr).toSize();
    QImage img = renderFor(m_page, px);
    img.setDevicePixelRatio(dpr);

    const QPointF o = pageOrigin();
    const QSizeF pagePx = pts * m_zoom;

    // 纸张阴影 + 纸面（render() 输出透明背景，纸色在这里补）
    p.fillRect(QRectF(o + QPointF(3, 3), pagePx), QColor(0, 0, 0, 60));
    p.fillRect(QRectF(o, pagePx), themePaper(m_theme));
    p.drawImage(o, img);

    // 选择高亮
    if (!m_selBounds.isEmpty()) {
        p.save();
        p.translate(o);
        p.scale(m_zoom, m_zoom);
        QColor hl = themeInk(m_theme);
        hl.setAlpha(110);
        p.setPen(Qt::NoPen);
        p.setBrush(hl);
        for (const QPolygonF &poly : std::as_const(m_selBounds))
            p.drawPolygon(poly);
        p.restore();
    }
}
