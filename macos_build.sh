#!/usr/bin/env bash
# ======================================================================
#  macos_build.sh - configure and build femm-qt on macOS
#
#  Usage:  ./macos_build.sh [clean]
#
#  Run from anywhere; builds into femm-qt/build/.  Pass "clean" to delete
#  the build directory first.  Qt 6 comes from either the official qt.io
#  distribution (~/Qt/<version>/macos) or Homebrew -- see "Locate Qt 6" below
#  for how the choice is made and overridden, and docs/macos-build.pdf for
#  prerequisites.
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

# ---- Signing configuration -------------------------------------------
# Sourced HERE, before cmake, purely so a misconfigured release fails in the
# first second instead of after a full build and a bundle assembly. It reads
# FEMM_SIGN_ID / FEMM_NOTARY_PROFILE and sets the mode; see the header of
# packaging/macos_sign.sh for what they mean and how to create the profile.
. "packaging/macos_sign.sh"

# ---- Locate Qt 6: official distribution or Homebrew ---------------------
#
# Two Qts can build this, and they are not interchangeable in what they
# produce:
#
#   official  qt.io's binaries, installed to ~/Qt/<version>/macos (the Qt
#             online installer, or `aqt install-qt mac desktop <ver> clang_64`).
#             Built against the system frameworks, universal, and with Qt's
#             own minimum macOS -- 14.0 for Qt 6.11. A bundle made from it
#             runs on every macOS from that floor up.
#   homebrew  `brew install qt`. Built for THIS macOS only: every bottle
#             declares the host version as its minimum, and macdeployqt drags
#             ~22 of them (glib, ICU, harfbuzz, freetype, pcre2, dbus ...) into
#             the bundle. Fine for development; a DMG from it will not launch
#             on any older macOS. (Found 2026-10-05: the whole bundle declared
#             minos 26.0 and nobody had noticed.)
#
# So the choice is explicit, printed, and recorded in the bundle:
#
#   FEMM_QT=official|homebrew   which flavour. Default: official if ~/Qt holds
#                               one, otherwise Homebrew.
#   FEMM_QT_PREFIX=<dir>        use exactly this Qt (.../6.11.3/macos, or a
#                               brew prefix); overrides FEMM_QT.
#   FEMM_MACOS_MIN=<version>    deployment target. Default: the Qt's own floor,
#                               read from QtCore itself, so it tracks the Qt
#                               version rather than a number written here.
#
# The deployment target is passed to CMake for every binary we compile AND
# written to Info.plist as LSMinimumSystemVersion, and step 4b refuses a
# bundle in which any Mach-O declares a higher minimum than that -- so "runs
# on macOS N" is something the build measured, not something it hoped.
qt_floor() {   # minimum macOS a Mach-O declares (LC_BUILD_VERSION minos)
    otool -l "$1" 2>/dev/null | awk '/LC_BUILD_VERSION/{f=1} f && /minos/{print $2; exit}'
}
QT_PREFIX=""
QT_FLAVOUR=""
want="${FEMM_QT:-auto}"
if [ -n "${FEMM_QT_PREFIX:-}" ]; then
    QT_PREFIX="$FEMM_QT_PREFIX"
    QT_FLAVOUR="${FEMM_QT:-explicit}"
else
    case "$want" in
        official|homebrew|auto) ;;
        *) echo "ERROR: FEMM_QT must be 'official' or 'homebrew' (got '$want')"; exit 1 ;;
    esac
    if [ "$want" = official ] || [ "$want" = auto ]; then
        # Newest version under ~/Qt. `sort -V` so 6.11.3 beats 6.9.3.
        cand=$(ls -d "$HOME"/Qt/[0-9]*.[0-9]*/macos 2>/dev/null | sort -V | tail -1 || true)
        if [ -n "$cand" ] && [ -x "$cand/bin/macdeployqt" ]; then
            QT_PREFIX="$cand"; QT_FLAVOUR=official
        elif [ "$want" = official ]; then
            echo "ERROR: FEMM_QT=official but no Qt under ~/Qt/<version>/macos."
            echo "       Install one with the Qt online installer, or:"
            echo "         pip install aqtinstall && aqt install-qt mac desktop 6.11.3 clang_64 -O ~/Qt"
            exit 1
        fi
    fi
    if [ -z "$QT_PREFIX" ]; then
        for formula in qt@6 qt; do
            prefix=$(brew --prefix "$formula" 2>/dev/null || true)
            if [ -n "$prefix" ] && [ -d "$prefix" ]; then
                QT_PREFIX="$prefix"; QT_FLAVOUR=homebrew
                break
            fi
        done
    fi
fi
if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX/lib/QtCore.framework" ]; then
    echo "ERROR: Qt 6 not found.  Either:"
    echo "         pip install aqtinstall && aqt install-qt mac desktop 6.11.3 clang_64 -O ~/Qt"
    echo "       or:  brew install qt"
    exit 1
fi
QT_VERSION=$("$QT_PREFIX/bin/qmake" -query QT_VERSION 2>/dev/null || echo "?")
QT_FLOOR=$(qt_floor "$QT_PREFIX/lib/QtCore.framework/Versions/A/QtCore")
if [ -z "$QT_FLOOR" ]; then
    echo "ERROR: could not read a minimum macOS from $QT_PREFIX/lib/QtCore.framework; set FEMM_MACOS_MIN"
    exit 1
fi
# Our own floor, independent of Qt: libc++ makes std::to_chars for floating
# point available from macOS 13.3 (common/complex.cpp, tangle/tangle.cpp use
# it), and with a lower deployment target clang refuses to compile them.
# Raise this when the code starts needing something newer; the build will
# say which symbol and which macOS introduced it.
FEMM_CODE_MIN=13.3
# The target is the higher of the two floors, unless overridden.
MACOS_MIN=$(printf '%s\n%s\n' "$QT_FLOOR" "$FEMM_CODE_MIN" | sort -V | tail -1)
MACOS_MIN="${FEMM_MACOS_MIN:-$MACOS_MIN}"
echo "Qt: $QT_FLAVOUR $QT_VERSION at $QT_PREFIX"
echo "    Qt's minimum macOS $QT_FLOOR, femm-qt's $FEMM_CODE_MIN; building for macOS $MACOS_MIN${FEMM_MACOS_MIN:+ (FEMM_MACOS_MIN)}"
if [ "$QT_FLAVOUR" = homebrew ]; then
    echo "    NOTE: a Homebrew-built bundle runs only on this macOS; build with the"
    echo "    official Qt (FEMM_QT=official) for anything you intend to ship."
fi

# ---- Configure -------------------------------------------------------
mkdir -p build
# Switching Qt with a build directory in place is not a reconfigure CMake
# can do: Qt6_DIR and friends are cached from the old Qt. Drop the cache (not
# the objects; ninja recompiles what the new headers touch).
# The deployed bundle has to go too: macdeployqt only ADDS to Contents/
# Frameworks and PlugIns, so the other Qt's frameworks and dylibs would stay
# behind and step 4c would (rightly) refuse the result. Found the first time
# the flavour was switched without `clean`.
if [ -f build/CMakeCache.txt ] && ! grep -q "^Qt6_DIR:PATH=$QT_PREFIX/" build/CMakeCache.txt; then
    echo "Qt changed since the last configure -- resetting the CMake cache and the bundle."
    rm -rf build/CMakeCache.txt build/CMakeFiles build/bin/femm_gui.app   # APP is defined later; literal on purpose
fi
cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
         -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" -DFEMM_MACOS_MIN="$MACOS_MIN"

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
# "codesign verification error" lines for the dylibs it copies, yet
# still exits 0.  Those are harmless here — step 5 re-signs the bundle and we
# verify the result — but they alarm people.  So capture macdeployqt's output
# in a log, show it only if macdeployqt actually fails (non-zero exit) or if
# the final signature check fails (see step 5).
DEPLOY_LOG="$(mktemp -t macdeployqt.XXXXXX)"
trap 'rm -f "$DEPLOY_LOG"' EXIT

# ---- Does everything in the bundle point at something that is there? --
# Prints one line per unresolved dependency, and nothing at all when the
# bundle is closed. Used twice: to decide whether macdeployqt needs another
# pass (just below), and as a gate before signing (step 4b).
#
# All the relocatable load-command forms are checked, not just one. An
# earlier version of this audit looked at @rpath alone and pronounced a
# bundle clean while it held three broken @executable_path references --
# which is why this is a function with a fixed definition rather than
# something retyped each time it is needed.
#
# Absolute paths (/usr/lib, /System/...) are the OS's to satisfy, so they
# are skipped.
bundle_broken_refs() {
    find "$APP/Contents" -type f -print0 2>/dev/null |
    while IFS= read -r -d '' _f; do
        file -b "$_f" 2>/dev/null | grep -q "Mach-O" || continue
        otool -L "$_f" 2>/dev/null | tail -n +2 | awk '{print $1}' |
        while IFS= read -r _r; do
            if [ "${_r#@executable_path/../}" != "$_r" ]; then
                _t="$APP/Contents/${_r#@executable_path/../}"
            elif [ "${_r#@rpath/}" != "$_r" ]; then
                _t="$APP_FRAMEWORKS/${_r#@rpath/}"
            elif [ "${_r#@loader_path/}" != "$_r" ]; then
                _t="${_f%/*}/${_r#@loader_path/}"
            else
                continue
            fi
            [ -e "$_t" ] || echo "  ${_f#"$APP"/} -> $_r"
        done
    done | sort -u
}

DEPLOYQT="$QT_PREFIX/bin/macdeployqt"
if [ -x "$DEPLOYQT" ]; then
    echo "Bundling Qt frameworks (macdeployqt)..."
    if "$DEPLOYQT" "$APP" >"$DEPLOY_LOG" 2>&1; then
        echo "  Qt frameworks bundled."

        # macdeployqt does not reach a fixpoint in one pass over a BARE
        # bundle. It copies the plugins, but the frameworks those plugins
        # pull in -- QtSvg for the svg icon engine, QtPdf for the pdf image
        # format -- are left as @rpath references with nothing behind them.
        # Run it again, with the plugins now in place, and it resolves and
        # copies them.
        #
        # So the FIRST build of a fresh tree produced a bundle with dangling
        # plugin dependencies, and the SECOND silently repaired it. That hid
        # for as long as nobody threw the build directory away; `clean`, or
        # any new checkout, hit it every time.
        #
        # A loop rather than just running it twice, so the condition is "the
        # bundle is closed" and not "we did it the number of times that
        # happened to work once".
        _pass=1
        while [ -n "$(bundle_broken_refs)" ] && [ "$_pass" -lt 4 ]; do
            echo "  unresolved dependencies remain -- macdeployqt pass $((_pass + 1))..."
            "$DEPLOYQT" "$APP" >>"$DEPLOY_LOG" 2>&1 || true
            _pass=$((_pass + 1))
        done
    else
        echo "ERROR: macdeployqt failed.  Its output:"
        cat "$DEPLOY_LOG"
        exit 1
    fi
else
    echo "WARNING: macdeployqt not found — Qt libraries not bundled."
    echo "The app will only run on machines with the same Qt installed."
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
# 2026-09-09: the lists above the fixpoint had gone stale and the step had
# quietly stopped pruning. The bundle grew from ~75 MB to 108 MB, and
# libssl.3.dylib and libcrypto.3.dylib started shipping -- the exact outcome the
# comment above says this step exists to prevent.
#
# Newer Qt deploys QtVirtualKeyboard, which was never listed, so it landed in
# the KEEP set and vouched for QtQuick -> QtQml -> QtNetwork -> OpenSSL under
# the "still referenced" guard. Every one of those IS listed here, and every one
# survived on its say-so. The newer PlugIns/tls and PlugIns/networkinformation
# arrived the same way and vouch for QtNetwork and OpenSSL directly.
#
# femm_gui links Qt6::Widgets and nothing else (gui/CMakeLists.txt), so none of
# it is reachable code; the virtual-keyboard PLUGIN was already being pruned,
# which left its frameworks as dead weight still holding the chain up.
#
# Two list additions unravel it in one pass: plugins are removed before the
# reference scan, and files inside a framework that is itself a prune candidate
# are skipped when collecting references, so candidates cannot vouch for each
# other.
# 2026-09-11: QtPdf and QtSvg added, with the two plugins that pull them.
# Neither is reachable: gui/icons.qrc is 88 .png and 18 .ico and the tree
# contains no .svg at all, and Help > Manual hands the PDF to the system
# viewer via QDesktopServices::openUrl (mainwindow.cpp:2322) rather than
# rendering it. savebitmap writes PNG (built into QtGui) or JPEG, so
# libqjpeg and libjpeg STAY.
#
# QtPdf is why this matters rather than being 4 MB of tidying: QtPdf pulls
# QtNetwork, and QtNetwork hard-links libssl and libcrypto. Leaving QtPdf in
# was enough to hold the whole OpenSSL chain up on its own.
#
# Entries may name a plugin DIRECTORY or a single plugin file -- `rm -rf`
# does not care, and the pdf/svg plugins are one file each inside a
# directory whose other members are wanted.
# The image-format surface of this program is small and provable, so the
# codecs are listed by what is USED rather than trimmed by guesswork:
#
#   reading  gui/icons.qrc, and nothing else -- 88 .png and 18 .ico
#   writing  savebitmap, which is PNG or JPEG (gui/main.cpp:198-201);
#            the only other grab() goes to the clipboard as a QPixmap
#
# PNG is built into QtGui and needs no plugin. So libqico and libqjpeg stay
# (and with them libjpeg), and every other format Qt ships a codec for is
# dead weight -- along with libjasper, libmng, libtiff, libwebp* and
# libsharpyuv, which exist only to serve those codecs.
#
# SVG needs BOTH of its plugins named. libqsvgicon is the icon ENGINE and
# libqsvg is the image FORMAT; pruning only the first leaves the second
# holding QtSvg up, which is exactly what happened on the first attempt.
PRUNE_PLUGINS="platforminputcontexts tls networkinformation \
imageformats/libqpdf.dylib iconengines/libqsvgicon.dylib \
imageformats/libqsvg.dylib imageformats/libqgif.dylib \
imageformats/libqicns.dylib imageformats/libqjp2.dylib \
imageformats/libqmacheif.dylib imageformats/libqmng.dylib \
imageformats/libqtga.dylib imageformats/libqtiff.dylib \
imageformats/libqwbmp.dylib imageformats/libqwebp.dylib"
PRUNE_FRAMEWORKS="QtQuick.framework QtOpenGL.framework QtQmlMeta.framework \
QtQmlModels.framework QtQmlWorkerScript.framework QtQml.framework \
QtVirtualKeyboard.framework QtVirtualKeyboardQml.framework \
QtNetwork.framework QtPdf.framework QtSvg.framework \
libbrotlidec.1.dylib libbrotlicommon.1.dylib \
libssl.3.dylib libcrypto.3.dylib \
libjasper.7.dylib libmng.2.dylib libtiff.6.dylib liblcms2.2.dylib \
libwebp.7.dylib libwebpdemux.2.dylib libwebpmux.3.dylib libsharpyuv.0.dylib"

for p in $PRUNE_PLUGINS; do
    rm -rf "$APP/Contents/PlugIns/$p"
done
# A plugin directory left empty by the line above is litter, and an empty
# directory inside Contents/PlugIns is one more thing for codesign to seal.
#
# rmdir is EXPECTED to fail on most of these -- imageformats still holds a
# dozen plugins -- so the failure is absorbed on the command itself. Writing
# it as `[ -d "$d" ] && rmdir "$d"` puts a failing compound at the end of the
# loop body, which under `set -e` ends the script: that cost one clean build
# to find, and the trace pointed at the rmdir line with the EXIT trap firing
# immediately after.
for d in "$APP"/Contents/PlugIns/*/; do
    if [ -d "$d" ]; then
        rmdir "$d" 2>/dev/null || true
    fi
done

# Decide what actually goes, by iterating to a FIXPOINT.
#
# CONDEMNED starts as every candidate and only ever shrinks. Each pass
# collects the install-names referenced by everything NOT currently
# condemned; a candidate something still points at is reprieved, and on the
# NEXT pass its own dependencies count as referenced too. When a pass
# reprieves nobody, what is left is genuinely unreachable.
#
# The single pass this replaces got that wrong in a way that shipped: files
# inside a candidate are skipped when collecting references, so candidates
# cannot vouch for each other -- but a candidate that is later REPRIEVED
# never got its own dependencies counted, and they were deleted out from
# under it. Adding QtPdf to the prune list made that visible. QtPdf vouched
# for QtNetwork, QtNetwork was reprieved, and libssl, libcrypto and
# libbrotlidec -- referenced by nothing else, because the tls plugin that
# used to vouch for them had just been pruned -- were deleted anyway. The
# result passed every check the script had and was a broken bundle:
#
#     QtNetwork -> @executable_path/../Frameworks/libssl.3.dylib   (gone)
#
# Step 4b is the backstop that would now catch it regardless.
CONDEMNED=" "
for fw in $PRUNE_FRAMEWORKS; do CONDEMNED="$CONDEMNED$fw "; done

while : ; do
    PRUNE_REFS=$(
        for f in "$APP_BIN"/* "$APP"/Contents/PlugIns/*/*.dylib \
                 "$APP_FRAMEWORKS"/*.dylib "$APP_FRAMEWORKS"/*.framework/Versions/*/*; do
            [ -f "$f" ] || continue
            skip=0
            for p in $PRUNE_FRAMEWORKS; do
                # Only a STILL-CONDEMNED candidate is skipped. Once reprieved,
                # a framework is part of the keep set and what it points at
                # has to count -- that is the whole fix.
                [ "${CONDEMNED#* $p }" != "$CONDEMNED" ] || continue
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

    # Wrapped in newlines once, so each test below can ask for an exact line.
    PRUNE_REFS_NL="$(printf '\n%s\n' "$PRUNE_REFS")"

    reprieved=0
    for fw in $PRUNE_FRAMEWORKS; do
        [ "${CONDEMNED#* $fw }" != "$CONDEMNED" ] || continue   # already reprieved
        [ -e "$APP_FRAMEWORKS/$fw" ] || continue
        # Substring test rather than `printf ... | grep -qx`. That pipeline is
        # only safe while PRUNE_REFS stays short: grep -q exits on the match,
        # the writer takes SIGPIPE, and under `set -o pipefail` the status is
        # 141 rather than 0 -- so the branch inverts and a framework that IS
        # still referenced gets deleted. Demonstrated with a long list; the
        # real one is a few dozen names, which is why it has never fired. The
        # newline guards keep it a whole-line match, as -x was.
        if [ "${PRUNE_REFS_NL#*$'\n'"$fw"$'\n'}" != "$PRUNE_REFS_NL" ]; then
            echo "NOTE: $fw is still referenced — keeping it (Qt's dependency set has changed)"
            NEWC=" "
            for x in $CONDEMNED; do
                [ "$x" = "$fw" ] || NEWC="$NEWC$x "
            done
            CONDEMNED="$NEWC"
            reprieved=1
        fi
    done
    [ "$reprieved" -eq 0 ] && break
done

for fw in $PRUNE_FRAMEWORKS; do
    [ "${CONDEMNED#* $fw }" != "$CONDEMNED" ] || continue   # reprieved; keep it
    rm -rf "$APP_FRAMEWORKS/$fw"
done

# ---- 2c. Thin every binary to the architectures we actually build ------
# The official Qt ships universal (x86_64 + arm64) frameworks and plugins;
# femm-qt is built for Apple Silicon only (decision of 2026-09-15), so half
# of every Qt binary would be dead weight nobody can execute -- ~20 MB of a
# 72 MB bundle. Thin everything to the slices femm_gui itself carries: if
# the project ever builds universal, this step keeps the matching slices and
# the gate in step 4e has nothing to object to. Homebrew's Qt is single-arch
# already, so this is a no-op there. Done BEFORE signing (step 5): lipo
# invalidates signatures, and the codesign pass later re-signs everything.
WANT_ARCHS=$(lipo -archs "$APP_BIN/femm_gui" 2>/dev/null || true)
if [ -n "$WANT_ARCHS" ]; then
    THINNED=0
    while IFS= read -r -d '' _f; do
        file -b "$_f" 2>/dev/null | grep -q "Mach-O" || continue
        _have=$(lipo -archs "$_f" 2>/dev/null || true)
        [ "$_have" = "$WANT_ARCHS" ] && continue
        _keep=""
        for a in $WANT_ARCHS; do
            case " $_have " in *" $a "*) _keep="$_keep $a";; esac
        done
        [ -n "$_keep" ] || continue      # nothing of ours in it: step 4e will say so
        # shellcheck disable=SC2086
        if [ "$(echo $_keep | wc -w | tr -d ' ')" -eq 1 ]; then
            lipo "$_f" -thin $_keep -output "$_f.thin" && mv "$_f.thin" "$_f"
        else
            lipo "$_f" $(for a in $_keep; do printf ' -extract %s' "$a"; done) -output "$_f.thin" && mv "$_f.thin" "$_f"
        fi
        THINNED=$((THINNED + 1))
    done < <(find "$APP/Contents" -type f -print0 2>/dev/null)
    echo "Thinned $THINNED binaries to: $WANT_ARCHS"
fi

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

# Lua 4 legacy client assets. Two copies, and the reason is that a source tree
# and a distribution are shaped differently.
#
# In bin/, femm-lua.sh finds lua4 through its own fallback: there is no
# bin/lua4 (step 3c deletes it), but bin/femm_gui.app/Contents/MacOS/lua4 is
# right there. That is the arrangement the other platforms have, where
# everything sits beside everything (linux_build.sh / freebsd_build.sh).
#
# In the BUNDLE, because a distribution has no bin/ at all. The launcher used
# to ship nowhere: packaging/stage_extras.sh stages femm.lua and the README
# into the bindings folder and never staged femm-lua.sh, so an installed .app
# had no way to run a legacy Lua script. Even staged into bindings/luafemm it
# could not have worked -- the lua4 fallback looks for femm_gui.app BESIDE the
# script, and in ~/FEMM/bindings/luafemm the app is in /Applications.
#
# Contents/MacOS is where it belongs anyway: it is where executables live on
# macOS, it is where lua4 already is, and it is the same relationship the other
# platforms have between the launcher and the program (on Linux the launcher
# lives in the app directory and unix_install.sh symlinks it onto PATH). The
# script then needs NO change -- $here/femm.lua, $here/lua4 and FEMM_DIR=$here
# are all satisfied by that one directory.
#
# A DMG is a drag-install and cannot put anything on PATH; /usr/local/bin is
# root-owned and ~/bin is not on the macOS default PATH the way ~/.local/bin is
# on Ubuntu. So the Read Me names the full path instead, the same way
# unix_install.sh prints the line to add when PATH is not already set up.
cp ../luafemm/femm.lua ../luafemm/femm-lua.sh bin/
chmod +x bin/femm-lua.sh
cp ../luafemm/femm.lua ../luafemm/femm-lua.sh "$APP_BIN/"
chmod +x "$APP_BIN/femm-lua.sh"

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

# ---- 4b. Nothing may point at something that is not there -------------
# The bundle is finished; before it is signed, check that it is CLOSED --
# every relocatable dependency of every Mach-O in it resolves to a file that
# is actually present.
#
# This is a gate, not a warning, because both ways of getting it wrong are
# invisible at runtime until exactly the wrong moment. A dangling plugin
# dependency does not stop the app launching: Qt loads image-format and
# icon-engine plugins lazily, fails quietly, and carries on without the
# format. The bundle looks fine, signs fine, notarises fine, and is broken.
#
# Both bugs this catches shipped at least once. macdeployqt's first pass
# left the svg and pdf plugins pointing at frameworks it had not copied
# (see step 1); the prune's non-iterated fixpoint deleted libssl and
# libcrypto while keeping the QtNetwork that needs them (see step 2b).
# Neither was found by anything else in this script.
echo "Checking the bundle is self-contained..."
BROKEN_REFS="$(bundle_broken_refs)"
if [ -n "$BROKEN_REFS" ]; then
    echo "ERROR: the bundle has unresolved dependencies:"
    printf '%s\n' "$BROKEN_REFS"
    echo
    echo "       Nothing is signed or shipped in this state. Usually one of:"
    echo "         * a framework pruned in step 2b that something still needs"
    echo "           -- remove it from PRUNE_FRAMEWORKS, or prune whatever"
    echo "           pulls it in as well;"
    echo "         * macdeployqt not having copied something, which the retry"
    echo "           loop in step 1 should have handled."
    exit 1
fi
echo "  no unresolved dependencies."

# ---- 4c. Every binary must honour the declared minimum macOS -------------
# A bundle is only as portable as its least portable Mach-O: dyld refuses the
# first one it meets that was built for a newer macOS than it is running on.
# Info.plist says LSMinimumSystemVersion = $MACOS_MIN (gui/Info.plist.in via
# FEMM_MACOS_MIN); everything inside must declare that or lower. Homebrew
# dylibs declare the host version, which is why a Homebrew build is dev-only.
echo "Checking every binary against the declared minimum macOS ($MACOS_MIN)..."
PLIST_MIN=$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$APP/Contents/Info.plist" 2>/dev/null || true)
if [ "$PLIST_MIN" != "$MACOS_MIN" ]; then
    echo "ERROR: Info.plist LSMinimumSystemVersion is '$PLIST_MIN', expected '$MACOS_MIN'"
    exit 1
fi
TOO_NEW=$(
    find "$APP/Contents" -type f -print0 2>/dev/null |
    while IFS= read -r -d '' _f; do
        file -b "$_f" 2>/dev/null | grep -q "Mach-O" || continue
        _m=$(qt_floor "$_f")
        [ -n "$_m" ] || continue
        # numeric compare on major.minor
        if [ "$(printf '%s\n%s\n' "$MACOS_MIN" "$_m" | sort -V | tail -1)" != "$MACOS_MIN" ]; then
            echo "  ${_f#"$APP"/}  minos $_m"
        fi
    done | sort
)
if [ -n "$TOO_NEW" ]; then
    if [ "$QT_FLAVOUR" = homebrew ]; then
        # Expected: Homebrew's bottles are built for this macOS only. Rather
        # than ship a plist that lies, declare what the bundle actually needs
        # -- the highest minimum found -- and say so. Our own code was still
        # compiled for $MACOS_MIN, which is harmless.
        HB_MIN=$(printf '%s\n' "$TOO_NEW" | awk '{print $NF}' | sort -V | tail -1)
        /usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion $HB_MIN" "$APP/Contents/Info.plist"
        echo "  Homebrew build: $(printf '%s\n' "$TOO_NEW" | wc -l | tr -d ' ') dylibs need macOS $HB_MIN (this machine's bottles);"
        echo "  Info.plist now declares LSMinimumSystemVersion $HB_MIN. This bundle is for this Mac;"
        echo "  build with the official Qt to ship:  FEMM_QT=official ./macos_build.sh clean"
        MACOS_MIN="$HB_MIN"
    else
        echo "ERROR: these binaries require a newer macOS than the bundle declares ($MACOS_MIN):"
        printf '%s\n' "$TOO_NEW"
        echo "       Nothing is signed or shipped in this state."
        exit 1
    fi
else
    echo "  every binary declares macOS $MACOS_MIN or lower."
fi

# ---- 4d. What the bundle carries, and what it must not ------------------
# Printed on every build so a module creeping back in is visible in the log;
# the forbidden set is what step 2b exists to remove, so its presence means
# the prune lists have gone stale (as they did on 2026-09-09) and is an error,
# not a fatter bundle.
echo "Frameworks: $(ls "$APP_FRAMEWORKS" | wc -l | tr -d ' ') -> $(ls "$APP_FRAMEWORKS" | tr '\n' ' ')"
echo "PlugIns:    $(for d in "$APP"/Contents/PlugIns/*/; do [ -d "$d" ] && printf '%s(%s) ' "$(basename "$d")" "$(ls "$d" | wc -l | tr -d ' ')"; done)"
FORBIDDEN_PRESENT=""
for fw in libssl.3.dylib libcrypto.3.dylib QtNetwork.framework QtQml.framework QtQuick.framework \
          QtPdf.framework QtSvg.framework QtVirtualKeyboard.framework QtOpenGL.framework; do
    [ -e "$APP_FRAMEWORKS/$fw" ] && FORBIDDEN_PRESENT="$FORBIDDEN_PRESENT $fw"
done
if [ -n "$FORBIDDEN_PRESENT" ]; then
    echo "ERROR: pruned-by-design frameworks are in the bundle:$FORBIDDEN_PRESENT"
    echo "       Step 2b's lists are stale for this Qt -- see its comments. Not shipping this."
    exit 1
fi
for fw in QtCore.framework QtGui.framework QtWidgets.framework; do
    [ -e "$APP_FRAMEWORKS/$fw" ] || { echo "ERROR: $fw missing from the bundle"; exit 1; }
done

# ---- 4e. No binary may carry an architecture we do not ship ----------------
# Step 2c should have left only $WANT_ARCHS everywhere. Anything else -- an
# x86_64 slice that survived, or a binary with none of our slices -- is an
# error: it is either dead weight or something the build would load and
# fail on, and in neither case should it be signed and shipped.
if [ -n "${WANT_ARCHS:-}" ]; then
    echo "Checking every binary carries exactly: $WANT_ARCHS..."
    FOREIGN=$(
        find "$APP/Contents" -type f -print0 2>/dev/null |
        while IFS= read -r -d '' _f; do
            file -b "$_f" 2>/dev/null | grep -q "Mach-O" || continue
            _have=$(lipo -archs "$_f" 2>/dev/null || true)
            [ "$_have" = "$WANT_ARCHS" ] || echo "  ${_f#"$APP"/}  has: $_have"
        done | sort
    )
    if [ -n "$FOREIGN" ]; then
        echo "ERROR: binaries whose architectures differ from femm_gui's ($WANT_ARCHS):"
        printf '%s\n' "$FOREIGN"
        echo "       Nothing is signed or shipped in this state."
        exit 1
    fi
    echo "  every binary is $WANT_ARCHS only."
fi

# ---- 5. Re-sign the whole bundle --------------------------------------
# Two things broke the signature macdeployqt applied: adding/rewriting the
# solvers, and macdeployqt's own habit of mangling the signatures of the
# Homebrew dylibs it copies (the "codesign verification error" it prints).
# Re-signing every nested binary bottom-up and then the bundle fixes both.
#
# WHICH signature depends on the environment -- ad-hoc by default, Developer
# ID when FEMM_SIGN_ID is set, and notarised on top of that when
# FEMM_NOTARY_PROFILE is too. packaging/macos_sign.sh, sourced at the top,
# holds the whole arrangement and the inside-out walk itself; the three
# modes are documented in its header.
#
# An ad-hoc signature is enough to run and test HERE -- it satisfies
# Gatekeeper's "code has not been modified" check on the machine that made
# it -- and is refused everywhere else, which is what the release modes are
# for.
if command -v codesign >/dev/null 2>&1; then
    echo "Code-signing the bundle ($FEMM_SIGN_MODE)..."
    femm_sign_app "$APP" "$DEPLOY_LOG"

    # --deep is right HERE, where it means "check the nested code too" --
    # unlike signing, where it is deprecated. See macos_sign.sh.
    if codesign --verify --deep --strict --verbose=2 "$APP" >>"$DEPLOY_LOG" 2>&1; then
        echo "  Signature verified."
    else
        echo "WARNING: bundle signature did not verify."
        # The macdeployqt step is the usual culprit, so surface its (otherwise
        # hidden) output now that something has actually gone wrong.
        if [ -s "$DEPLOY_LOG" ]; then
            echo "--- macdeployqt / codesign output ---"
            cat "$DEPLOY_LOG"
            echo "--- end output ---"
        fi
    fi
fi

# ---- 6. Notarise and staple the app -----------------------------------
# Only when a notary profile is configured; otherwise this is where the
# everyday build ends.
#
# The app is notarised HERE, separately from the .dmg that
# packaging/macos_dmg.sh notarises later, and both rounds are wanted. The
# DMG's ticket covers the DMG; it does not travel with an .app dragged out
# of it onto a machine that is offline. Stapling the app as well means the
# copy in /Applications validates on its own.
if [ -n "$FEMM_NOTARY_PROFILE" ]; then
    echo "Notarising the app..."

    # notarytool will not take a .app directory -- it wants a single file --
    # so the bundle is zipped for the trip. ditto, NOT `zip`: the Qt
    # frameworks are full of symlinks (Versions/Current, the top-level
    # QtCore -> Versions/Current/QtCore), and a zip that flattens them
    # produces a submission Apple rejects as a malformed bundle.
    # --keepParent keeps the femm_gui.app wrapper inside the archive.
    #
    # The ticket is stapled to the .app, never to the zip: the zip is a
    # shipping container that is thrown away here.
    NOTARY_TMP="$(mktemp -d)"
    trap 'rm -f "$DEPLOY_LOG"; rm -rf "$NOTARY_TMP"' EXIT

    ditto -c -k --keepParent "$APP" "$NOTARY_TMP/femm_gui.zip"
    femm_notarize "$NOTARY_TMP/femm_gui.zip" "femm_gui.app"
    femm_staple "$APP" "femm_gui.app"

    # What Gatekeeper itself will say on the far side. `-t exec` is the
    # right context for an application; the DMG is assessed differently
    # (see macos_dmg.sh).
    #
    # Captured and string-matched rather than piped into `grep -q`: under
    # pipefail that pipeline can report SIGPIPE instead of the match and
    # warn about a bundle that is perfectly fine. See macos_sign.sh.
    GK="$(spctl -a -t exec -vv "$APP" 2>&1 || true)"
    if [ "${GK#*accepted}" != "$GK" ]; then
        echo "  Gatekeeper: accepted."
    else
        echo "WARNING: Gatekeeper did not accept the app:"
        printf '%s\n' "$GK" | sed 's/^/    /'
    fi
fi

# ---- 7. Re-stamp the copies so the staleness check keeps working ------
# testing/_verify_all.sh and packaging/macos_dmg.sh both detect "somebody
# ran ninja without reassembling the bundle" by asking whether femm_gui is
# newer than the copies sitting beside it. Signing breaks that test through
# no fault of its own: the .app is signed LAST -- it has to be, each
# signature seals what is below it -- and signing the .app rewrites
# Contents/MacOS/femm_gui. So femm_gui comes out a second newer than the
# solvers signed moments earlier, and every run afterwards reports a stale
# bundle that is not stale. It cost a DMG build and a suite run to find.
#
# Re-stamping the copies restores exactly what the check MEANS -- "was
# femm_gui modified after this bundle was assembled?" -- rather than
# defeating it: a later `ninja` relinks femm_gui and makes it newer than
# these again, which is the case the check exists to catch.
#
# One touch for all of them, so they share an instant and none can be
# newer than another. mtime is not covered by a code signature, so this
# invalidates neither the signature nor the stapled ticket.
STAMP_LIST=""
for exe in femm_gui $BUNDLE_EXES; do
    if [ -f "$APP_BIN/$exe" ]; then
        STAMP_LIST="$STAMP_LIST $APP_BIN/$exe"
    fi
done
if [ -n "$STAMP_LIST" ]; then
    touch $STAMP_LIST
fi

echo
echo "Build complete.  App bundle: build/bin/femm_gui.app"
case "$FEMM_SIGN_MODE" in
    ad-hoc)
        echo "Signed ad-hoc: runs here, refused on any other Mac."
        echo "Set FEMM_SIGN_ID (and FEMM_NOTARY_PROFILE) to build a"
        echo "distributable one -- see packaging/macos_sign.sh."
        ;;
    developer-id)
        if [ -n "$FEMM_NOTARY_PROFILE" ]; then
            echo "Signed, notarised and stapled. Run packaging/macos_dmg.sh"
            echo "to wrap it in a .dmg for distribution."
        else
            echo "Signed with a Developer ID but NOT notarised."
            echo "Set FEMM_NOTARY_PROFILE to notarise -- without it macOS will"
            echo "still refuse this on another machine."
        fi
        ;;
esac
