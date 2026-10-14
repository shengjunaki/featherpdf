#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("featherpdf"));
    QApplication::setApplicationName(QStringLiteral("featherpdf"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Wayland app_id（niri window-rule 匹配用），必须在建窗口之前设置
    QGuiApplication::setDesktopFileName(QStringLiteral("featherpdf"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FeatherPDF — 轻量 Qt6 PDF 阅读器"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("要打开的 PDF 文件"));
    parser.process(app);

    MainWindow win;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        win.openFile(args.first());
    } else {
        // 不指定文件 → 默认打开上次的 PDF
        QSettings s;
        const QString last = s.value(QStringLiteral("lastFile")).toString();
        if (!last.isEmpty() && QFileInfo::exists(last))
            win.openFile(last);
    }
    win.show();
    return app.exec();
}
