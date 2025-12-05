cmake_minimum_required(VERSION 3.16)
project(ecg_qt_viewer_project LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(pybind11 REQUIRED)

find_package(Qt5 COMPONENTS Core Widgets PrintSupport REQUIRED)

set(ECG_QT_VIEWER_SOURCES
    src/ECGViewerSetup.cpp
    src/ECGViewerPlot.cpp
    src/ECGViewerAnnotations.cpp
    src/ECGViewerInteractions.cpp
    src/ECGModule.cpp
    src/qcustomplot.cpp
    include/plotter/qcustomplot.h
)

pybind11_add_module(ecg_qt_viewer
    ${ECG_QT_VIEWER_SOURCES}
)

target_include_directories(ecg_qt_viewer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/plotter"
)

target_link_libraries(ecg_qt_viewer
    PRIVATE
        Qt5::Core
        Qt5::Widgets
        Qt5::PrintSupport
)

set_target_properties(ecg_qt_viewer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    ARCHIVE_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
)

set(PARSE_ECG_SOURCES
    src/ParseEcg.cpp
)

pybind11_add_module(parse_ecg
    ${PARSE_ECG_SOURCES}
)

set_target_properties(parse_ecg PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    ARCHIVE_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
)

# Usage from plotter/build:
#   cmake --build . --target clean-all
# OR 
#   make clean
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE:ecg_qt_viewer>"
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE:parse_ecg>"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/CMakeCache.txt"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_BINARY_DIR}/CMakeFiles"
        "${CMAKE_BINARY_DIR}/ecg_qt_viewer_autogen"
    COMMENT "Removing built ecg_qt_viewer & parse_ecg modules and CMake build files"
)
