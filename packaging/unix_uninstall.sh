#!/usr/bin/env bash
# ======================================================================
#  unix_uninstall.sh - remove what unix_install.sh installed
#
#  Not run directly; linux_build.sh and freebsd_build.sh call it for
#  their `uninstall` argument.  Usage:
#
#      packaging/unix_uninstall.sh <prefix> [-y]
#
#  ---- Why a manifest, and not a list of paths recomputed here -------
#
#  The install makes decisions that cannot be re-derived with certainty
#  later: which of the four solver links it managed to create, whether
#  it edited a login profile, and above all WHERE the documents went --
#  that comes from xdg-user-dir, which is localised and which the user
#  may have reconfigured since.  Guessing any of those wrong means
#  either leaving files behind or, far worse, deleting the wrong ones.
#
#  So the install writes down exactly what it did, and this reads it.
#  A missing manifest is treated as "not an install I made", and is
#  refused rather than guessed at.
#
#  ---- What is deliberately NOT removed ------------------------------
#
#  ~/Documents/FEMM/examples.  Those are the user's copies, never
#  overwritten by an install precisely because they are meant to be
#  edited, and a script that deletes a directory someone has been
#  working in for a year is not one anybody should run.  It is reported
#  instead.
# ======================================================================
set -eu

PREFIX="${1:?usage: unix_uninstall.sh <prefix> [-y]}"
ASSUME_YES=0
[ "${2:-}" = "-y" ] && ASSUME_YES=1

APPDIR="$PREFIX/opt/femm-qt"
MANIFEST="$APPDIR/uninstall-manifest.txt"

if [ ! -d "$APPDIR" ]; then
    echo "Nothing to uninstall: $APPDIR does not exist."
    echo "If you installed with a different prefix, pass it:"
    echo "    ./linux_build.sh uninstall /your/prefix"
    exit 0
fi

if [ ! -f "$MANIFEST" ]; then
    echo "ERROR: $APPDIR exists but has no uninstall manifest."
    echo "       That means it was not installed by this script -- or was"
    echo "       installed by a version predating the manifest. Refusing to"
    echo "       delete a directory I cannot account for; remove it by hand if"
    echo "       you are sure:"
    echo "           rm -rf $APPDIR"
    exit 1
fi

# ---- Read the manifest ----------------------------------------------
# One "kind path" per line. Read it ALL now: the manifest lives inside the
# directory this is about to delete.
dirs=""
files=""
links=""
kept=""
profile=""
# A literal newline, via $'\n'. NOT $(printf '\n'):
# command substitution strips trailing newlines, so that yields an EMPTY
# separator, every path runs into the next, and the uninstaller then
# silently removes nothing at all -- which is precisely what it did the
# first time this was run.
NL=$'\n'
while read -r kind path; do
    case "$kind" in
        dir)     dirs="$dirs$path$NL" ;;
        file)    files="$files$path$NL" ;;
        link)    links="$links$path$NL" ;;
        keep)    kept="$path" ;;
        profile) profile="$path" ;;
        *)       ;;                      # comment or blank
    esac
done < "$MANIFEST"

# ---- Say what will go, before anything goes --------------------------
echo "This will remove:"
printf '%s' "$dirs"  | while IFS= read -r p; do [ -n "$p" ] && echo "    $p"; done
printf '%s' "$files" | while IFS= read -r p; do [ -n "$p" ] && echo "    $p"; done
n_links=$(printf '%s' "$links" | grep -c . || true)
[ "$n_links" -gt 0 ] && echo "    $n_links command links in $PREFIX/bin"
if [ -n "$profile" ] && [ -f "$profile" ] &&
   grep -q "# added by femm-qt install" "$profile"; then
    echo "    the PATH line femm-qt added to $profile"
fi
if [ -n "$kept" ] && [ -d "$kept" ]; then
    echo
    echo "It will NOT remove $kept --"
    echo "those are your copies of the examples, and may hold your own edits."
fi

if [ "$ASSUME_YES" -eq 0 ]; then
    if [ ! -t 0 ]; then
        echo
        echo "Not running interactively; pass -y to confirm."
        exit 1
    fi
    echo
    printf "Proceed? [y/N] "
    read -r reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) echo "Cancelled."; exit 0 ;;
    esac
fi

echo

# ---- Links first, and only ours -------------------------------------
# A name like lua4 or fkn could belong to something else entirely. Remove a
# link only if it still points inside the directory we are removing; anything
# else is somebody else's and is left alone, loudly.
printf '%s' "$links" | while IFS= read -r p; do
    [ -n "$p" ] || continue
    if [ -L "$p" ]; then
        target=$(readlink -f "$p" 2>/dev/null || echo "")
        case "$target" in
            "$APPDIR"/*) rm -f "$p" ;;
            *) echo "  left alone: $p does not point into $APPDIR" ;;
        esac
    elif [ -e "$p" ]; then
        echo "  left alone: $p is not a symlink"
    fi
done

printf '%s' "$files" | while IFS= read -r p; do
    [ -n "$p" ] && rm -f "$p"
done

# ---- The PATH line, if we are the ones who added it ------------------
if [ -n "$profile" ] && [ -f "$profile" ] &&
   grep -q "# added by femm-qt install" "$profile"; then
    tmp="$profile.femm-qt.$$"
    awk '
        /^# added by femm-qt install$/ { skip = 1; next }
        skip > 0                       { skip--; next }
        { print }
    ' "$profile" > "$tmp" && mv "$tmp" "$profile"
    echo "  removed the PATH line from $profile"
fi

# ---- Directories last: the manifest lives in one of them -------------
printf '%s' "$dirs" | while IFS= read -r p; do
    [ -n "$p" ] && rm -rf "$p"
done

# FEMM/ itself, if taking docs/ out left it empty and the examples are gone
# too. rmdir, never rm -rf: it removes it only if it is genuinely empty.
if [ -n "$kept" ]; then
    rmdir "$(dirname "$kept")" 2>/dev/null || true
fi

echo
echo "Uninstalled."
if [ -n "$kept" ] && [ -d "$kept" ]; then
    echo "Your examples are still in $kept."
fi
if [ -n "$profile" ]; then
    echo "Open a new login session for the PATH change to take effect."
fi
