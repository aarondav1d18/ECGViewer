cmake_minimum_required(VERSION 3.16)
project(ECGViewer_project LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

# Cross-platform: help CMake find Qt5 and pybind11 on Windows
# If the user set QT5_DIR or Qt5_DIR env var / cmake var, append it.
# This covers manual installs, the Qt online installer, vcpkg, and conda.
if(DEFINED ENV{QT5_DIR} AND NOT DEFINED Qt5_DIR)
    set(Qt5_DIR "$ENV{QT5_DIR}")
endif()

if(DEFINED Qt5_DIR)
    list(APPEND CMAKE_PREFIX_PATH "${Qt5_DIR}")
endif()

# Conda environments: add conda prefix so find_package works out of the box
if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/Library")          # Windows conda
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/Library/lib/cmake") # Windows conda Qt5
endif()

# vcpkg integration (if VCPKG_ROOT is set)
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain")
endif()

set(PYBIND11_FINDPYTHON ON)
find_package(pybind11 REQUIRED)

find_package(Qt5 COMPONENTS Core Widgets PrintSupport REQUIRED)

set(ECG_VIEWER_SOURCES
    src/ECGViewerSetup.cpp
    src/ECGViewerPlot.cpp
    src/ECGViewerAnnotations.cpp
    src/ECGViewerInteractions.cpp
    src/ECGModule.cpp
    src/qcustomplot.cpp
    include/ECGViewer/qcustomplot.h
)

pybind11_add_module(ECGViewer
    ${ECG_VIEWER_SOURCES}
)

target_include_directories(ECGViewer
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/ECGViewer"
)

target_link_libraries(ECGViewer
    PRIVATE
        Qt5::Core
        Qt5::Widgets
        Qt5::PrintSupport
)

set_target_properties(ECGViewer PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    ARCHIVE_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
)

# Windows: MSVC multi-config generators put outputs in Debug/Release subdirs.
# Force them all to the same flat directory.
if(WIN32)
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(ECGViewer PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
            RUNTIME_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
            ARCHIVE_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
        )
    endforeach()
endif()

set(PARSE_ECG_SOURCES
    src/ParseEcg.cpp
)

pybind11_add_module(parseECG
    ${PARSE_ECG_SOURCES}
)

set_target_properties(parseECG PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    RUNTIME_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
    ARCHIVE_OUTPUT_DIRECTORY  "${CMAKE_CURRENT_SOURCE_DIR}/"
)

if(WIN32)
    foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(parseECG PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
            RUNTIME_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
            ARCHIVE_OUTPUT_DIRECTORY_${cfg}  "${CMAKE_CURRENT_SOURCE_DIR}/"
        )
    endforeach()
endif()

# clean-all target
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE:ECGViewer>"
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE:parseECG>"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_BINARY_DIR}/CMakeCache.txt"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "${CMAKE_BINARY_DIR}/CMakeFiles"
        "${CMAKE_BINARY_DIR}/ECGViewer_autogen"
    COMMENT "Removing built ECGViewer & parseECG modules and CMake build files"
)
