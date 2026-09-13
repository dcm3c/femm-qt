#!/usr/bin/env bash
# ======================================================================
#  macos_dmg_nametest.sh - does macos_dmg.sh name its image correctly?
#
#  Usage:  packaging/macos_dmg_nametest.sh
#
#  macos_dmg.sh can finish in four states, and only one of them produces
#  something shippable. Reaching them for real means four builds, two of
#  which need a Developer ID and a notarisation round trip to Apple, so
#  in practice nobody checks more than the state they happen to be in --
#  which is how an ad-hoc image came to be named like a release.
#
#  This drives the naming tail of macos_dmg.sh through all four with the
#  macOS-only commands stubbed. It runs anywhere bash does, including on
#  the machine that cannot build a DMG at all, which is the point: the
#  logic being checked is shell, not anything Apple provides.
#
#  The tail is CUT OUT OF macos_dmg.sh at run time rather than copied
#  here. A retyped copy would keep passing after the real script changed.
# ======================================================================
set -uo pipefail

cd "$(dirname "$0")/.."          # femm-qt/
SRC="packaging/macos_dmg.sh"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/out"

sed -n "/^# ---- What this image will do on someone else's Mac/,\$p" "$SRC" \
    > "$T/tail.sh"
if [ ! -s "$T/tail.sh" ]; then
    echo "FAIL: could not find the naming tail in $SRC -- has it been renamed?"
    exit 1
fi

FAILED=0

expect() {                   # mode notary gk want label
    local mode="$1" notary="$2" gk="$3" want="$4" label="$5" got
    rm -f "$T"/out/*.dmg "$T"/out/.*.dmg 2>/dev/null
    (
        OUT_DIR="$T/out"
        DMG_BASE="femm-qt-5.0_01Jan2026"
        DMG="$OUT_DIR/.${DMG_BASE}.building.dmg"
        DMG_WORK="$DMG"
        FEMM_SIGN_MODE="$mode"
        FEMM_NOTARY_PROFILE="$notary"
        : > "$DMG"
        # The two things the tail shells out to, and nothing else.
        spctl() { echo "$gk"; }
        du()    { printf '12M\tx\n'; }
        trap '[ -n "${DMG_WORK:-}" ] && rm -f "$DMG_WORK"; true' EXIT
        . "$T/tail.sh" > /dev/null 2>&1
    )
    got="$(cd "$T/out" && ls -A 2>/dev/null | tr '\n' ' ')"
    got="${got% }"
    if [ "$got" = "$want" ]; then
        printf '  ok    %-32s %s\n' "$label" "$got"
    else
        printf '  FAIL  %-32s got [%s], wanted [%s]\n' "$label" "$got" "$want"
        FAILED=1
    fi
}

B="femm-qt-5.0_01Jan2026"
echo "macos_dmg.sh naming:"
expect ad-hoc       ""            ""  "$B-adhoc.dmg" \
       "no FEMM_SIGN_ID"
expect developer-id ""            ""  "$B-unnotarized.dmg" \
       "signed, no notary profile"
expect developer-id femm-notary   "x: accepted
source=Notarized Developer ID"        "$B.dmg" \
       "signed+notarised, accepted"
expect developer-id femm-notary   "x: rejected"  "$B-REJECTED.dmg" \
       "signed+notarised, REJECTED"

# An aborted run must leave nothing behind. Notarisation needs Apple's
# servers, so aborting part way through is a normal outcome, and a
# half-built image left in packaging/output is indistinguishable from a
# finished one once the terminal scrollback is gone.
rm -f "$T"/out/*.dmg "$T"/out/.*.dmg 2>/dev/null
(
    DMG_WORK="$T/out/.$B.building.dmg"
    : > "$DMG_WORK"
    trap '[ -n "${DMG_WORK:-}" ] && rm -f "$DMG_WORK"; true' EXIT
    exit 1
) 2>/dev/null
LEFT="$(cd "$T/out" && ls -A 2>/dev/null | tr '\n' ' ')"
if [ -z "$LEFT" ]; then
    echo "  ok    aborted run                      leaves nothing"
else
    echo "  FAIL  aborted run                      left [$LEFT]"
    FAILED=1
fi

# macos_release.sh reports the build with `ls -t packaging/output/*.dmg`.
# A dot-prefixed working file must not be picked up by that glob.
: > "$T/out/.$B.building.dmg"
PICK="$(cd "$T/out" && ls -t ./*.dmg 2>/dev/null | tr '\n' ' ')"
if [ -z "$PICK" ]; then
    echo "  ok    working file                     invisible to *.dmg"
else
    echo "  FAIL  working file                     matched by *.dmg: [$PICK]"
    FAILED=1
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "macos_dmg.sh names its image for what the image turned out to be."
else
    echo "macos_dmg.sh naming is WRONG -- see the FAIL lines above."
fi
exit "$FAILED"
