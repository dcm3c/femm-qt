@echo off
setlocal EnableDelayedExpansion

rem ====================================================================
rem  femm-lua.bat - run a legacy FEMM Lua 4 script (or an interactive
rem                 Lua prompt) against femm-qt.
rem
rem  Usage:  femm-lua [-h] [script.lua [args...]]
rem            -h, --headless   drive femm_headlessw (no GUI window)
rem            (no script)      start an interactive Lua prompt
rem
rem  Self-contained: the bundled Lua 4 interpreter (lua4.exe), femm.lua,
rem  and the femm-qt executables all live in this same folder (bin\).
rem ====================================================================

rem --- bundled interpreter + binding live beside this batch file ---
set "LUA4=%~dp0lua4.exe"
set "FEMMLUA=%~dp0femm.lua"
if not exist "%LUA4%" (
    echo femm-lua: cannot find lua4.exe next to this script: "%LUA4%" 1>&2
    exit /b 1
)
if not exist "%FEMMLUA%" (
    echo femm-lua: cannot find femm.lua next to this script: "%FEMMLUA%" 1>&2
    exit /b 1
)

rem --- FEMM_DIR: the bundled femm_gui / femm_headlessw live in this folder, so
rem     always point at it. Doing so unconditionally means an inherited
rem     FEMM_DIR (e.g. from another FEMM install) can't redirect us to a server
rem     that lacks these binaries. This is inside setlocal, so it does not
rem     change FEMM_DIR in your environment after the script exits. The
rem     for-loop normalizes %~dp0, which carries a trailing backslash. ---
for %%I in ("%~dp0.") do set "FEMM_DIR=%%~fI"

rem --- parse leading options ---
set "MODE=0"
:parse
if /i "%~1"=="-h"         ( set "MODE=1" & shift & goto parse )
if /i "%~1"=="--headless" ( set "MODE=1" & shift & goto parse )
if /i "%~1"=="-?"         goto usage
if /i "%~1"=="--help"     goto usage

rem openfemm(0) = latch/spawn GUI, openfemm(1) = latch/spawn femm_headlessw
set "PREAMBLE=dofile([[%FEMMLUA%]]); openfemm(%MODE%)"

if "%~1"=="" (
    rem no script -> interactive REPL connected to FEMM. -v prints the Lua
    rem banner first; the bare-interpreter banner is skipped on the explicit
    rem -i path, so we ask for it here.
    "%LUA4%" -v -e "!PREAMBLE!" -i
    goto :eof
)

rem --- collect the script and its args, quotes preserved ---
set "ARGS="
:collect
if "%~1"=="" goto run
set "ARGS=!ARGS! %1"
shift
goto collect

:run
"%LUA4%" -e "!PREAMBLE!" -f!ARGS!
goto :eof

:usage
echo Usage: %~n0 [-h] [script.lua [args...]]
echo    -h, --headless   use femm_headlessw instead of the GUI
echo    (no script)      start an interactive Lua prompt connected to FEMM
