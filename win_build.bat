@echo off
rem ======================================================================
rem  win_build.bat - configure and build femm-qt on Windows (Qt + MinGW)
rem
rem  Usage:   win_build.bat [clean]
rem
rem  Run this from anywhere; it builds into femm-qt\build\.  Pass "clean"
rem  to delete the build directory first.
rem
rem  Qt, MinGW, CMake and Ninja are auto-detected under C:\Qt.  If you
rem  installed Qt elsewhere, or have several Qt versions and the wrong one
rem  is picked, set QT_DIR / MINGW_DIR below (or as environment variables).
rem ======================================================================
setlocal enabledelayedexpansion

rem ---- User-overridable settings (leave blank to auto-detect) ----------
if not defined QT_ROOT set "QT_ROOT=C:\Qt"
rem set "QT_DIR=C:\Qt\6.10.3\mingw_64"
rem set "MINGW_DIR=C:\Qt\Tools\mingw1310_64"

rem ---- Work in this script's own folder (the femm-qt source root) ------
cd /d "%~dp0"

rem ---- Handle the optional 'clean' argument ----------------------------
if /i "%~1"=="clean" (
    echo Removing build directory...
    if exist build rmdir /s /q build
)

rem ---- Auto-detect Qt 6 (MinGW build) ----------------------------------
if not defined QT_DIR (
    for /d %%D in ("%QT_ROOT%\6.*") do (
        if exist "%%D\mingw_64\lib\cmake\Qt6\" set "QT_DIR=%%D\mingw_64"
    )
)
if not defined QT_DIR (
    echo ERROR: Could not find a Qt 6 MinGW install under %QT_ROOT%.
    echo        Set QT_DIR near the top of this script.
    exit /b 1
)

rem ---- Auto-detect MinGW toolchain -------------------------------------
if not defined MINGW_DIR (
    for /d %%D in ("%QT_ROOT%\Tools\mingw*_64") do set "MINGW_DIR=%%D"
)
if not defined MINGW_DIR (
    echo ERROR: Could not find MinGW under %QT_ROOT%\Tools.
    echo        Set MINGW_DIR near the top of this script.
    exit /b 1
)

rem ---- Locate CMake and Ninja (prefer Qt's, fall back to PATH) ---------
set "CMAKE_EXE=%QT_ROOT%\Tools\CMake_64\bin\cmake.exe"
if not exist "%CMAKE_EXE%" set "CMAKE_EXE=cmake"
set "NINJA_EXE=%QT_ROOT%\Tools\Ninja\ninja.exe"
if not exist "%NINJA_EXE%" set "NINJA_EXE=ninja"

rem ---- CMake wants forward slashes in path arguments -------------------
set "QT_DIR_CM=%QT_DIR:\=/%"
set "MINGW_CM=%MINGW_DIR:\=/%"
set "NINJA_CM=%NINJA_EXE:\=/%"

echo Qt:    %QT_DIR%
echo MinGW: %MINGW_DIR%
echo CMake: %CMAKE_EXE%
echo Ninja: %NINJA_EXE%
echo.

rem ---- Configure -------------------------------------------------------
if not exist build mkdir build
cd build
"%CMAKE_EXE%" .. -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%QT_DIR_CM%" ^
    -DCMAKE_C_COMPILER="%MINGW_CM%/bin/gcc.exe" ^
    -DCMAKE_CXX_COMPILER="%MINGW_CM%/bin/g++.exe" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA_CM%"
if errorlevel 1 (
    echo.
    echo CMake configuration failed.
    exit /b 1
)

rem ---- Build -----------------------------------------------------------
"%NINJA_EXE%"
if errorlevel 1 (
    echo.
    echo Build failed.
    exit /b 1
)

rem ---- Copy Qt/MinGW runtime DLLs so the exe runs in place -------------
echo Copying runtime DLLs...

rem Qt core DLLs
for %%F in (Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Svg.dll) do (
    copy /y "%QT_DIR%\bin\%%F" bin\ >nul 2>&1
)

rem MinGW runtime DLLs
for %%F in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
    copy /y "%QT_DIR%\bin\%%F" bin\ >nul 2>&1
)

rem Qt plugins (platforms, styles, image formats, icon engines)
if not exist bin\platforms mkdir bin\platforms
copy /y "%QT_DIR%\plugins\platforms\qwindows.dll" bin\platforms\ >nul 2>&1

if not exist bin\styles mkdir bin\styles
copy /y "%QT_DIR%\plugins\styles\qmodernwindowsstyle.dll" bin\styles\ >nul 2>&1

if not exist bin\imageformats mkdir bin\imageformats
for %%F in (qico.dll qjpeg.dll qgif.dll qsvg.dll) do (
    copy /y "%QT_DIR%\plugins\imageformats\%%F" bin\imageformats\ >nul 2>&1
)

if not exist bin\iconengines mkdir bin\iconengines
copy /y "%QT_DIR%\plugins\iconengines\qsvgicon.dll" bin\iconengines\ >nul 2>&1

rem ---- Copy material libraries so solvers find them in bin\ ------------
for %%F in (..\matlibs\*.dat) do copy /y "%%F" bin\ >nul 2>&1

rem ---- Copy the Lua 4 legacy client assets (lua4.exe built into bin\) --
copy /y "..\luafemm\femm.lua"     bin\ >nul 2>&1
copy /y "..\luafemm\femm-lua.bat" bin\ >nul 2>&1

echo.
echo Build complete.  Executables and DLLs are in build\bin\
endlocal
