#include <QCoreApplication>
#include <QPdfDocument>
#include <QPdfBookmarkModel>
#include <QImage>
#include <cstdio>
static void dump(QPdfBookmarkModel *m, const QModelIndex &p, int depth) {
    for (int r = 0; r < m->rowCount(p); ++r) {
        QModelIndex i = m->index(r, 0, p);
        fprintf(stderr, "%*srow=%d display='%s' title='%s' page=%d\n", depth*2, "", r,
                qPrintable(i.data(Qt::DisplayRole).toString()),
                qPrintable(i.data(int(QPdfBookmarkModel::Role::Title)).toString()),
                i.data(int(QPdfBookmarkModel::Role::Page)).toInt());
        dump(m, i, depth+1);
    }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QPdfDocument doc;
    auto err = doc.load(QString::fromLocal8Bit(argv[1]));
    fprintf(stderr, "load err=%d pages=%d page0=%.0fx%.0f\n", int(err), doc.pageCount(),
            doc.pagePointSize(0).width(), doc.pagePointSize(0).height());
    QPdfBookmarkModel m; m.setDocument(&doc);
    fprintf(stderr, "toc rows=%d\n", m.rowCount(QModelIndex()));
    dump(&m, QModelIndex(), 0);
    QImage img = doc.render(0, QSize(200, 260));
    fprintf(stderr, "render: %dx%d fmt=%d\n", img.width(), img.height(), int(img.format()));
    return 0;
}
