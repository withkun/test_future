cmake_minimum_required(VERSION 3.20)
project(test_future)

set(CMAKE_CXX_STANDARD 20)


# 检查qt
find_package(QT NAMES Qt6 Qt5 COMPONENTS Sql Qml Core Gui Quick QuickWidgets Widgets LinguistTools REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Sql Qml Core Gui Quick QuickWidgets Widgets LinguistTools REQUIRED)


set(my_sources
    utils/lib_export.h

    ui/layer_manager.cpp
    ui/layer_manager.h
    ui/layer_tool.cpp
    ui/layer_tool.h
    ui/osd.cpp
    ui/osd.h
    ui/osd_area.cpp
    ui/osd_area.h
    ui/roi.cpp
    ui/roi.h
    ui/roi_area.cpp
    ui/roi_area.h
    ui/tl_application.cpp
    ui/tl_application.h

    ui/roi/roi_circle.cpp
    ui/roi/roi_circle.h
    ui/roi/roi_freedraw.cpp
    ui/roi/roi_freedraw.h
    ui/roi/roi_rectangle.cpp
    ui/roi/roi_rectangle.h

    ui/osd/osd_circle.cpp
    ui/osd/osd_circle.h
    ui/osd/osd_polypoint.cpp
    ui/osd/osd_polypoint.h
    ui/osd/osd_rectangle.cpp
    ui/osd/osd_rectangle.h

    main.cpp
    mainwindow.cpp
    mainwindow.h
)

set(my_headers ${my_sources})
list(FILTER my_headers INCLUDE REGEX "\\.h$|\\.hpp")

set(my_includes
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set(my_libraries
    Qt${QT_VERSION_MAJOR}::Widgets
    ${OpenCV_LIBS}
)

list(APPEND my_defines HAVE_QT
    ${OPENCV_DEFINITION}
)

add_library(${PROJECT_NAME} SHARED
    ${my_sources}
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${my_includes}
)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    ${my_defines}
)

target_compile_options(${PROJECT_NAME} PUBLIC
    ${my_cflags}
)

target_link_options(${PROJECT_NAME} PUBLIC
    /DEBUG /NOLOGO
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    ${my_libraries}
)