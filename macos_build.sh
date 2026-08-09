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
# and re-sign.

APP="bin/femm_gui.app"
APP_BIN="$APP/Contents/MacOS"
APP_FRAMEWORKS="$APP/Contents/Frameworks"

# ---- 1. Bundle Qt frameworks and dependent dylibs --------------------
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
# on the solvers and .dat files being co-located here (exeDir()).
BUNDLE_EXES="fkn belasolv hsolv csolv femm_headless lua4"
for exe in $BUNDLE_EXES; do
    if [ -f "bin/$exe" ]; then
        cp "bin/$exe" "$APP_BIN/"
    fi
done

# ---- 2b. Prune what macdeployqt bundled but nothing uses -------------
# Runs AFTER the solvers are copied in, so the reference scan below covers every
# executable the bundle will actually contain, not just femm_gui.
# macdeployqt copies every plugin Qt ships and then the transitive closure of
# their dependencies. That drags in QtVirtualKeyboard -- an on-screen keyboard
# for embedded/kiosk systems with no OS input method -- which links the whole
# QML stack, which in turn pulls QtNetwork and OpenSSL. femm-qt is a QtWidgets
# application and uses none of it; macOS supplies its own Accessibility
# Keyboard, and the Windows installer has never shipped an input-context plugin.
#
# Dropping OpenSSL matters beyond bundle size: shipping it means owning its CVE
# stream for the life of the product.
#
# The framework list below was derived by removing the plugin and iterating to a
# fixpoint over "what does nothing reference any more". Rather than trust it
# forever, each entry is re-checked here against the bundle as it actually is,
# and kept (with a warning) if anything still points at it -- so a future Qt
# rearranging its dependencies degrades to a fatter bundle, never a broken one.
PRUNE_PLUGINS="platforminputcontexts"
PRUNE_FRAMEWORKS="QtQuick.framework QtOpenGL.framework QtQmlMeta.framework \
QtQmlModels.framework QtQmlWorkerScript.framework QtQml.framework \
QtNetwork.framework libbrotlidec.1.dylib libbrotlicommon.1.dylib \
libssl.3.dylib libcrypto.3.dylib"

for p in $PRUNE_PLUGINS; do
    rm -rf "$APP/Contents/PlugIns/$p"
done

# Collect every install-name referenced by what we are KEEPING. Files inside a
# prune candidate are skipped, so the candidates cannot vouch for each other.
# (A self-reference is harmless here: a pruned item is never in the keep set.)
PRUNE_REFS=$(
    for f in "$APP_BIN"/* "$APP"/Contents/PlugIns/*/*.dylib \
             "$APP_FRAMEWORKS"/*.dylib "$APP_FRAMEWORKS"/*.framework/Versions/*/*; do
        [ -f "$f" ] || continue
        skip=0
        for p in $PRUNE_FRAMEWORKS; do
            # Plain string tests, not `case`: a `case` whose pattern ends in `)`
            # placed inside this $(...) trips a parser bug in macOS's system bash
            # 3.2 ("syntax error near unexpected token ;;"). The prefix test is
            # true when $f lives inside the framework dir we are pruning.
            if [ "$f" = "$APP_FRAMEWORKS/$p" ] || \
               [ "${f#"$APP_FRAMEWORKS/$p"/}" != "$f" ]; then
                skip=1; break
            fi
        done
        [ "$skip" -eq 1 ] && continue
        otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}'
    done | sed -e 's#.*/Frameworks/##' -e 's#^@rpath/##' -e 's#^@loader_path/##' \
         | awk -F/ '{print $1}' | sort -u
)

for fw in $PRUNE_FRAMEWORKS; do
    [ -e "$APP_FRAMEWORKS/$fw" ] || continue
    if printf '%s\n' "$PRUNE_REFS" | grep -qx "$fw"; then
        echo "NOTE: $fw is still referenced — keeping it (Qt's dependency set has changed)"
    else
        rm -rf "$APP_FRAMEWORKS/$fw"
    fi
done

# ---- 3. Material libraries (data files, not code) -------------------
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

# ---- 4. License and notice files -------------------------------------
# A binary distribution has to carry the license text for everything it ships,
# and the bundle previously carried none. Contents/Resources/ is the idiomatic
# home for them, and the destination names match the Windows installer's
# (packaging/femm-qt.iss) so both distributions present the same set.
#
# Placed AFTER macdeployqt, which inspects what it finds and chokes on
# non-Mach-O files, and BEFORE the re-sign below, so the notices land inside the
# signature instead of invalidating it.
APP_RES="$APP/Contents/Resources"
mkdir -p "$APP_RES"

copy_license() {        # $1 = source path, $2 = name inside Resources/
    if [ -f "$1" ]; then
        cp "$1" "$APP_RES/$2"
        chmod u+w "$APP_RES/$2"     # sources may be read-only
    else
        echo "WARNING: license file not found, NOT bundled: $1"
    fi
}

copy_license ../LICENSE-MIT                      LICENSE-MIT
copy_license ../LICENSE-GPL                      LICENSE-GPL
copy_license ../NOTICE-Qt.txt                    NOTICE-Qt.txt
copy_license ../LICENSE-melihaltun.txt           LICENSE-melihaltun.txt
copy_license ../lua/liblua/COPYRIGHT             Lua-COPYRIGHT.txt
copy_license ../lua/luasocket-1.0/COPYRIGHT      LuaSocket-COPYRIGHT.txt
copy_license ../micropython/LICENSE.micropython  LICENSE-MicroPython.txt
copy_license ../micropython/ulab/LICENSE         LICENSE-ulab.txt
# macOS-only: macdeployqt copies Qt's transitive Homebrew dependencies into
# Contents/Frameworks/, so the bundle redistributes them. Their notices are
# aggregated into THIRD-PARTY-NOTICES.txt by
# packaging/gen-third-party-notices.sh. Windows covers the equivalent set via
# the Inno Setup list, which is why this file is macOS-only.
copy_license ../THIRD-PARTY-NOTICES.txt          THIRD-PARTY-NOTICES.txt

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
