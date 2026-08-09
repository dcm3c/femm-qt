#!/usr/bin/env bash
# ======================================================================
#  gen-third-party-notices.sh - build THIRD-PARTY-NOTICES.txt for the
#  macOS app bundle.
#
#  Usage:  ./packaging/gen-third-party-notices.sh [APP_BUNDLE] [OUTPUT_FILE]
#
#    APP_BUNDLE   path to femm_gui.app   (default: build/bin/femm_gui.app)
#    OUTPUT_FILE  file to write          (default: <repo>/THIRD-PARTY-NOTICES.txt)
#
#  macos_build.sh runs macdeployqt, which copies Qt's transitive Homebrew
#  dependencies -- roughly 30 dylibs -- into
#  femm_gui.app/Contents/Frameworks/.  Shipping the .app redistributes every
#  one of them, so each needs a notice.  This script produces that aggregate
#  notice file automatically.
#
#  For every *.dylib in Contents/Frameworks/ it:
#    1. maps the dylib back to the Homebrew formula that installed it, using
#       the Cellar/<formula>/<version>/ component of the keg path (reliable
#       even after macdeployqt rewrites the copy's own install name);
#    2. asks `brew info --json=v2` for the SPDX license id and homepage;
#    3. copies the full license text out of the keg if one ships there, or
#       emits a clearly marked placeholder if none does.
#
#  Qt*.framework directories are deliberately skipped: Qt is covered
#  separately by the repo's NOTICE-Qt.txt (LGPL-3.0).
#
#  IMPORTANT: run this against a bundle produced by the CURRENT
#  macos_build.sh.  That build prunes the unused QtVirtualKeyboard plugin
#  and, transitively, QtQuick/QtQml/QtNetwork (and with them OpenSSL and
#  brotli), so those libraries never reach Contents/Frameworks/.  Generate
#  from a stale bundle and you will document libraries that no longer ship.
#  Build first, then generate.
#
#  At the end the script prints a summary of any component that needs manual
#  attention (no license text found in its keg) so the gap can be filled in
#  by hand rather than silently omitted.
# ======================================================================
set -euo pipefail

cd "$(dirname "$0")/.."          # repo root
REPO_ROOT="$PWD"

APP="${1:-build/bin/femm_gui.app}"
OUT="${2:-$REPO_ROOT/THIRD-PARTY-NOTICES.txt}"
FRAMEWORKS="$APP/Contents/Frameworks"
# License texts kept in-repo for components whose Homebrew keg ships none.
# Named per-formula (glib.txt) or per-license (LGPL-2.1.txt, shared).
LICENSE_TEXTS="$REPO_ROOT/packaging/license-texts"

# ---- Sanity checks ---------------------------------------------------
command -v brew    >/dev/null 2>&1 || { echo "ERROR: 'brew' not found."; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "ERROR: 'python3' not found."; exit 1; }
if [ ! -d "$FRAMEWORKS" ]; then
    echo "ERROR: $FRAMEWORKS not found."
    echo "Build the bundle first:  ./macos_build.sh"
    exit 1
fi

CELLAR="$(brew --cellar)"
PREFIX="$(brew --prefix)"
GEN_DATE="$(date -u +%Y-%m-%d)"

# ----------------------------------------------------------------------
#  Helpers
# ----------------------------------------------------------------------

# resolve_keg <dylib-basename>  ->  prints "<formula>\t<version>\t<kegdir>"
# Prefer the prefix/lib symlink (points straight into the Cellar); fall back
# to searching the Cellar for keg-only formulae that are not linked into
# prefix/lib.
resolve_keg() {
    local base="$1" real kegpath
    real="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' \
                "$PREFIX/lib/$base" 2>/dev/null || true)"
    if [ "${real#"$CELLAR"/}" != "$real" ]; then
        kegpath="$real"
    else
        kegpath="$(find "$CELLAR" -maxdepth 4 -name "$base" 2>/dev/null | head -1)"
    fi
    [ -n "$kegpath" ] || return 1
    local rel="${kegpath#"$CELLAR"/}"          # formula/version/lib/<base>
    local formula="${rel%%/*}"
    local rest="${rel#*/}"
    local version="${rest%%/*}"
    printf '%s\t%s\t%s\n' "$formula" "$version" "$CELLAR/$formula/$version"
}

# find_license_text <kegdir>  ->  prints path to a license file, or nothing.
# Prefer the plain, top-level names before variant/suffixed ones so we ship
# the project's primary license rather than, say, a secondary COPYING.LGPLv2.1.
find_license_text() {
    local keg="$1" f
    for f in LICENSE LICENSE.txt LICENSE.TXT LICENSE.md License.txt \
             COPYING COPYING.txt COPYRIGHT COPYRIGHT.txt NOTICE NOTICE.txt; do
        if [ -f "$keg/$f" ]; then echo "$keg/$f"; return 0; fi
    done
    # Fall back to anything license-shaped anywhere shallow in the keg.
    find "$keg" -maxdepth 2 -type f \
        \( -iname 'LICENSE*' -o -iname 'COPYING*' -o -iname 'COPYRIGHT*' \
           -o -iname 'NOTICE*' \) 2>/dev/null | sort | head -1
}

# brew_field <formula> <field>  ->  prints one field from brew's JSON.
brew_field() {
    brew info --json=v2 "$1" 2>/dev/null | \
        python3 -c "import json,sys
d=json.load(sys.stdin)['formulae'][0]
print(d.get('$2') or '')" 2>/dev/null || true
}

# ----------------------------------------------------------------------
#  1. Enumerate dylibs and group them by formula.
#     (webp, glib, pcre2, icu4c... each ship several dylibs -> one section.)
# ----------------------------------------------------------------------
echo "Scanning $FRAMEWORKS ..."

# Parallel arrays keyed by formula (bash 3.2 on macOS has no assoc arrays).
FORMULAE=""            # space-separated list of unique formula names
declare_ver()  { eval "VER_$1=\"\$2\""; }
declare_files(){ eval "FILES_$1=\"\$2\""; }
get_ver()      { eval "printf '%s' \"\${VER_$1:-}\""; }
get_files()    { eval "printf '%s' \"\${FILES_$1:-}\""; }
# formula names are Homebrew-legal but contain '@'/'-'/'.' -> sanitize for var names
key() { printf '%s' "$1" | tr '@.-' '___'; }

for dy in "$FRAMEWORKS"/*.dylib; do
    [ -e "$dy" ] || continue
    base="$(basename "$dy")"
    if ! info="$(resolve_keg "$base")"; then
        echo "  WARNING: could not map $base to a Homebrew keg -- skipping."
        UNMAPPED="${UNMAPPED:-} $base"
        continue
    fi
    formula="$(printf '%s' "$info" | cut -f1)"
    version="$(printf '%s' "$info" | cut -f2)"
    k="$(key "$formula")"
    case " $FORMULAE " in
        *" $formula "*) ;;                       # already seen
        *) FORMULAE="$FORMULAE $formula"; declare_ver  "$k" "$version" ;;
    esac
    existing="$(get_files "$k")"
    declare_files "$k" "${existing:+$existing }$base"
    echo "  $base -> $formula $version"
done

FORMULAE="$(printf '%s\n' $FORMULAE | sort -u)"
if [ -z "${FORMULAE//[[:space:]]/}" ]; then
    echo "ERROR: no dylibs found in $FRAMEWORKS."; exit 1
fi

# ----------------------------------------------------------------------
#  2. Emit the notice file.
# ----------------------------------------------------------------------
NEED_MANUAL=""          # formulae with no license text in the keg
LGPL_LIST=""            # formulae carrying LGPL obligations

emit() { printf '%s\n' "$1" >>"$OUT"; }
rule() { emit "======================================================================"; }
thin() { emit "----------------------------------------------------------------------"; }

: >"$OUT"
rule
emit "THIRD-PARTY NOTICES  --  FEMM-QT macOS application bundle"
rule
emit ""
emit "Generated by packaging/gen-third-party-notices.sh on $GEN_DATE"
emit "Source bundle: $APP"
emit ""
emit "femm_gui.app redistributes the libraries listed below inside"
emit "Contents/Frameworks/.  They are Qt's transitive dependencies, copied into"
emit "the bundle by macdeployqt.  Each component is listed with its version,"
emit "SPDX license identifier, upstream URL, and -- where the upstream ships"
emit "one -- the full text of its license."
emit ""
emit "Qt itself is NOT covered here.  See NOTICE-Qt.txt (LGPL-3.0) for Qt."
emit ""
emit "Components marked [LGPL] carry obligations beyond attribution (you must"
emit "allow the end user to relink against a modified version of the library --"
emit "satisfied here because the bundle ships the unmodified dynamic library and"
emit "this notice, mirroring the arrangement for Qt in NOTICE-Qt.txt)."
emit ""

# Table of contents (filled after we know the license ids).
TOC_MARKER="@@TOC@@"
emit "$TOC_MARKER"
emit ""

TOC=""   # accumulate "formula<TAB>version<TAB>license" lines for the TOC

for formula in $FORMULAE; do
    k="$(key "$formula")"
    version="$(get_ver "$k")"
    files="$(get_files "$k")"
    keg="$CELLAR/$formula/$version"

    license="$(brew_field "$formula" license)"
    homepage="$(brew_field "$formula" homepage)"
    [ -n "$license" ]  || license="(unknown -- see upstream)"
    [ -n "$homepage" ] || homepage="(unknown)"

    # If Homebrew's formula metadata ever disagrees with the license text a
    # keg actually ships, correct it here with a justification -- a wrong SPDX
    # id in a compliance document is worse than none. (The libomp correction
    # that used to live here retired with the OpenMP dependency.)

    is_lgpl=""
    case "$license" in *LGPL*) is_lgpl="yes"; LGPL_LIST="$LGPL_LIST $formula" ;; esac

    echo "Documenting $formula $version ($license)..."

    rule
    if [ -n "$is_lgpl" ]; then
        emit "$formula  $version   [LGPL]"
    else
        emit "$formula  $version"
    fi
    rule
    emit "SPDX-License-Identifier: $license"
    emit "Homepage: $homepage"
    emit "Bundled files: $(printf '%s' "$files" | tr ' ' ',' | sed 's/,/, /g')"
    [ -n "$is_lgpl" ] && emit "NOTE: LGPL component -- see the LGPL obligations note in the header."
    emit ""

    lic_text="$(find_license_text "$keg" || true)"
    lic_src="the Homebrew keg"

    # Some kegs ship no license text at all (glib, notably, which is LGPL and
    # so the one where a missing text matters most). Fall back to a copy kept
    # in the repo, so a regeneration cannot silently drop it again: first a
    # per-formula file, then one named for the license itself, which is shared
    # by any component under the same terms.
    if [ -z "$lic_text" ] || [ ! -f "$lic_text" ]; then
        base_id="${license%-or-later}"          # LGPL-2.1-or-later -> LGPL-2.1
        for cand in "$LICENSE_TEXTS/$formula.txt" "$LICENSE_TEXTS/$base_id.txt"; do
            if [ -f "$cand" ]; then
                lic_text="$cand"
                lic_src="packaging/license-texts (the keg ships none)"
                break
            fi
        done
    fi

    if [ -n "$lic_text" ] && [ -f "$lic_text" ]; then
        emit "License text ($(basename "$lic_text") -- from $lic_src):"
        thin
        cat "$lic_text" >>"$OUT"
        emit ""
        thin
    else
        emit "*** LICENSE TEXT NOT SHIPPED IN THE HOMEBREW KEG ***"
        emit "*** FILL IN MANUALLY from $homepage (license: $license) ***"
        emit "*** or drop the text in packaging/license-texts/$formula.txt ***"
        NEED_MANUAL="$NEED_MANUAL $formula"
    fi
    emit ""

    lgpl_tag=""; [ -n "$is_lgpl" ] && lgpl_tag=" [LGPL]"
    TOC="$TOC$(printf '  %-18s %-14s %s%s\n' "$formula" "$version" "$license" "$lgpl_tag")
"
done

# ---- End matter: manual-attention summary inside the file ------------
rule
emit "COMPONENTS REQUIRING MANUAL ATTENTION"
rule
if [ -n "${NEED_MANUAL// /}" ]; then
    emit "The Homebrew keg for the following components did not ship a license"
    emit "text file.  The SPDX id and homepage are recorded above; paste the"
    emit "upstream license text into each placeholder before distribution:"
    emit ""
    for f in $NEED_MANUAL; do emit "  - $f"; done
else
    emit "None -- every component shipped a license text in its keg."
fi
emit ""

# ---- Splice the table of contents in over its marker -----------------
TOC_HEADER="Components (see full entries below):"
python3 - "$OUT" "$TOC_MARKER" "$TOC_HEADER" "$TOC" <<'PY'
import sys
path, marker, header, toc = sys.argv[1:5]
with open(path) as fh:
    text = fh.read()
block = header + "\n\n" + toc.rstrip("\n") + "\n"
with open(path, "w") as fh:
    fh.write(text.replace(marker, block, 1))
PY

# ----------------------------------------------------------------------
#  3. Report to the operator.
# ----------------------------------------------------------------------
echo ""
echo "Wrote $OUT"
echo ""
if [ -n "${UNMAPPED:-}" ]; then
    echo "WARNING: these dylibs could not be mapped to a Homebrew keg and were"
    echo "         omitted from the notice -- investigate before distributing:"
    for b in $UNMAPPED; do echo "  - $b"; done
    echo ""
fi
if [ -n "${LGPL_LIST// /}" ]; then
    echo "LGPL components (obligations beyond attribution):"
    for f in $(printf '%s\n' $LGPL_LIST | sort -u); do echo "  - $f"; done
    echo ""
fi
if [ -n "${NEED_MANUAL// /}" ]; then
    echo "MANUAL ATTENTION REQUIRED -- no license text found in the keg for:"
    for f in $NEED_MANUAL; do echo "  - $f"; done
    echo ""
    echo "Each has a marked placeholder in $(basename "$OUT"); fill it in by hand."
else
    echo "All components shipped a license text; no manual attention required."
fi
