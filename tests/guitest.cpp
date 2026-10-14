// 无头回归测试：由 tests/run_tests.sh 构建运行
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QSettings>
#include <QUrl>
#include <QWidget>
#include <cstdio>
#include "mainwindow.h"
#include "pageview.h"
#include <QPdfLink>

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) fprintf(stderr, "PASS  %s\n", msg); \
    else { fprintf(stderr, "FAIL  %s\n", msg); ++fails; } } while (0)

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("featherpdf-test");
    QCoreApplication::setApplicationName("guitest");

    MainWindow w;
    w.resize(1000, 800);
    w.show();
    QCoreApplication::processEvents();

    CHECK(w.openFile(QString::fromLocal8Bit(argv[1])), "openFile(test.pdf)");

    PageView *v = w.findChild<PageView *>();
    CHECK(v != nullptr, "find PageView");
    CHECK(v->pageCount() == 3, "pageCount == 3");
    CHECK(v->theme() == PageView::Theme::Normal, "theme default Normal");

    // ---- 主题真的改变渲染像素，且 duotone 落在 paper/ink 色上 ----
    QImage a = v->grab().toImage();
    v->setTheme(PageView::Theme::Invert);
    QImage b = v->grab().toImage();
    CHECK(a.size() == b.size() && a != b, "theme changes rendered pixels");
    const QPoint center(a.width() / 2, a.height() / 2);   // 页面中央是纸面
    QColor paperN = a.pixelColor(center);
    QColor paperI = b.pixelColor(center);
    CHECK(paperN.lightness() > 200 && paperI.lightness() < 60,
          "Normal paper bright / Invert paper dark");

    v->setTheme(PageView::Theme::Catppuccin);
    QImage c = v->grab().toImage();
    QColor paperC = c.pixelColor(center);
    CHECK(qAbs(paperC.red() - 0x1e) < 6 && qAbs(paperC.blue() - 0x2e) < 6,
          "Catppuccin paper ~= base #1e1e2e");

    v->setTheme(PageView::Theme::TokyoNight);
    QImage d = v->grab().toImage();
    QColor paperT = d.pixelColor(center);
    CHECK(qAbs(paperT.red() - 0x1a) < 6 && qAbs(paperT.blue() - 0x26) < 6,
          "TokyoNight paper ~= #1a1b26");

    // 循环顺序：TokyoNight -> Normal
    v->cycleTheme();
    CHECK(v->theme() == PageView::Theme::Normal, "cycleTheme wraps to Normal");

    // ---- 文本选择 / 链接（页面 pt 坐标，顶部原点）----
    v->jumpToPage(1);
    QPdfLink link = v->linkAtPoint(QPointF(200, 430));   // "example.com" 链接矩形内
    CHECK(!link.url().isEmpty() && link.url() == QUrl("https://example.com"),
          "linkAtPoint finds https://example.com");
    CHECK(v->linkAtPoint(QPointF(550, 800)).url().isEmpty(), "linkAtPoint empty outside links");

    v->selectAllPage();
    CHECK(v->hasSelection(), "selectAllPage makes selection");
    CHECK(v->selectedText().contains("Section 1.1") && v->selectedText().contains("example.com"),
          "selectedText contains page text");
    v->copySelection(false);
    CHECK(QApplication::clipboard()->text() == v->selectedText(), "Ctrl+C copies selection");
    v->copySelection(true);
    if (QApplication::clipboard()->supportsSelection()) {
        CHECK(QApplication::clipboard()->text(QClipboard::Selection) == v->selectedText(),
              "primary selection set (middle-click paste)");
    } else {
        fprintf(stderr, "NOTE  primary selection unsupported on this QPA (offscreen)"
                        " — 真实 Wayland/niri 下有效\n");
    }
    v->clearSelection();
    CHECK(!v->hasSelection(), "clearSelection");

    // ---- fit 模式 ----
    v->setFit(PageView::Fit::Width);   qreal zw = v->zoom();
    v->setFit(PageView::Fit::Height);  qreal zh = v->zoom();
    v->setFit(PageView::Fit::Page);    qreal zp = v->zoom();
    v->setFit(PageView::Fit::Content); qreal zc = v->zoom();
    CHECK(zw > 0 && zh > 0 && zp > 0 && zc > 0, "all fits produce positive zoom");
    CHECK(zp <= zw + 1e-6 && zp <= zh + 1e-6, "fit-page <= fit-width/height");
    CHECK(zc > zp, "fit-content zooms in more than fit-page (sparse page)");
    fprintf(stderr, "  zooms: width=%.2f height=%.2f page=%.2f content=%.2f\n", zw, zh, zp, zc);

    // ---- 翻页 + 越界钳制 ----
    v->jumpToPage(2);
    CHECK(v->currentPage() == 2, "jumpToPage(2)");
    v->nextPage();
    CHECK(v->currentPage() == 2, "nextPage clamps at last page");
    v->firstPage();
    CHECK(v->currentPage() == 0, "firstPage");
    v->prevPage();
    CHECK(v->currentPage() == 0, "prevPage clamps at first page");

    // ---- 缩放 ----
    v->setZoom(1.5);
    CHECK(v->fit() == PageView::Fit::Custom && qAbs(v->zoom() - 1.5) < 1e-6, "setZoom -> Custom fit");
    v->setZoom(100.0);
    CHECK(v->zoom() <= 20.0, "zoom clamped to 20x");

    // ---- 每文档记忆 + lastFile：关窗 → 新窗口恢复 ----
    v->jumpToPage(1);
    v->setTheme(PageView::Theme::Catppuccin);
    v->setFit(PageView::Fit::Height);
    w.close();
    QCoreApplication::processEvents();

    {
        MainWindow w2;
        w2.resize(1000, 800);
        w2.show();
        CHECK(w2.openFile(QString::fromLocal8Bit(argv[1])), "reopen file");
        PageView *v2 = w2.findChild<PageView *>();
        CHECK(v2->currentPage() == 1, "restored page == 1");
        CHECK(v2->theme() == PageView::Theme::Catppuccin, "restored theme == Catppuccin");
        CHECK(v2->fit() == PageView::Fit::Height, "restored fit == Height");
        w2.close();
    }

    QSettings s;
    QStringList recent = s.value("recent").toStringList();
    CHECK(recent.size() == 1 && recent.first().endsWith("test.pdf"), "recent list has 1 entry");
    CHECK(s.value("lastFile").toString().endsWith("test.pdf"), "lastFile recorded");

    fprintf(stderr, fails ? "\n== %d FAILURES ==\n" : "\n== ALL PASS ==\n", fails);
    return fails;
}
