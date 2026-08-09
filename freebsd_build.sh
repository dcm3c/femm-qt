#!/usr/bin/env bash
# ======================================================================
#  freebsd_build.sh - configure and build femm-qt on FreeBSD
#
#  Usage:  ./freebsd_build.sh [clean]
#
#  Run from anywhere; builds into femm-qt/build/.  Pass "clean" to delete
#  the build directory first.  Qt 6 is found from the packages under
#  ${LOCALBASE:-/usr/local}.
#
#  This is the Linux build script adapted for FreeBSD's base system:
#    - the default C++ compiler is clang (cc/c++), not g++
#    - base cp(1) has no GNU '-u' flag, so the data files are copied plain
#    - Qt 6 lives under LOCALBASE (/usr/local), not /usr
#
#  Prerequisites (as root):
#    pkg install cmake ninja qt6
#  bash itself comes from:  pkg install bash
# ======================================================================
set -euo pipefail

# ---- Work in this script's own folder (the femm-qt source root) ------
# FreeBSD 12+ has 'readlink -f'; fall back to the raw path just in case.
SELF="${BASH_SOURCE[0]}"
cd "$(dirname "$(readlink -f "$SELF" 2>/dev/null || echo "$SELF")")"

# ---- Handle the optional 'clean' argument ----------------------------
if [ "${1:-}" = "clean" ]; then
    echo "Removing build directory..."
    rm -rf build
fi

# ---- Check build tools -----------------------------------------------
# FreeBSD's base toolchain is clang, exposed as cc/c++.  Accept any of the
# common C++ driver names so a stock system (no g++) still builds.
missing=""
for tool in cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if ! command -v c++ >/dev/null 2>&1 \
    && ! command -v clang++ >/dev/null 2>&1 \
    && ! command -v g++ >/dev/null 2>&1; then
    missing="$missing c++"
fi
if [ -n "$missing" ]; then
    echo "ERROR: missing build tools:$missing"
    echo "Install them with:"
    echo "  pkg install cmake ninja"
    echo "(a C++ compiler ships in the FreeBSD base system as cc/c++)"
    exit 1
fi

# ---- Configure -------------------------------------------------------
# Qt 6 and its CMake config files install under LOCALBASE (/usr/local).
# Point CMAKE_PREFIX_PATH there so find_package(Qt6) resolves without a
# hand-set Qt6_DIR.  Override LOCALBASE if your packages
# live elsewhere.
LOCALBASE="${LOCALBASE:-/usr/local}"

mkdir -p build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$LOCALBASE"

# ---- Build -----------------------------------------------------------
ninja

# ---- Copy material libraries so solvers find them in bin/ ------------
# Plain cp: FreeBSD's base cp(1) has no GNU '-u' option.  Make the copies
# writable in case the source files came in read-only.
cp ../matlibs/*.dat bin/
chmod u+w bin/*.dat

# ---- Copy the Lua 4 legacy client assets (lua4 built into bin/) ------
cp ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh

echo
echo "Build complete.  Executables are in build/bin/"
