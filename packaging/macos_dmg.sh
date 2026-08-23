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

# ---- One folder to drag, and it lands in the Linux layout ------------
# Everything the user needs outside the bundle goes into a single FEMM/
# directory, so the whole install is two drags: the app onto Applications,
# and this onto Documents. Separate Examples, Bindings and Documentation
# folders at the root would be three more things to drag and three more
# things to forget.
#
# The result is deliberately identical to what ./linux_build.sh install
# produces -- ~/FEMM/{examples,docs,bindings} -- so one set of
# instructions covers both platforms, and the paths printed in the build
# guides are true on either.
#
# Writable, because it has to be: an example script does open("model.fem")
# then mi_saveas("temp.fem"), both relative to its own directory, and the
# analysis writes the model back before solving. Inside the read-only
# bundle none of that works, which is the whole reason this folder exists.
FOLDER="$STAGE/FEMM"
mkdir -p "$FOLDER/docs"
cp -R "$APP/Contents/Resources/examples" "$FOLDER/examples"
[ -d "$APP/Contents/Resources/bindings" ] &&
    cp -R "$APP/Contents/Resources/bindings" "$FOLDER/bindings"

# The documents, named explicitly rather than by wildcard: Resources also
# holds the material-library seeds and the examples/bindings trees, none of
# which belong here.
for doc in manual.pdf octavefemm.pdf femm-lua.pdf usage.nb readme.txt \
           pyfemm.md octavefemm.md femm-lua.md luafemm.md; do
    [ -f "$APP/Contents/Resources/$doc" ] &&
        cp "$APP/Contents/Resources/$doc" "$FOLDER/docs/"
done

chmod -R u+w "$FOLDER"

# ---- Read Me ---------------------------------------------------------
# A DMG that opens to an app and a folder does not say what to do with
# either. This is the macOS stand-in for what the Linux installer prints
# when it finishes.
cat > "$STAGE/Read Me.txt" <<'READYME'
FEMM 5.0
========

Two things to drag:

  1. femm_gui.app  ->  Applications
     The Applications alias is right here in this window; drop it on that.

  2. FEMM          ->  your home folder
     There is no alias for this one -- every Mac has /Applications at the
     same place, but your home folder is different for every person, so no
     shortcut can point at it, and this window has no sidebar to drop onto.

     So: in Finder's menu bar choose  Go > Home  (or press Shift-Command-H).
     That opens a window on your home folder. Drag FEMM from this window
     into that one.

     Your home folder, and NOT Documents, on purpose. macOS protects
     Documents, Desktop and Downloads: a program reading a file there needs
     your permission first, and while an app can ask you for it, something
     started from a terminal usually just fails with "Operation not
     permitted" and no way offered to fix it. Since driving FEMM from Octave,
     MATLAB, Python or a script is most of the point, the home folder -- which
     is not protected -- saves everyone that. It also keeps the examples out
     of iCloud, which would otherwise sync every solver output file you
     produce.

That second one matters. It holds the examples, and they have to live
somewhere you can write: every example script opens a model and then saves
it under another name, and running an analysis writes the model back before
solving. Inside the application itself nothing is writable, so the examples
cannot run there.

Dropping FEMM into Documents gives you:

  ~/FEMM/examples    models and scripts, for every language
  ~/FEMM/docs        the manual and the per-binding guides
  ~/FEMM/bindings    the client files for other programs

which is the same layout FEMM uses on Linux.

The manual is also in the Help menu (Help > Manual, or F1); you do not need
to go looking for the PDF.

Using FEMM from another program
-------------------------------

The bindings are client-side files loaded by Octave, MATLAB, Python, Julia,
Scilab or Mathematica. Each finds FEMM by itself, so there is nothing to
configure -- but each language has to be told where its own binding file is:

  Octave/MATLAB  addpath('~/FEMM/bindings/mfiles')
  Python         sys.path.append('~/FEMM/bindings/pyfemm')
  Julia          include("~/FEMM/bindings/juliafemm/juliafemm.jl")
  Scilab         exec('~/FEMM/bindings/scifemm/scifemm.sci')
  Mathematica    Get["~/FEMM/bindings/mathfemm/mathfemm.m"]

Set FEMM_DIR only if you keep more than one copy of FEMM and need to say
which one they should talk to.

Your materials
--------------

Materials you add in the material library editor are saved to

  ~/Library/Application Support/FEMM/

not inside the application, so they survive replacing FEMM with a newer
version. Deleting that folder restores the materials FEMM shipped with.

Uninstalling
------------

Drag femm_gui.app to the Trash. ~/FEMM and the materials folder
are yours; delete them if you want them gone.
READYME

# ---- Permissions the image will carry --------------------------------
# hdiutil copies the SOURCE FOLDER's modes onto the volume, and mktemp -d
# creates its directory 0700. That put drwx------ on the volume root, which
# Finder gets away with because it runs as the packager -- but a sandboxed
# reader does not: double-clicking Read Me.txt on the mounted image failed
# with "You don't have permission", while the file itself was a perfectly
# ordinary 644. Reported from the first real DMG build.
#
# More generally the artifact must not inherit whoever-built-it's umask.
# These three lines make it independent of that: a traversable root, content
# readable by anyone, and directories anyone can enter. u+rwX,go+rX uses
# capital X so the execute bit lands on directories only and never on data.
chmod 755 "$STAGE"
chmod -R u+rwX,go+rX "$FOLDER"
chmod 644 "$STAGE/Read Me.txt"

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
