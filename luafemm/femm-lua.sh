#!/usr/bin/env bash
# ====================================================================
#  femm-lua.sh - run a legacy FEMM Lua 4 script (or an interactive Lua
#                prompt) against femm-qt.
#
#  Usage:  femm-lua.sh [-h] [script.lua [args...]]
#            -h, --headless   drive the headless server (no GUI window)
#            (no script)      start an interactive Lua prompt
#
#  Self-contained: the bundled Lua 4 interpreter (lua4), femm.lua, and the
#  femm-qt executables all live in this same folder.
# ====================================================================
set -u

here="$(cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" && pwd)"
LUA4="$here/lua4"
FEMMLUA="$here/femm.lua"

[ -x "$LUA4" ]    || { echo "femm-lua: cannot find lua4 next to this script: $LUA4" >&2; exit 1; }
[ -f "$FEMMLUA" ] || { echo "femm-lua: cannot find femm.lua next to this script: $FEMMLUA" >&2; exit 1; }

# FEMM_DIR: the bundled femm_gui / femm_headless live in this folder, so always
# point at it. Doing so unconditionally means an inherited FEMM_DIR (e.g. from
# another FEMM install) can't redirect us to a server that lacks these
# binaries. Exported only to this script's children, not your shell.
FEMM_DIR="$here"
export FEMM_DIR

mode=0
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--headless) mode=1; shift ;;
        -\?|--help)
            echo "Usage: $(basename "$0") [-h] [script.lua [args...]]"
            echo "   -h, --headless   use the headless server instead of the GUI"
            echo "   (no script)      start an interactive Lua prompt connected to FEMM"
            exit 0 ;;
        *) break ;;
    esac
done

# openfemm(0) = latch/spawn GUI, openfemm(1) = latch/spawn the headless server
preamble="dofile([[$FEMMLUA]]); openfemm($mode)"

if [ $# -eq 0 ]; then
    # no script -> interactive REPL. -v prints the Lua banner first; the
    # bare-interpreter banner is skipped on the explicit -i path.
    exec "$LUA4" -v -e "$preamble" -i
else
    exec "$LUA4" -e "$preamble" -f "$@"
fi
