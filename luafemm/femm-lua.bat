@echo off
setlocal EnableDelayedExpansion

rem ====================================================================
rem  femm-lua.bat - run a legacy FEMM Lua 4 script (or an interactive
rem                 Lua prompt) against femm-qt.
rem
rem  Usage:  femm-lua [-h] [-p PORT] [script.lua [args...]]
rem            -h, --headless   drive femm_headlessw (no GUI window)
rem            -p, --port PORT  use TCP port PORT instead of the default 48015
rem            (no script)      start an interactive Lua prompt
rem
rem  -p is what makes two of these run at the same time: femm-qt serves one
rem  client per port, so simultaneous runs sharing 48015 collide -- one wins
rem  and the other is turned away. Give each its own port:
rem
rem      start "" femm-lua -h -p 48016 job1.lua
rem      start "" femm-lua -h -p 48017 job2.lua
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
set "SETPORT="
:parse
if /i "%~1"=="-h"         ( set "MODE=1" & shift & goto parse )
if /i "%~1"=="--headless" ( set "MODE=1" & shift & goto parse )
if /i "%~1"=="-p"         goto setport
if /i "%~1"=="--port"     goto setport
if /i "%~1"=="-?"         goto usage
if /i "%~1"=="--help"     goto usage
goto parsed

:setport
if "%~2"=="" (
    echo femm-lua: %~1 needs a port number 1>&2
    exit /b 1
)
rem Reject anything that is not all digits, so a typo becomes a message here
rem rather than a Lua syntax error inside the preamble.
echo %~2| findstr /r /c:"^[0-9][0-9]*$" >NUL || (
    echo femm-lua: not a port number: %~2 1>&2
    exit /b 1
)
set "SETPORT=_femm_port=%~2; "
shift & shift & goto parse

:parsed
rem _femm_port must be set BEFORE openfemm(), which is why the port cannot be
rem chosen from inside the script: this preamble has already connected by the
rem time the script's first line runs.
rem openfemm(0) = latch/spawn GUI, openfemm(1) = latch/spawn femm_headlessw
set "PREAMBLE=dofile([[%FEMMLUA%]]); !SETPORT!openfemm(%MODE%)"

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
rem Propagate the interpreter's exit status, and take it with NOTHING between
rem the call and the exit -- not even a rem, which is enough to lose it.
rem
rem "goto :eof" ended the batch on whatever errorlevel happened to be current
rem rather than on lua4's, so a script that died reported SUCCESS to whoever
rem ran femm-lua. Invisible to a human watching the traceback scroll past, and
rem exactly the wrong answer for anything scripted around it.
"%LUA4%" -e "!PREAMBLE!" -f!ARGS!
exit /b %errorlevel%

:usage
echo Usage: %~n0 [-h] [-p PORT] [script.lua [args...]]
echo    -h, --headless   use femm_headlessw instead of the GUI
echo    -p, --port PORT  use TCP port PORT instead of the default 48015
echo    (no script)      start an interactive Lua prompt connected to FEMM
