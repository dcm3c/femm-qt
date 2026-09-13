#!/usr/bin/env bash
# ======================================================================
#  macos_release.sh - build the complete, distributable macOS release.
#
#      packaging/macos_release.sh [clean]
#
#  The macOS counterpart of packaging/pack.bat: one command that produces
#  the artifact you hand to other people, as opposed to the one you test
#  with. It runs
#
#      ./macos_build.sh        signs, notarises and staples femm_gui.app
#      packaging/macos_dmg.sh  signs, notarises and staples the .dmg
#
#  in that order, which is the order that matters -- macos_dmg.sh signs the
#  IMAGE, macos_build.sh signs what is inside it, and an image built over an
#  unsigned app is rejected by Apple after a full notarisation round trip
#  rather than immediately.
#
#  ---------------------------------------------------------------------
#  WHY THIS EXISTS, given that the two scripts are two commands.
#
#  Because the signing identity is passed by environment variable, and the
#  everyday build deliberately runs WITHOUT it (see packaging/macos_sign.sh).
#  That leaves two ways to get it wrong, and this script exists to close
#  both:
#
#    * Forgetting to export them, and shipping an ad-hoc build. The scripts
#      say so loudly, but "loudly" is still something a person has to read.
#
#    * Exporting them permanently, in ~/.zprofile, so they are always set --
#      at which point every ordinary `./macos_build.sh` during development
#      starts signing with a timestamp, phoning Apple and, with a notary
#      profile set, waiting half an hour. The everyday path has to stay
#      offline and instant; that is the whole reason it is opt-in.
#
#  So: set them HERE, in the one place that means "I am cutting a release",
#  and leave the shell alone.
#
#  ---------------------------------------------------------------------
#  NOTHING ACCOUNT-SPECIFIC IS COMMITTED, same rule as pack.bat.
#
#  The identity is not written down here. It is taken from FEMM_SIGN_ID if
#  you exported one, and otherwise read out of the keychain at run time --
#  there is normally exactly one "Developer ID Application" identity, and if
#  there is more than one this script stops and asks which.
#
#  The notary profile defaults to "femm-notary" and is overridden with
#  FEMM_NOTARY_PROFILE. It is a keychain profile name, not a credential; the
#  credential itself is created once with
#
#      xcrun notarytool store-credentials femm-notary \
#            --apple-id you@example.com --team-id TEAMID
#
#  and lives in the keychain from then on.
# ======================================================================
set -euo pipefail

cd "$(dirname "$0")/.."          # femm-qt/

# ---- Identity ---------------------------------------------------------
# An explicit FEMM_SIGN_ID always wins, so this script composes with a
# shell that already has one rather than overriding it.
if [ -n "${FEMM_SIGN_ID:-}" ]; then
    echo "Identity: $FEMM_SIGN_ID  (from FEMM_SIGN_ID)"
else
    # Ask the keychain. `security find-identity -v -p codesigning` prints
    # one indented line per usable identity with the name in quotes; pull
    # out the Developer ID Application ones, which are the only kind that
    # can sign something for distribution outside the App Store.
    IDS="$(security find-identity -v -p codesigning 2>/dev/null |
           sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p')"
    COUNT="$(printf '%s' "$IDS" | grep -c . || true)"

    if [ "$COUNT" -eq 0 ]; then
        echo "ERROR: no 'Developer ID Application' identity in the keychain."
        echo
        echo "       A release needs one -- an ad-hoc signature cannot be"
        echo "       notarised, and macOS refuses an un-notarised app."
        echo "       Install the certificate from developer.apple.com, or run"
        echo "       ./macos_build.sh on its own for an ad-hoc local build."
        exit 1
    fi
    if [ "$COUNT" -gt 1 ]; then
        echo "ERROR: more than one Developer ID Application identity here:"
        printf '%s\n' "$IDS" | sed 's/^/         /'
        echo
        echo "       Say which one:"
        echo "         export FEMM_SIGN_ID=\"<one of the lines above>\""
        exit 1
    fi

    FEMM_SIGN_ID="$IDS"
    echo "Identity: $FEMM_SIGN_ID  (found in keychain)"
fi
export FEMM_SIGN_ID

# ---- Notary profile ---------------------------------------------------
export FEMM_NOTARY_PROFILE="${FEMM_NOTARY_PROFILE:-femm-notary}"

# Checked NOW, not when the first submission is attempted. Otherwise a
# mistyped or missing profile is discovered after a full build and a bundle
# assembly -- and the notarisation step is where all the waiting is, so the
# cost of learning late is the highest in the script. This is one network
# round trip and no submission.
echo "Notary:   $FEMM_NOTARY_PROFILE"
printf '          checking credentials... '
if xcrun notarytool history --keychain-profile "$FEMM_NOTARY_PROFILE" \
        >/dev/null 2>&1; then
    echo "ok"
else
    echo "FAILED"
    echo
    echo "ERROR: the notary profile '$FEMM_NOTARY_PROFILE' does not work."
    echo "       Either it does not exist, or its credentials are stale."
    echo
    echo "       Create it once, interactively:"
    echo "         xcrun notarytool store-credentials $FEMM_NOTARY_PROFILE \\"
    echo "               --apple-id you@example.com --team-id TEAMID"
    echo
    echo "       It asks for an APP-SPECIFIC password from appleid.apple.com,"
    echo "       not your Apple ID password."
    exit 1
fi

# Bounded wait, so a release cannot hang forever on Apple's queue. Honours
# an existing setting; see femm_notarize in packaging/macos_sign.sh.
export FEMM_NOTARY_TIMEOUT="${FEMM_NOTARY_TIMEOUT:-1h}"

echo
echo "Expect two notarisation waits, one per artifact. Apple's queue has"
echo "taken around 25 minutes each here, and that is normal, not a hang."
echo

# ---- Build ------------------------------------------------------------
# "clean" is passed straight through to macos_build.sh, which is the only
# one of the two that understands it.
echo "=== 1/2  building, signing and notarising femm_gui.app ==="
./macos_build.sh ${1:+"$1"}

echo
echo "=== 2/2  building, signing and notarising the disk image ==="
bash packaging/macos_dmg.sh

# ---- Say what came out ------------------------------------------------
# Named explicitly rather than left for the reader to find: the DMG carries
# a datestamp, so "the newest one in packaging/output" is the only way to
# identify it and is worth printing.
DMG="$(ls -t packaging/output/*.dmg 2>/dev/null | head -1)"
echo
if [ -n "$DMG" ]; then
    echo "Release complete: $DMG"
    echo "Both the app and the image are signed, notarised and stapled."
else
    echo "WARNING: finished, but no .dmg turned up in packaging/output/."
fi
