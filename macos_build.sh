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
APP_RES="$APP/Contents/Resources"

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
# on the solvers being co-located here (exeDir()).
#
# lua4 is here for a different reason, and it cuts against where it logically
# belongs. Every binding on every platform lives OUTSIDE the program directory
# -- the Windows installer puts octavefemm under app/mfiles, mathfemm under
# app/mathfemm, juliafemm under app/juliafemm (packaging/femm-qt.iss) -- and
# lua4 is a CLIENT interpreter that nothing inside the bundle ever invokes.
#
# But it is also the only BINARY among the client-side pieces. femm.lua,
# openfemm.m, juliafemm.jl, scifemm.sci and femm.py are plain text and
# Gatekeeper ignores them; a loose Mach-O shipped beside the .app is a separate
# object with its own quarantine bit, and a notarisation ticket cannot be
# stapled to a bare executable -- only to a .app, .dmg or .pkg. Downloaded
# unsigned, it is refused outright from Terminal. Inside the bundle it is
# covered by the same codesign --deep and the same stapled ticket as everything
# else, for free.
#
# So the binary ships inside and its text companions ship outside, and
# femm-lua.sh looks in both places (see luafemm/femm-lua.sh).
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

# ---- 3. Material libraries (read-only seeds) ------------------------
# These four are the only files femm-qt WRITES beside itself, and the write is
# easy to trigger: the material-library dialog saves on every OK, edited or not.
# So they cannot live in Contents/MacOS/, which codesign seals as nested code --
# the first OK would invalidate the signature of a distributed .app.
#
# They go in Contents/Resources/ as read-only ORIGINALS. At runtime
# libfemm/materialLibPath() copies each one, on first use, to
# ~/Library/Application Support/FEMM/ and reads and writes it there; the bundle
# is never written to. See docs/macos-appsupport-material-libs.md.
#
# Only in the bundle: nothing resolves a library path from bin/ any more (see
# step 3c). No chmod either -- the seeds are meant to stay read-only, and
# materialLibPath() is what makes the USER's copy writable.
#
# Still done AFTER macdeployqt, which chokes on non-Mach-O files it finds.
mkdir -p "$APP_RES"
cp ../matlibs/*.dat "$APP_RES/"

# Lua 4 legacy client assets -- the TEXT half. These stay outside the bundle
# with every other binding's client files; only the lua4 binary goes inside, for
# signing (step 2). femm-lua.sh finds the interpreter and the engine wherever
# they are: beside itself in a source tree, or inside femm_gui.app in a
# distribution. Matches the other platforms' build scripts, where everything is
# beside everything (linux_build.sh / freebsd_build.sh).
cp ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh

# ---- 3b2. The manual ------------------------------------------------
# Help > Manual (F1) opens applicationDirPath()/manual.pdf, which is where the
# Windows installer and the Linux build script put it -- beside the executable.
# A bundle cannot: codesign seals Contents/MacOS/ as nested code, and documents
# belong in Resources/ anyway. mainwindow.cpp falls back to ../Resources/ on
# macOS. Without this the Help menu has never worked in a macOS build.
if [ -f ../manual/manual.pdf ]; then
    cp ../manual/manual.pdf "$APP_RES/"
else
    echo "NOTE: ../manual/manual.pdf not found -- Help > Manual will be dead in"
    echo "      this bundle. Build the manual first if that matters."
fi

# ---- 3d. Examples, bindings and the remaining documents -------------
# A macOS distribution is the .app and nothing else -- there is no {app}
# directory to hold sibling folders the way the Windows installer has. So the
# read-only companions go where read-only companions belong on this platform,
# Contents/Resources, and users reach them at
#   /Applications/femm_gui.app/Contents/Resources/...
# which is how Julia and Mathematica ship their equivalents.
#
# The examples here are read-only MASTERS. They cannot be used in place:
# analysis writes the .fem back and drops an .ans beside it, and an example
# SCRIPT is worse -- open("model.fem") then mi_saveas("temp.fem"), both relative
# to the script's own directory, which is where -run and the GUI's script
# toolbar both chdir. Inside the bundle the saveas fails outright.
#
# So packaging/macos_dmg.sh copies this directory back out as a loose
# `Examples` folder in the DMG, which the user drags somewhere writable -- the
# same arrangement as c:\femm-qt\examples on Windows. Keeping the masters here
# as well costs half a megabyte and means the examples survive throwing the DMG
# away.
#
# The staging itself is packaging/stage_extras.sh, shared with the install
# step of linux_build.sh and freebsd_build.sh. It reads the example list out
# of packaging/femm-qt.iss -- which already curates exactly which files ship,
# and matters because ../examples in a working tree also holds solver output
# and scratch models that must not go out.
#
# Shared rather than duplicated because the duplicate had already drifted: the
# four per-binding markdown guides (pyfemm.md, octavefemm.md, femm-lua.md and
# luafemm's README) were added to the shared copy and this one silently went on
# shipping without them. Verified equivalent before the switch -- the inline
# version and the shared one staged 710 byte-identical files, differing only by
# those four documents.
#
# This script runs with cwd = build/, so the root is one level up.
bash "../packaging/stage_extras.sh" "$APP_RES" "$(cd .. && pwd)"

# ---- 3c. Remove the loose copies of everything now in the bundle -----
# CMake links these into bin/ and step 2 copied them into Contents/MacOS/.
# Leaving both behind is not just clutter: they are two exeDir()s, and only the
# bundle's has the solvers, the material-library seeds and the python assets
# beside it. A stray run of bin/femm_headless would look like femm-qt and
# behave differently.
#
# Nothing needs them now. The regression suite drives the bundle (e5ad776) and
# all six language bindings resolve <dir>/femm_gui.app/Contents/MacOS/<exe>
# ahead of the bare form, so they reach the bundle even with FEMM_DIR pointing
# here (8302faa).
#
# lua4 goes too: step 2 copies it into the bundle so it is inside the signature,
# and femm-lua.sh finds it there. Leaving a loose copy would put an unsigned
# Mach-O beside a signed .app, which is the situation bundling it avoids.
#
# bin/python and bin/pybridge-worker also stay: CMake stages them there and
# step 3b copies them in, so bin/ is their source, not a duplicate.
#
# A bare `ninja` recreates the binaries here without reassembling the bundle.
# That is the point of the staleness check in testing/_verify_all.sh.
for stale in fkn belasolv hsolv csolv femm_headless lua4; do
    rm -f "bin/$stale"
done
rm -f bin/*.dat

# ---- 3b. Python scripting assets ------------------------------------
# init_py() puts exeDir()+"python" on sys.path and pybridge_host.cpp looks for
# its CPython worker at exeDir()+"pybridge-worker/", so in a DISTRIBUTED bundle
# both have to sit in Contents/MacOS beside the executables.  CMake stages them
# into build/bin/, which is beside the .app rather than inside it, so without
# this step an installed app has no datetime and no matplotlib bridge -- while
# a developer build has both, which is exactly the kind of difference that
# reaches a user.
#
# Copied, not symlinked the way the .dat files above are: those are WRITTEN by
# the material editor, so a second copy would drift.  These are read-only at
# runtime and CMake rewrites them on every build, so a copy is the simpler
# thing that cannot go stale.  Done here, after macdeployqt, so it never sees
# a .py file in MacOS/ and tries to read it as a Mach-O binary.
for pydir in python pybridge-worker; do
    if [ -d "bin/$pydir" ]; then
        rm -rf "$APP_BIN/$pydir"
        cp -R "bin/$pydir" "$APP_BIN/$pydir"
        # A CPython process that reads python/ leaves .pyc files of MicroPython
        # source behind; they are junk, and inside a signed bundle they are junk
        # that has to be sealed.
        find "$APP_BIN/$pydir" -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
    else
        echo "WARNING: bin/$pydir not found -- datetime and the matplotlib bridge will be MISSING from the bundle"
    fi
done

# ---- 4. License and notice files -------------------------------------
# A binary distribution has to carry the license text for everything it ships,
# and the bundle previously carried none. Contents/Resources/ is the idiomatic
# home for them, and the destination names match the Windows installer's
# (packaging/femm-qt.iss) so both distributions present the same set.
#
# Placed AFTER macdeployqt, which inspects what it finds and chokes on
# non-Mach-O files, and BEFORE the re-sign below, so the notices land inside the
# signature instead of invalidating it.
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
copy_license ../third_party/bdwgc/LICENSE       LICENSE-bdwgc.txt
copy_license ../QT-THIRD-PARTY-NOTICES.txt      QT-THIRD-PARTY-NOTICES.txt
copy_license ../pylib/vendor/LICENSE.micropython-lib   LICENSE-micropython-lib.txt
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
