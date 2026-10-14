QT       += widgets pdf
CONFIG   += c++17 warn_on
TEMPLATE  = app
TARGET    = featherpdf

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/pageview.cpp

HEADERS += \
    src/mainwindow.h \
    src/pageview.h

unix:!android {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    desktop.files = featherpdf.desktop
    desktop.path = $$PREFIX/share/applications
    INSTALLS += target desktop
}
