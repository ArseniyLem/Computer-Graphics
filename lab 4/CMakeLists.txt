cmake_minimum_required(VERSION 3.16)
project(Lab4Clipping LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Работает и с Qt6, и с Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(Lab4Clipping
    main.cpp
    Geometry.h
    Clipping.h Clipping.cpp
    CanvasWidget.h CanvasWidget.cpp
    MainWindow.h MainWindow.cpp
)
target_link_libraries(Lab4Clipping PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
