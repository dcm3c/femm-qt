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
#  SIGNING follows whatever macos_build.sh did, through the same two
#  environment variables -- see the header of packaging/macos_sign.sh:
#
#    nothing set            the image is unsigned, and so is the app in it.
#                           Fine for a local look at the layout; Gatekeeper
#                           refuses it anywhere else.
#
#  The FILENAME records which of those you got. Only a signed, notarised,
#  stapled image that Gatekeeper accepts is named femm-qt-5.0_<date>.dmg;
#  anything else gets -adhoc, -unnotarized or -REJECTED after the date. Ship
#  the one with nothing after the date.
#    FEMM_SIGN_ID           the image is signed with the Developer ID.
#    + FEMM_NOTARY_PROFILE  the image is notarised and stapled as well.
#
#  This is the SECOND notarisation round. macos_build.sh already notarised
#  and stapled femm_gui.app itself; this one covers the image. Both are
#  wanted: the image's ticket is what gets checked on download, and the
#  app's own ticket is what keeps working after it has been dragged to
#  /Applications and the image thrown away.
# ======================================================================
set -euo pipefail

cd "$(dirname "$0")/.."          # femm-qt/

# Reads FEMM_SIGN_ID / FEMM_NOTARY_PROFILE and sets the mode. Sourced before
# the staging work so a misconfiguration fails immediately rather than after
# hdiutil has built an image that cannot be used.
. "packaging/macos_sign.sh"

APP="build/bin/femm_gui.app"
OUT_DIR="packaging/output"
VOLNAME="FEMM 5.0"
STAMP="$(date +%d%b%Y)"

# The image is BUILT under a working name and only takes its real one at the
# very end, once what it actually IS has been established -- see "Name it for
# what it turned out to be" at the bottom of this script.
#
# The name used to be the date and nothing else, in both signing modes. That
# records WHEN an image was built and never whether it can be shipped, and
# those are not the same question: an ad-hoc image (FEMM_SIGN_ID unset) runs on
# the machine that built it and is refused by Gatekeeper everywhere else. It
# bit us twice. On the same day the ad-hoc build silently replaced a signed,
# notarised and stapled image built hours earlier -- the entire Apple round
# trip gone, no prompt, because the line below this one used to be `rm -f
# "$DMG"`. On different days both survived, and the test build sorted newest
# and looked like the newest release. packaging/output is the folder builds are
# handed over in, so its filenames are exactly where that question gets asked.
#
# The working name is dot-prefixed: hidden in Finder, plainly not a deliverable
# in ls, skipped by the *.dmg glob macos_release.sh uses, and removed by the
# trap if this script does not reach the end.
DMG_BASE="femm-qt-5.0_${STAMP}"
DMG="$OUT_DIR/.${DMG_BASE}.building.dmg"
DMG_WORK="$DMG"          # cleared once the image has been named, not before

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

# ---- The app must already carry the signature this image will claim ---
# Division of labour that is easy to get wrong: THIS script signs the
# IMAGE; macos_build.sh signs what is inside it. Run this one on its own
# against an ad-hoc bundle and the result is a Developer-ID-signed image
# wrapping an app Apple will refuse -- and the way you find out is a full
# notarisation round trip, which is tens of minutes. Check it in a second
# instead.
#
# Captured and string-matched rather than piped into `grep -q`: under
# pipefail that pipeline can come back 141 (SIGPIPE) instead of the match,
# which silently skips the check. See macos_sign.sh.
if [ "$FEMM_SIGN_MODE" = "developer-id" ]; then
    APPSIG="$(codesign -dv --verbose=2 "$APP" 2>&1 || true)"
    if [ "${APPSIG#*Signature=adhoc}" != "$APPSIG" ]; then
        echo "ERROR: $APP is ad-hoc signed, but this image would be signed"
        echo "       with a Developer ID. Notarisation would reject the app"
        echo "       inside it."
        echo "       Run ./macos_build.sh with FEMM_SIGN_ID set, first."
        exit 1
    fi
    if [ -n "$FEMM_NOTARY_PROFILE" ] &&
       ! xcrun stapler validate "$APP" >/dev/null 2>&1; then
        echo "ERROR: $APP has no stapled notarisation ticket, but"
        echo "       FEMM_NOTARY_PROFILE is set."
        echo "       Run ./macos_build.sh with the SAME variables set, so the"
        echo "       app is notarised and stapled before it goes in the image."
        exit 1
    fi
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
trap 'rm -rf "$STAGE"; [ -n "${DMG_WORK:-}" ] && rm -f "$DMG_WORK"; true' EXIT

echo "Staging..."
# ditto rather than cp -R, for what this bundle now carries rather than for
# anything cp does wrong: 54 files in Contents/MacOS hold their code
# signature in an extended attribute (see macos_sign.sh), and the stapled
# ticket is a file at Contents/CodeResources. Measured here, cp -R preserves
# both -- but it preserves xattrs as behaviour, whereas ditto preserves them
# as its contract. For the one copy that becomes the artifact people
# download, take the guarantee. A dropped xattr would not fail visibly here;
# it would produce an image whose app no longer verifies.
ditto "$APP" "$STAGE/femm_gui.app"
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

# ...and the reference subfolders stage_extras.sh put in Resources/docs: the
# file-format references and the material-library papers. Without this they
# shipped only INSIDE the bundle, where nobody looks (the format references
# were in that state from their first release until 2026-10).
for sub in formats materials; do
    [ -d "$APP/Contents/Resources/docs/$sub" ] &&
        cp -R "$APP/Contents/Resources/docs/$sub" "$FOLDER/docs/$sub"
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

Lua is different: it is not loaded into a program you already have, it IS
the program. Legacy FEMM 4.2 Lua scripts run through a launcher that ships
inside the application, with the Lua interpreter beside it:

  /Applications/femm_gui.app/Contents/MacOS/femm-lua.sh myscript.lua

Dragging an application out of a disk image cannot add anything to your
PATH, so that full path is the honest answer. If you use it often, give it
a name in your ~/.zshrc:

  alias femm-lua='/Applications/femm_gui.app/Contents/MacOS/femm-lua.sh'

An alias rather than a symlink on purpose: the launcher locates the Lua
interpreter and femm.lua relative to its own directory, and following a
symlink back to the bundle needs "readlink -f", which is not on every
system. The alias has nothing to resolve.

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

# ---- Sign, notarise and staple the image ------------------------------
# The app inside was already signed (and possibly notarised and stapled) by
# macos_build.sh. This covers the image itself, which is the thing a
# browser actually downloads and quarantines.
#
# The log is OUTSIDE $STAGE on purpose: $STAGE is what hdiutil just packaged,
# and anything dropped in there would have shipped.
SIGN_LOG="$(mktemp -t femm_dmg_sign.XXXXXX)"
# The half-built image goes too if anything below fails. Notarisation needs the
# network and Apple's servers, so this script can abort part way through more
# or less at random, and an aborted run must not leave something behind that a
# later reader could take for a finished artifact.
trap 'rm -rf "$STAGE"; rm -f "$SIGN_LOG"; [ -n "${DMG_WORK:-}" ] && rm -f "$DMG_WORK"; true' EXIT

if [ "$FEMM_SIGN_MODE" = "developer-id" ]; then
    echo "Signing the image ($FEMM_SIGN_MODE)..."
    femm_sign_dmg "$DMG" "$SIGN_LOG"

    if [ -n "$FEMM_NOTARY_PROFILE" ]; then
        echo "Notarising the image..."
        # No zip this time: notarytool takes a .dmg directly, because a disk
        # image is already the single file it wants. Only a .app has to be
        # wrapped (see macos_build.sh step 6).
        # Labelled with the name the image is ABOUT to get, not its working
        # one: these two print progress and errors, and ".femm-...building.dmg"
        # in a notarisation log helps nobody.
        femm_notarize "$DMG" "${DMG_BASE}.dmg"
        femm_staple "$DMG" "${DMG_BASE}.dmg"
    fi
fi

echo
echo "Contents:"
hdiutil attach "$DMG" -nobrowse -quiet -mountpoint "$STAGE/mnt"
ls -1 "$STAGE/mnt" | sed 's/^/  /'
hdiutil detach "$STAGE/mnt" -quiet
echo

# ---- What this image will do on someone else's Mac --------------------
# Each branch also decides what the file will be CALLED. The releasable image
# -- signed, notarised, stapled and accepted -- gets the plain name; everything
# else carries a label saying why it is not that. The folder then answers
# "which of these do I ship" on its own, without anyone having to remember
# which run each file came out of.
if [ "$FEMM_SIGN_MODE" != "developer-id" ]; then
    SUFFIX="-adhoc"
    echo "This DMG is UNSIGNED. Downloaded on another Mac it will be refused"
    echo "by Gatekeeper as an unidentified developer -- set FEMM_SIGN_ID and"
    echo "FEMM_NOTARY_PROFILE (see packaging/macos_sign.sh) to fix that."
elif [ -z "$FEMM_NOTARY_PROFILE" ]; then
    SUFFIX="-unnotarized"
    echo "Signed with a Developer ID but NOT notarised. Still refused on"
    echo "another Mac: since macOS 10.15 a Developer ID signature alone is"
    echo "not enough. Set FEMM_NOTARY_PROFILE."
else
    # The real test, and the one worth trusting: this is the assessment
    # macOS runs when the image is opened from a browser download.
    # --context context:primary-signature asks about the DMG's OWN
    # signature, which is the thing being judged at that moment.
    # Captured, not piped into grep -q -- see macos_sign.sh for why.
    GK="$(spctl -a -t open --context context:primary-signature -v "$DMG" 2>&1 || true)"
    if [ "${GK#*accepted}" != "$GK" ]; then
        SUFFIX=""
        echo "Signed, notarised, stapled, and accepted by Gatekeeper."
        echo "This image can be handed to anyone."
    else
        # Labelled rather than left with the release name. This is the one
        # failure that would otherwise look exactly like a good release: it
        # is signed, it is notarised, everything above it succeeded, and
        # Gatekeeper still refuses it.
        SUFFIX="-REJECTED"
        echo "WARNING: Gatekeeper did not accept the finished image:"
        printf '%s\n' "$GK" | sed 's/^/  /'
    fi
fi

# ---- Name it for what it turned out to be -----------------------------
# Here, at the end, rather than where the name used to be chosen at the top:
# signing, notarisation and the Gatekeeper assessment can each fail, and a name
# picked before any of them had run would describe what was intended rather
# than what happened.
DMG_FINAL="$OUT_DIR/${DMG_BASE}${SUFFIX}.dmg"
mv -f "$DMG" "$DMG_FINAL"
DMG_WORK=""              # it has a real name now; the trap leaves it alone
DMG="$DMG_FINAL"

echo
echo "Wrote $DMG ($(du -h "$DMG" | cut -f1))"
if [ -n "$SUFFIX" ]; then
    echo "The '${SUFFIX#-}' in the name is deliberate: this image is NOT the"
    echo "one to ship. A releasable build is named femm-qt-5.0_<date>.dmg with"
    echo "nothing after the date."
fi
