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

# ---- Optional: install instead of building --------------------------
# `install [prefix]` puts an ALREADY BUILT tree into the user's home and
# exits. It deliberately does not build first: you install once and build
# many times. packaging/unix_install.sh explains why the install goes to
# $HOME rather than /usr/local.
if [ "${1:-}" = "install" ]; then
    shift
    exec packaging/unix_install.sh "$PWD" "$PWD/build" "${1:-$HOME/.local}"
fi

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

# ---- Copy the manual -------------------------------------------------
# Help > Manual (F1) opens applicationDirPath()/manual.pdf -- beside the
# executable. Without this the Help menu reports "Could not find manual.pdf in
# the application directory", which it did on this platform until 2026-08-22.
# The Windows installer has always placed it in {app}\bin for the same reason.
if [ -f ../manual/manual.pdf ]; then
    cp -u ../manual/manual.pdf bin/
else
    echo "NOTE: ../manual/manual.pdf not found -- Help > Manual will not work."
    echo "      Build the manual first if that matters."
fi

# ---- Copy the Lua 4 legacy client assets (lua4 built into bin/) ------
cp -u ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh

echo
echo "Build complete.  Executables are in build/bin/"
