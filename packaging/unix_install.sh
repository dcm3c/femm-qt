#!/usr/bin/env bash
# ======================================================================
#  unix_install.sh - install a built femm-qt into the user's home
#
#  Not run directly; linux_build.sh and freebsd_build.sh call it for
#  their `install` argument.  Usage:
#
#      packaging/unix_install.sh <femm-qt-root> <build-dir> [prefix]
#
#  Installs what is already in <build-dir>/bin.  It does NOT build:
#  you install once and build many times, and an install that forced a
#  rebuild would be a nuisance.  It fails clearly if there is nothing to
#  install.
#
#  ---- Why the user's home, and not /usr/local ----------------------
#
#  femm-qt resolves almost everything relative to the running executable
#  (exeDir() in libfemm/script_callbacks.cpp): the solvers, the python/
#  scripting assets, the PyBridge worker, the manual, and the four
#  material libraries.  A self-contained directory therefore installs
#  with no code changes at all, which is exactly what the Windows
#  installer does -- c:\femm-qt, PrivilegesRequired=lowest, never
#  Program Files.
#
#  It also keeps the material libraries and the examples WRITABLE, which
#  they have to be: the material editor saves the libraries, and every
#  example script opens a model and immediately saves it under another
#  name.  Under /usr/local both would be read-only, and each would need
#  its own copy-into-the-user's-home resolver first.
#
#  An FHS install (/usr/local/bin plus /usr/local/share/femm) is a
#  reasonable thing to want, and it is what a distribution packager
#  would need -- but it needs those resolvers, and sudo.  This is the
#  smaller thing that works today.
# ======================================================================
set -eu

ROOT="${1:?usage: unix_install.sh <femm-qt-root> <build-dir> [prefix]}"
BUILD="${2:?usage: unix_install.sh <femm-qt-root> <build-dir> [prefix]}"
PREFIX="${3:-$HOME/.local}"

BIN="$BUILD/bin"
APPDIR="$PREFIX/opt/femm-qt"

if [ ! -x "$BIN/femm_gui" ] && [ ! -x "$BIN/femm_gui.exe" ]; then
    echo "ERROR: no femm_gui in $BIN -- build first."
    exit 1
fi
for needed in femm_headless fkn belasolv hsolv csolv; do
    if [ ! -e "$BIN/$needed" ]; then
        echo "ERROR: $BIN/$needed is missing -- the build is incomplete."
        exit 1
    fi
done

echo "Installing femm-qt into $APPDIR"

# ---- 1. The program directory ---------------------------------------
# Everything from bin/ verbatim: the executables, the material
# libraries, python/, pybridge-worker/, the Lua client assets and the
# manual. exeDir() then finds all of it exactly as it does in the build
# tree, because the layout is the same.
rm -rf "$APPDIR"
mkdir -p "$APPDIR"
cp -R "$BIN/." "$APPDIR/"

# ---- 2. Examples, bindings and documents ----------------------------
"$ROOT/packaging/stage_extras.sh" "$APPDIR" "$ROOT"

# The examples must stay writable: an example script does
#   open("model.fem")  then  mi_saveas("temp.fem")
# both relative to its own directory.
chmod -R u+w "$APPDIR/examples"

# ---- 3. Somewhere on PATH -------------------------------------------
# ~/.local/bin is on PATH by default on current distributions. Symlinks
# rather than copies, so an upgrade of the program directory is picked
# up without re-linking.
mkdir -p "$PREFIX/bin"
for exe in femm_gui femm_headless; do
    ln -sf "$APPDIR/$exe" "$PREFIX/bin/$exe"
done
[ -f "$APPDIR/femm-lua.sh" ] && ln -sf "$APPDIR/femm-lua.sh" "$PREFIX/bin/femm-lua"

# ---- 4. Desktop entry and icon --------------------------------------
mkdir -p "$PREFIX/share/applications"
mkdir -p "$PREFIX/share/icons/hicolor/256x256/apps"
[ -f "$ROOT/gui/icons/femm.png" ] &&
    cp "$ROOT/gui/icons/femm.png" \
       "$PREFIX/share/icons/hicolor/256x256/apps/femm-qt.png"

cat > "$PREFIX/share/applications/femm-qt.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=FEMM
GenericName=Finite Element Method Magnetics
Comment=2D electromagnetic, electrostatic, heat flow and current flow analysis
Exec=$APPDIR/femm_gui %f
Icon=femm-qt
Terminal=false
Categories=Science;Engineering;Physics;
Keywords=FEM;magnetics;electrostatics;finite element;
DESKTOP

# Harmless if absent, and the entry shows up without it on most desktops.
command -v update-desktop-database >/dev/null 2>&1 &&
    update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

echo
echo "Installed."
echo "  program     $APPDIR"
echo "  on PATH     $PREFIX/bin/femm_gui, femm_headless, femm-lua"
echo "  menu entry  $PREFIX/share/applications/femm-qt.desktop"
echo
echo "For the language bindings, point FEMM_DIR at the program directory:"
echo "  export FEMM_DIR=$APPDIR"
echo
echo "The examples are in $APPDIR/examples and are yours to edit."
echo "To uninstall: rm -rf $APPDIR $PREFIX/share/applications/femm-qt.desktop"
echo "              rm -f $PREFIX/bin/femm_gui $PREFIX/bin/femm_headless $PREFIX/bin/femm-lua"
