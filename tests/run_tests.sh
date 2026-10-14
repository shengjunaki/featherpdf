#!/usr/bin/env bash
# 无头回归测试：构建 guitest 并对 tests/test.pdf 跑一遍
set -e
cd "$(dirname "$0")/.."
MOC=$(qmake6 -query QT_HOST_LIBEXECS)/moc
[ -x "$MOC" ] || MOC=/usr/lib/qt6/moc
OUT=/tmp/featherpdf-tests
mkdir -p "$OUT"
"$MOC" src/pageview.h  -o "$OUT/moc_pageview.cpp"
"$MOC" src/mainwindow.h -o "$OUT/moc_mainwindow.cpp"
g++ -std=c++17 -fPIC -Isrc $(pkg-config --cflags Qt6Widgets Qt6Pdf) \
    tests/guitest.cpp src/pageview.cpp src/mainwindow.cpp \
    "$OUT/moc_pageview.cpp" "$OUT/moc_mainwindow.cpp" \
    -o "$OUT/guitest" $(pkg-config --libs Qt6Widgets Qt6Pdf)
rm -f ~/.config/featherpdf-test/guitest.conf
QT_QPA_PLATFORM=offscreen "$OUT/guitest" tests/test.pdf
