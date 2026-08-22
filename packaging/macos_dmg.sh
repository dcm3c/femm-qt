#!/usr/bin/env bash
# ======================================================================
#  macos_dmg.sh - wrap the built app bundle in a draggable .dmg
#
#  Usage:  packaging/macos_dmg.sh
#
#  Run AFTER ./macos_build.sh. This is the macOS counterpart of running
#  Inno Setup over packaging/femm-qt.iss, and it is deliberately a
#  separate step for the same reasons: femm_gui.app has to exist to run
#  and to test -- the regression suite drives the binaries inside it --
#  whereas a .dmg is needed by nobody except someone shipping. Folding
#  this into macos_build.sh would put hdiutil, and later a notarisation
#  round trip to Apple, on the everyday build path.
#
#  NOT SIGNED OR NOTARISED. macos_build.sh applies an ad-hoc signature to
#  the bundle, which is enough to run and test locally, but a DMG handed
#  to anyone else needs a Developer ID:
#
#      codesign --force --options runtime --timestamp \
#               --sign "Developer ID Application: ..." <app>
#      xcrun notarytool submit <dmg> --keychain-profile ... --wait
#      xcrun stapler staple <dmg>
#
#  Those are left out on purpose rather than written blind -- the failure
#  modes there (hardened runtime, entitlements, nested binaries needing
#  their own timestamped signature) only show up against a real
#  certificate and real error messages.
# ======================================================================
set -euo pipefail

cd "$(dirname "$0")/.."          # femm-qt/

APP="build/bin/femm_gui.app"
OUT_DIR="packaging/output"
VOLNAME="FEMM 5.0"
STAMP="$(date +%d%b%Y)"
DMG="$OUT_DIR/femm-qt-5.0_${STAMP}.dmg"

# ---- The bundle has to be there, and has to be current ---------------
if [ ! -d "$APP" ]; then
    echo "ERROR: $APP not found. Run ./macos_build.sh first."
    exit 1
fi

# A bundle that never had macos_build.sh run over it is missing everything
# the app needs at runtime, and the result would look like a working DMG.
for needed in Contents/MacOS/femm_headless Contents/Resources/matlib.dat \
              Contents/Resources/examples Contents/MacOS/python; do
    if [ ! -e "$APP/$needed" ]; then
        echo "ERROR: $APP is missing $needed -- it has not been assembled."
        echo "       Run ./macos_build.sh."
        exit 1
    fi
done

# Same staleness test the regression suite uses: CMake links femm_gui
# straight into the bundle (MACOSX_BUNDLE), so it is always current after a
# build, while everything else in there was COPIED by macos_build.sh. If
# femm_gui is the newer of the two, somebody ran ninja without reassembling
# and this would ship the previous build's solvers.
if [ "$APP/Contents/MacOS/femm_gui" -nt "$APP/Contents/MacOS/femm_headless" ]; then
    echo "ERROR: $APP is stale -- femm_gui is newer than the copies inside it."
    echo "       Run ./macos_build.sh; ninja alone does not reassemble the bundle."
    exit 1
fi

# ---- Stage what ships -------------------------------------------------
# The app, a symlink to /Applications, and a copy of the examples.
#
# No loose EXECUTABLES: those would sit outside the app's signature with no
# way to staple a notarisation ticket to them, which is why lua4 lives in
# the bundle (d801ffc). The examples are different -- they are data, and
# Gatekeeper's unidentified-developer gate is about launching code, so a
# quarantined .fem or .lua is just a file.
#
# They have to be loose, because they are WORKED ON. The FEMM idiom is
#
#     open("Roters-Ch9Fig6.fem")   -- relative to the script's directory
#     mi_saveas("temp.fem")        -- never modify the original
#
# and both -run and the GUI's script toolbar set the working directory to
# the script's own. Inside a read-only bundle the saveas fails ("Could not
# save to ...") and the example cannot run at all. All three of the bundled
# Lua examples do exactly this. On Windows they work in place because
# c:\femm-qt\examples is user-writable; a folder the user drags out of the
# DMG is the same thing.
#
# Copied FROM the bundle rather than re-extracted from femm-qt.iss, so
# there is still only one manifest. Contents/Resources/examples keeps the
# read-only masters, which costs half a megabyte and means the examples are
# recoverable after the DMG is thrown away.
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "Staging..."
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp -R "$APP/Contents/Resources/examples" "$STAGE/Examples"
chmod -R u+w "$STAGE/Examples"

mkdir -p "$OUT_DIR"
rm -f "$DMG"

# ---- Build the image -------------------------------------------------
# UDZO is the usual compressed read-only format. -srcfolder takes the
# staging directory as the volume's contents.
echo "Creating $DMG ..."
hdiutil create \
    -volname "$VOLNAME" \
    -srcfolder "$STAGE" \
    -fs HFS+ \
    -format UDZO \
    -quiet \
    "$DMG"

echo
echo "Wrote $DMG ($(du -h "$DMG" | cut -f1))"
echo
echo "Contents:"
hdiutil attach "$DMG" -nobrowse -quiet -mountpoint "$STAGE/mnt"
ls -1 "$STAGE/mnt" | sed 's/^/  /'
hdiutil detach "$STAGE/mnt" -quiet
echo
echo "This DMG is ad-hoc signed only. Downloaded on another Mac it will be"
echo "refused by Gatekeeper as an unidentified developer -- see the header"
echo "of this script for the Developer ID steps that fix that."
