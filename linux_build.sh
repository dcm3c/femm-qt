#!/usr/bin/env bash
# ======================================================================
#  linux_build.sh - configure and build femm-qt on Linux
#
#  Usage:  ./linux_build.sh [clean]
#
#  Run from anywhere; builds into femm-qt/build/.  Pass "clean" to delete
#  the build directory first.  Qt 6 is found automatically from the
#  system packages (see docs/linux-build.pdf for prerequisites).
# ======================================================================
set -euo pipefail

# ---- Work in this script's own folder (the femm-qt source root) ------
cd "$(dirname "$(readlink -f "$0")")"

# ---- Handle the optional 'clean' argument ----------------------------
if [ "${1:-}" = "clean" ]; then
    echo "Removing build directory..."
    rm -rf build
fi

# ---- Check build tools -----------------------------------------------
missing=""
for tool in cmake ninja g++; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    echo "ERROR: missing build tools:$missing"
    echo "Install them with:"
    echo "  sudo apt install build-essential cmake ninja-build"
    exit 1
fi

# ---- Configure -------------------------------------------------------
mkdir -p build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release

# ---- Build -----------------------------------------------------------
ninja

# ---- Copy material libraries so solvers find them in bin/ ------------
cp -u ../matlibs/*.dat bin/

# ---- Copy the Lua 4 legacy client assets (lua4 built into bin/) ------
cp -u ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh

echo
echo "Build complete.  Executables are in build/bin/"
