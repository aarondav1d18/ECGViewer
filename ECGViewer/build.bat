@echo off
setlocal enabledelayedexpansion

:: ---------------------------------------------------------------------------
:: build.bat  –  Windows equivalent of build.bash
::
:: Usage:
::   build.bat              Build (default)
::   build.bat --clean      Clean build directory
::   build.bat --clean-build  Clean then build
::   build.bat --help       Show usage
::
:: Environment variables you can set BEFORE running:
::   QT5_DIR      Path to Qt5 cmake dir, e.g. C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5
::   VCPKG_ROOT   Path to vcpkg root (auto-detected if vcpkg is on PATH)
::
:: If you use conda, just activate your environment first and it will be
:: detected automatically.
:: ---------------------------------------------------------------------------

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"

:: Parse arguments
set "ACTION=build"
if "%~1"=="--clean"       set "ACTION=clean"
if "%~1"=="--clean-build" set "ACTION=clean_and_build"
if "%~1"=="--help"        set "ACTION=help"

if "%ACTION%"=="help" (
    echo Usage: build.bat [--clean ^| --clean-build ^| --help]
    echo.
    echo   --clean        Remove all build files
    echo   --clean-build  Clean then do a fresh build
    echo   --help         Show this message
    echo.
    echo Environment variables:
    echo   QT5_DIR    Path to Qt5's cmake config directory
    echo   VCPKG_ROOT Path to vcpkg installation
    exit /b 0
)

if "%ACTION%"=="clean" goto :do_clean
if "%ACTION%"=="clean_and_build" (
    call :do_clean
    goto :do_build
)
goto :do_build

:: ---------------------------------------------------------------------------
:do_clean
echo Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)
echo Cleaned.
goto :eof

:: ---------------------------------------------------------------------------
:do_build
echo Creating build directory...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%BUILD_DIR%"

:: Detect available generator.  Prefer Ninja (fast), fallback to NMake, then
:: let CMake pick its default (usually Visual Studio).
set "GENERATOR="
where ninja >nul 2>&1 && set "GENERATOR=-G Ninja"
if not defined GENERATOR (
    where nmake >nul 2>&1 && set "GENERATOR=-G \"NMake Makefiles\""
)

echo Running CMake configure...
cmake .. %GENERATOR% -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo ======================================================================
    echo  CMake configure FAILED.
    echo.
    echo  Common fixes:
    echo    1. Install Qt5 and set QT5_DIR:
    echo         set QT5_DIR=C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5
    echo       Or with conda:
    echo         conda install qt=5
    echo    2. Install pybind11:
    echo         pip install pybind11
    echo    3. Make sure a C++ compiler is on PATH:
    echo         - Open "x64 Native Tools Command Prompt for VS 2022"
    echo         - Or install Build Tools for Visual Studio
    echo ======================================================================
    popd
    exit /b 1
)

echo Building...
cmake --build . --config Release -- %CMAKE_BUILD_PARALLEL%
if errorlevel 1 (
    echo Build FAILED.
    popd
    exit /b 1
)

popd
echo.
echo Build complete!  .pyd files are in %SCRIPT_DIR%
exit /b 0
