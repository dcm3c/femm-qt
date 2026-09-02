#!/usr/bin/env bash
# ======================================================================
#  stage_extras.sh - copy the examples, the language bindings and the
#                    loose documents into a destination directory.
#
#  Usage:  packaging/stage_extras.sh <dest-dir> <femm-qt-root>
#
#  Called by macos_build.sh (into femm_gui.app/Contents/Resources) and by
#  the install step of linux_build.sh / freebsd_build.sh (into the
#  installed program directory). One implementation, so the three cannot
#  drift apart -- which is exactly how the Windows installer's own
#  example list fell four years behind the repository.
#
#  The example list is read from packaging/femm-qt.iss rather than kept
#  here. That file already curates which files ship, and the examples
#  directory in a working tree also holds solver output and scratch
#  models that must not go out.
# ======================================================================
set -eu

DEST="${1:?usage: stage_extras.sh <dest-dir> <femm-qt-root>}"
ROOT="${2:?usage: stage_extras.sh <dest-dir> <femm-qt-root>}"

ISS="$ROOT/packaging/femm-qt.iss"
[ -f "$ISS" ] || { echo "stage_extras: no manifest at $ISS" >&2; exit 1; }

# ---- Examples --------------------------------------------------------
# Every example line reads
#   Source: "..\examples\X"; DestDir: "{app}\examples"; ...
# and no other DestDir ends in examples", so the pattern needs no
# backslash. The paths are relative to packaging/.
mkdir -p "$DEST/examples"
n=0
missing=0
while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    src="$ROOT/packaging/$rel"
    if [ -f "$src" ]; then
        cp "$src" "$DEST/examples/"
        n=$((n + 1))
    else
        missing=$((missing + 1))
        echo "  WARNING: listed in femm-qt.iss but not found: $rel" >&2
    fi
done <<EOF
$(awk -F'"' '/DestDir: .*examples";/ { print $2 }' "$ISS" | tr '\\' '/')
EOF

if [ "$n" -eq 0 ]; then
    echo "stage_extras: no examples extracted from femm-qt.iss -- the manifest" >&2
    echo "              format must have changed. Fix the awk above rather than" >&2
    echo "              shipping with no examples." >&2
    exit 1
fi

# ---- Language bindings ----------------------------------------------
# Client-side text, loaded by Octave/Julia/Mathematica/Scilab/Python and
# never by femm-qt itself. Directory names match the Windows installer's
# so one set of instructions covers every platform: note that octavefemm
# is installed as "mfiles" everywhere but the source tree.
mkdir -p "$DEST/bindings"
copy_binding() {                 # $1 = destination name, $2... = sources
    d="$DEST/bindings/$1"; shift
    mkdir -p "$d"
    for f in "$@"; do
        [ -f "$f" ] && cp "$f" "$d/"
    done
    return 0
}
copy_binding mfiles    "$ROOT"/octavefemm/*.m
copy_binding mathfemm  "$ROOT/mathfemm/mathfemm.m"
copy_binding scifemm   "$ROOT/scifemm/scifemm.sci"
copy_binding juliafemm "$ROOT/juliafemm/juliafemm.jl"
copy_binding pyfemm    "$ROOT/pyfemm/femm.py"
copy_binding luafemm   "$ROOT/luafemm/femm.lua" "$ROOT/luafemm/README.md"

# ---- Loose documents -------------------------------------------------
for doc in "$ROOT/readme.txt" \
           "$ROOT/octavefemm/manual/octavefemm.pdf" \
           "$ROOT/luafemm/femm-lua.pdf" \
           "$ROOT/mathfemm/usage.nb"; do
    [ -f "$doc" ] && cp "$doc" "$DEST/"
done

# The per-binding guides. These are the documents that answer "how do I use
# this from Octave / Python / Lua", and every one of them was sitting in the
# binding's own source directory, which an installed user has no reason to
# open and every reason not to find. luafemm's README is renamed on the way
# out: in one flat directory beside five other guides, "README.md" says
# nothing about which thing it is the readme for.
for doc in "$ROOT/pyfemm/pyfemm.md" \
           "$ROOT/octavefemm/octavefemm.md" \
           "$ROOT/luafemm/femm-lua.md"; do
    [ -f "$doc" ] && cp "$doc" "$DEST/"
done
[ -f "$ROOT/luafemm/README.md" ] && cp "$ROOT/luafemm/README.md" "$DEST/luafemm.md"

# ---- License texts ---------------------------------------------------
# These MUST travel with the binaries. The solvers are GPL-3.0-or-later, whose
# section 4 requires conveying the license with the program, and every vendored
# MIT/X11 component requires its copyright and permission notice to be included
# in distributions.
#
# Windows shipped them via femm-qt.iss and macOS via macos_build.sh's
# copy_license, but the Unix install shipped NONE -- so a Linux or FreeBSD
# install was the one route that distributed the solvers without the GPL text.
# Same names on every platform so the file a user is pointed at is the same
# file everywhere. Missing sources are skipped rather than fatal, because a
# partial source tree should still install.
lic() {                          # $1 = source path, $2 = name in the install
    [ -f "$1" ] && cp "$1" "$DEST/$2" && lic_n=$((lic_n + 1))
}
lic_n=0
lic "$ROOT/LICENSE-MIT"                            LICENSE-MIT
lic "$ROOT/LICENSE-GPL"                            LICENSE-GPL
lic "$ROOT/NOTICE-Qt.txt"                          NOTICE-Qt.txt
lic "$ROOT/LICENSE-melihaltun.txt"                 LICENSE-melihaltun.txt
lic "$ROOT/lua/liblua/COPYRIGHT"                   Lua-COPYRIGHT.txt
lic "$ROOT/lua/luasocket-1.0/COPYRIGHT"            LuaSocket-COPYRIGHT.txt
lic "$ROOT/micropython/LICENSE.micropython"        LICENSE-MicroPython.txt
lic "$ROOT/micropython/ulab/LICENSE"               LICENSE-ulab.txt
lic "$ROOT/third_party/bdwgc/LICENSE"              LICENSE-bdwgc.txt
lic "$ROOT/pylib/vendor/LICENSE.micropython-lib"   LICENSE-micropython-lib.txt
lic "$ROOT/QT-THIRD-PARTY-NOTICES.txt"             QT-THIRD-PARTY-NOTICES.txt
echo "  licenses: $lic_n staged"

# NOT THIRD-PARTY-NOTICES.txt. It is macOS-only by construction: generated by
# gen-third-party-notices.sh from the .app's Contents/Frameworks, it documents
# the Homebrew dylibs macdeployqt copies in -- dbus, glib, icu4c, libtiff,
# webp and a dozen more. A Linux or FreeBSD install ships none of those; it
# links the system Qt. macos_build.sh copies it separately, which is where it
# belongs. (It was briefly staged here, which would have shipped a document
# headed "FEMM-QT macOS application bundle" to Linux users, listing libraries
# not present.)

if [ "$missing" -gt 0 ]; then
    echo "  examples: $n copied, $missing missing"
else
    echo "  examples: $n copied"
fi
