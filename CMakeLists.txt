cmake_minimum_required(VERSION 3.21)
project(featherpdf VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 只链 Widgets + Pdf，不碰 Quick/QML
find_package(Qt6 REQUIRED COMPONENTS Widgets Pdf)

qt_add_executable(featherpdf
    src/main.cpp
    src/mainwindow.cpp
    src/pageview.cpp
)

target_link_libraries(featherpdf PRIVATE Qt6::Widgets Qt6::Pdf)

# 轻量化：发布构建去掉符号表
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(featherpdf PRIVATE -s)
endif()

install(TARGETS featherpdf RUNTIME DESTINATION bin)
install(FILES featherpdf.desktop DESTINATION share/applications)
install(FILES assets/featherpdf.svg DESTINATION share/icons/hicolor/scalable/apps)
