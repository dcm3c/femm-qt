#!/usr/bin/env bash
# ====================================================================
#  femm-lua.sh - run a legacy FEMM Lua 4 script (or an interactive Lua
#                prompt) against femm-qt.
#
#  Usage:  femm-lua.sh [-h] [-p PORT] [script.lua [args...]]
#            -h, --headless   drive the headless server (no GUI window)
#            -p, --port PORT  use TCP port PORT instead of the default 48015
#            (no script)      start an interactive Lua prompt
#
#  -p is what makes two of these run at the same time: femm-qt serves one
#  client per port, so simultaneous runs sharing 48015 collide -- one wins and
#  the other is turned away. Give each its own port.
#
#  Self-contained: the bundled Lua 4 interpreter (lua4), femm.lua, and the
#  femm-qt executables all live in this same folder.
# ====================================================================
set -u

here="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
FEMMLUA="$here/femm.lua"

# lua4 sits beside this script in a source tree, but inside femm_gui.app in a
# macOS distribution: it is the only BINARY among the client-side files, so it
# ships inside the bundle, where the app's signature and notarisation ticket
# already cover it. A loose Mach-O beside a signed .app carries its own
# quarantine bit and cannot have a ticket stapled to it.
LUA4="$here/lua4"
if [ ! -x "$LUA4" ] && [ -x "$here/femm_gui.app/Contents/MacOS/lua4" ]; then
    LUA4="$here/femm_gui.app/Contents/MacOS/lua4"
fi

[ -x "$LUA4" ]    || { echo "femm-lua: cannot find lua4 beside this script or inside femm_gui.app, looked in: $here" >&2; exit 1; }
[ -f "$FEMMLUA" ] || { echo "femm-lua: cannot find femm.lua next to this script: $FEMMLUA" >&2; exit 1; }

# FEMM_DIR: the bundled femm_gui / femm_headless live in this folder, so always
# point at it. Doing so unconditionally means an inherited FEMM_DIR (e.g. from
# another FEMM install) can't redirect us to a server that lacks these
# binaries. Exported only to this script's children, not your shell.
FEMM_DIR="$here"
export FEMM_DIR

mode=0
port=""
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--headless) mode=1; shift ;;
        -p|--port)
            [ $# -ge 2 ] || { echo "femm-lua: $1 needs a port number" >&2; exit 1; }
            port="$2"
            case "$port" in
                ''|*[!0-9]*) echo "femm-lua: not a port number: $port" >&2; exit 1 ;;
            esac
            shift 2 ;;
        -\?|--help)
            echo "Usage: $(basename "$0") [-h] [-p PORT] [script.lua [args...]]"
            echo "   -h, --headless   use the headless server instead of the GUI"
            echo "   -p, --port PORT  use TCP port PORT instead of the default 48015"
            echo "   (no script)      start an interactive Lua prompt connected to FEMM"
            exit 0 ;;
        *) break ;;
    esac
done

# _femm_port must be set BEFORE openfemm(), which is why the port cannot be
# chosen from inside the script: this preamble has already connected by the
# time the script's first line runs.
setport=""
[ -n "$port" ] && setport="_femm_port=$port; "

# openfemm(0) = latch/spawn GUI, openfemm(1) = latch/spawn the headless server
preamble="dofile([[$FEMMLUA]]); ${setport}openfemm($mode)"

if [ $# -eq 0 ]; then
    # no script -> interactive REPL. -v prints the Lua banner first; the
    # bare-interpreter banner is skipped on the explicit -i path.
    exec "$LUA4" -v -e "$preamble" -i
else
    exec "$LUA4" -e "$preamble" -f "$@"
fi
