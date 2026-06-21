#!/usr/bin/env bash
# ======================================================================
#  macos_build.sh - configure and build femm-qt on macOS
#
#  Usage:  ./macos_build.sh [clean]
#
#  Run from anywhere; builds into femm-qt/build/.  Pass "clean" to delete
#  the build directory first.  Qt 6 is located via Homebrew (see
#  docs/macos-build.pdf for prerequisites).
# ======================================================================
set -euo pipefail

# ---- Work in this script's own folder (the femm-qt source root) ------
cd "$(dirname "$0")"

# ---- Handle the optional 'clean' argument ----------------------------
if [ "${1:-}" = "clean" ]; then
    echo "Removing build directory..."
    rm -rf build
fi

# ---- Check build tools -----------------------------------------------
missing=""
for tool in cmake ninja; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    echo "ERROR: missing build tools:$missing"
    echo "Install them with:  brew install cmake ninja"
    exit 1
fi

# ---- Locate Qt 6 via Homebrew (try qt@6, then qt) --------------------
QT_PREFIX=""
for formula in qt@6 qt; do
    prefix=$(brew --prefix "$formula" 2>/dev/null || true)
    if [ -n "$prefix" ] && [ -d "$prefix" ]; then
        QT_PREFIX="$prefix"
        break
    fi
done
if [ -z "$QT_PREFIX" ]; then
    echo "ERROR: Qt 6 not found.  Install it with:  brew install qt@6"
    exit 1
fi
echo "Qt: $QT_PREFIX"

# ---- Configure -------------------------------------------------------
mkdir -p build
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"

# ---- Build -----------------------------------------------------------
ninja

# ---- Assemble the app bundle -----------------------------------------
# The GUI executable lives in femm_gui.app/Contents/MacOS/.  At runtime the
# GUI finds its solvers and material libraries alongside itself via exeDir()
# (see libfemm/script_callbacks.cpp), so both must end up in Contents/MacOS/.
#
# Order matters.  macdeployqt must run FIRST, on the bare bundle that holds
# only the GUI executable: it scans (and runs `otool` on) everything it finds
# in MacOS/ and Frameworks/, then code-signs the result.  If the solvers and
# .dat files are already present it chokes on them ("not an object file") and
# leaves a broken signature.  So: deploy Qt first, then drop in the extras,
# repoint the solvers at the bundled libomp, and re-sign.

APP="bin/femm_gui.app"
APP_BIN="$APP/Contents/MacOS"
APP_FRAMEWORKS="$APP/Contents/Frameworks"

# ---- 1. Bundle Qt frameworks and dependent dylibs --------------------
# macdeployqt also bundles libomp.dylib into Contents/Frameworks/ because the
# GUI links it, and rewrites the GUI's reference to @executable_path/../
# Frameworks/libomp.dylib.  No manual libomp copy is needed.
#
# macdeployqt is noisy: it prints a wall of "Cannot resolve rpath" and
# "codesign verification error" lines for the Homebrew dylibs it copies, yet
# still exits 0.  Those are harmless here — step 5 re-signs the bundle and we
# verify the result — but they alarm people.  So capture macdeployqt's output
# in a log, show it only if macdeployqt actually fails (non-zero exit) or if
# the final signature check fails (see step 5).
DEPLOY_LOG="$(mktemp -t macdeployqt.XXXXXX)"
trap 'rm -f "$DEPLOY_LOG"' EXIT

DEPLOYQT="$QT_PREFIX/bin/macdeployqt"
if [ -x "$DEPLOYQT" ]; then
    echo "Bundling Qt frameworks (macdeployqt)..."
    if "$DEPLOYQT" "$APP" >"$DEPLOY_LOG" 2>&1; then
        echo "  Qt frameworks bundled."
    else
        echo "ERROR: macdeployqt failed.  Its output:"
        cat "$DEPLOY_LOG"
        exit 1
    fi
else
    echo "WARNING: macdeployqt not found — Qt libraries not bundled."
    echo "The app will only run on machines with Qt installed via Homebrew."
fi

# ---- 2. Copy solver and headless executables into the bundle ---------
# The four solvers are launched by the GUI via exeDir().  femm_headless is the
# no-GUI front end that the language bindings (OctaveFEMM, pyFEMM, MathFEMM,
# JuliaFEMM) start when asked for a headless session — they locate it via
# $FEMM_DIR/femm_headless, so it must sit beside femm_gui in MacOS/.  Both rely
# on the solvers, .dat files, and libomp being co-located here (exeDir()).
BUNDLE_EXES="fkn belasolv hsolv csolv femm_headless lua4"
for exe in $BUNDLE_EXES; do
    if [ -f "bin/$exe" ]; then
        cp "bin/$exe" "$APP_BIN/"
    fi
done

# ---- 3. Repoint the bundled executables at the bundled libomp --------
# Each links the OpenMP runtime via its absolute Homebrew path, which does not
# exist on machines without Homebrew.  Redirect that reference to the
# libomp.dylib that macdeployqt placed in Contents/Frameworks/.  (They live in
# MacOS/, so @executable_path/../Frameworks is the right relative path.)
if [ -f "$APP_FRAMEWORKS/libomp.dylib" ]; then
    for exe in $BUNDLE_EXES; do
        target="$APP_BIN/$exe"
        [ -f "$target" ] || continue
        omp_ref=$(otool -L "$target" | awk '/libomp\.dylib/ {print $1; exit}')
        if [ -n "$omp_ref" ]; then
            install_name_tool -change "$omp_ref" \
                "@executable_path/../Frameworks/libomp.dylib" "$target"
        fi
    done
fi

# ---- 4. Material libraries (data files, not code) -------------------
# Resolved via exeDir() -- from the directory of the running executable -- so
# both binaries must reach them. femm_gui runs from the bundle; femm_headless
# runs from bin/. The material-library editor WRITES these files, so a second
# copy would silently drift; instead keep ONE real copy in the bundle (where
# the GUI reads/writes it, and which a distributed .app must contain) and point
# bin/ at it with relative symlinks. The links live only in the dev bin/, so
# distribution ships just the bundle. Done AFTER macdeployqt so it never treats
# the .dat files as Mach-O binaries.
cp ../matlibs/*.dat "$APP_BIN/"
chmod u+w "$APP_BIN"/*.dat   # Homebrew/source files may be read-only
for dat in "$APP_BIN"/*.dat; do
    ln -sf "femm_gui.app/Contents/MacOS/$(basename "$dat")" "bin/$(basename "$dat")"
done

# Lua 4 legacy client assets. These live in bin/ -- NOT the app bundle --
# next to the lua4 interpreter and femm_headless that CMake builds there.
# femm-lua.sh is self-contained: it sets FEMM_DIR to its own folder and loads
# lua4 + femm.lua and finds the FEMM executables (including femm_gui.app) from
# there, so it must sit beside them in bin/, matching the other platforms'
# build scripts (linux_build.sh / freebsd_build.sh).
cp ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh

# ---- 5. Re-sign the whole bundle --------------------------------------
# Two things broke the signature macdeployqt applied: adding/rewriting the
# solvers, and macdeployqt's own habit of mangling the signatures of the
# Homebrew dylibs it copies (the "codesign verification error" it prints).
# A single deep ad-hoc re-sign re-seals every nested binary bottom-up and
# fixes both.  No Developer ID is needed for local use — an ad-hoc signature
# ("-") satisfies Gatekeeper's "code has not been modified" check.
if command -v codesign >/dev/null 2>&1; then
    echo "Code-signing the bundle (ad-hoc)..."
    # codesign prints "replacing existing signature" on rebuilds — benign noise.
    # Hide it (and show it only if signing actually fails).
    if ! codesign --force --deep --sign - "$APP" >>"$DEPLOY_LOG" 2>&1; then
        echo "ERROR: code signing failed.  Output:"
        cat "$DEPLOY_LOG"
        exit 1
    fi
    if codesign --verify --deep --strict "$APP" 2>/dev/null; then
        echo "  Signature verified."
    else
        echo "WARNING: bundle signature did not verify."
        # The macdeployqt step is the usual culprit, so surface its (otherwise
        # hidden) output now that something has actually gone wrong.
        if [ -s "$DEPLOY_LOG" ]; then
            echo "--- macdeployqt output ---"
            cat "$DEPLOY_LOG"
            echo "--- end macdeployqt output ---"
        fi
    fi
fi

echo
echo "Build complete.  App bundle: build/bin/femm_gui.app"
