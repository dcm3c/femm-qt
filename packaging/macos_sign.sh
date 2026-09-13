# ======================================================================
#  macos_sign.sh - Developer ID signing, notarisation and stapling.
#
#  SOURCED, not run:  . packaging/macos_sign.sh
#
#  Used by macos_build.sh (the .app) and packaging/macos_dmg.sh (the .dmg).
#  Shared rather than duplicated because the two have to agree about the
#  identity, the flags and the failure handling, and a duplicate in this
#  tree has already drifted once (see the stage_extras.sh note in
#  macos_build.sh).
#
#  ---------------------------------------------------------------------
#  CONFIGURATION - by environment, mirroring packaging/pack.bat on Windows.
#
#  Three modes, in increasing order of how far the result travels:
#
#    nothing set            Ad-hoc signature. Runs and tests on THIS machine
#                           and nowhere else. No Apple account needed. This
#                           is the everyday build and the default.
#
#    FEMM_SIGN_ID           Developer ID, hardened runtime, secure timestamp.
#                           Everything notarisation requires except the
#                           notarisation. Use this to check that hardened
#                           runtime has not broken anything without waiting
#                           on a round trip to Apple.
#
#    + FEMM_NOTARY_PROFILE  Also notarise and staple, so the artifact opens
#                           on a Mac that has never seen it, with no network.
#
#  Nothing account-specific is committed; both values are read from the
#  environment:
#
#    export FEMM_SIGN_ID="Developer ID Application: Your Name (TEAMID)"
#    export FEMM_NOTARY_PROFILE=femm-notary
#
#  Optional: FEMM_NOTARY_TIMEOUT (default 1h) bounds how long the build
#  waits on Apple before giving up. It does not cancel the submission.
#
#  `security find-identity -v -p codesigning` lists the identity strings.
#  The notary profile is created once, interactively, and is then in the
#  keychain for good:
#
#    xcrun notarytool store-credentials femm-notary \
#          --apple-id you@example.com --team-id TEAMID
#
#  It prompts for an app-specific password -- NOT the Apple ID password --
#  which is minted at appleid.apple.com under "App-Specific Passwords".
#  `xcrun notarytool history --keychain-profile femm-notary` proves it works
#  without submitting anything.
# ======================================================================

# ---- Resolve the mode once, and say which one out loud ----------------
# Said out loud because the failure this prevents is silent: a release built
# with the variables unset is a perfectly good-looking .app that Gatekeeper
# refuses on every machine but this one.
FEMM_SIGN_ID="${FEMM_SIGN_ID:-}"
FEMM_NOTARY_PROFILE="${FEMM_NOTARY_PROFILE:-}"

if [ -n "$FEMM_SIGN_ID" ]; then
    # The two things notarisation checks for beyond the identity itself:
    # --options runtime turns on the hardened runtime, --timestamp gets a
    # secure timestamp from Apple (the one flag here that needs the network).
    FEMM_CODESIGN_ARGS="--force --options runtime --timestamp --sign"
    FEMM_SIGN_MODE="developer-id"
else
    # The everyday build deliberately gets NEITHER of those, which is a
    # choice rather than a restriction -- codesign accepts both with an
    # ad-hoc identity, it just produces flags=0x10002(adhoc,runtime) and no
    # timestamp, since an ad-hoc signature has no certificate for a
    # timestamp to attest to. Leaving them off keeps a local build behaving
    # exactly as it did before any of this existed. To test that hardened
    # runtime has not broken something, set FEMM_SIGN_ID; that is what the
    # middle mode is for.
    #
    # --timestamp=none is explicit rather than omitted so codesign never
    # reaches for the network on the path that is run a hundred times a day.
    FEMM_CODESIGN_ARGS="--force --timestamp=none --sign"
    FEMM_SIGN_ID="-"
    FEMM_SIGN_MODE="ad-hoc"
fi

if [ -n "$FEMM_NOTARY_PROFILE" ] && [ "$FEMM_SIGN_MODE" = "ad-hoc" ]; then
    echo "ERROR: FEMM_NOTARY_PROFILE is set but FEMM_SIGN_ID is not."
    echo "       An ad-hoc signature cannot be notarised -- Apple has nothing"
    echo "       to check it against. Set FEMM_SIGN_ID as well, or unset both."
    exit 1
fi

# ---- Sign one item ----------------------------------------------------
# $1 = path, $2 = log file to append noise to.
#
# codesign says "replacing existing signature" on every rebuild, which is
# benign and would otherwise bury the real output, so it goes to the log and
# only surfaces if the command actually fails.
femm_sign_item() {
    if ! codesign $FEMM_CODESIGN_ARGS "$FEMM_SIGN_ID" "$1" >>"$2" 2>&1; then
        echo "ERROR: codesign failed on $1"
        echo "--- codesign output ---"
        tail -30 "$2"
        exit 1
    fi
}

# ---- Sign a whole .app, inside out ------------------------------------
# $1 = path to the .app, $2 = log file.
#
# NOT `codesign --deep`. The man page on this machine marks it "DEPRECATED
# for signing as of macOS 13.0", and it is the wrong tool besides: --deep
# applies the OUTER bundle's flags and identifier to every nested piece, and
# its own documentation admits there are "listed locations they will not be
# signed by the --deep option". A bundle that looks signed but has an
# unsigned or mis-flagged nested binary is exactly what notarisation
# rejects, and it reports it as a list of paths rather than as "you used
# --deep".
#
# (--deep IS right for VERIFYING, where it means "check the nested code
# too" rather than "sign everything you find". macos_build.sh keeps it on
# the `codesign --verify` that follows this function.)
#
# Inside out, because each signature seals what is below it: signing the
# .app first and a framework second invalidates the .app's seal.
femm_sign_app() {
    _app="$1"
    _log="$2"
    _fw="$_app/Contents/Frameworks"

    # Frameworks first, signed AT THE VERSION DIRECTORY. A versioned
    # framework is a bundle whose _CodeSignature lives in Versions/A, so
    # signing the .framework wrapper or the bare Mach-O inside it puts the
    # signature where nothing looks for it and the outer verify fails.
    for _f in "$_fw"/*.framework; do
        [ -d "$_f" ] || continue
        if [ -d "$_f/Versions/A" ]; then
            femm_sign_item "$_f/Versions/A" "$_log"
        else
            femm_sign_item "$_f" "$_log"
        fi
    done

    # Then the loose dylibs in Frameworks/ and the Qt plugins.
    #
    # Found BY TYPE rather than from a list, so a Qt update that adds a
    # dylib is covered without editing this script -- the same reasoning as
    # the prune re-check in macos_build.sh step 2b. Anything under a
    # .framework is skipped: the loop above already signed it as part of its
    # bundle.
    # The Mach-O test captures first and matches on the string, rather than
    # the obvious `file -b ... | grep -q Mach-O`. Under `set -o pipefail`
    # that pipeline is a coin flip: grep -q exits on the first match, `file`
    # gets SIGPIPE, the pipeline reports 141, and the `if` goes FALSE -- so
    # a Mach-O silently does not get signed. It happens to win the race
    # today because `file` prints one short line and exits, which is exactly
    # the kind of thing that starts failing on a busier machine.
    # ${x#*pat} != $x is the same prefix test macos_build.sh step 2b uses.
    while IFS= read -r _m; do
        _type="$(file -b "$_m" 2>/dev/null || true)"
        if [ "${_type#*Mach-O}" != "$_type" ]; then
            femm_sign_item "$_m" "$_log"
        fi
    done < <(find "$_fw" "$_app/Contents/PlugIns" \
                  -type f -not -path "*.framework/*" 2>/dev/null)

    # Then EVERYTHING in Contents/MacOS -- and everything means the .py
    # files too, not just the Mach-O.
    #
    # This looks wrong and is not. Contents/MacOS is the code directory, and
    # codesign treats every regular file in it as a code object regardless
    # of what is actually in it. python/ and pybridge-worker/ put 54 .py and
    # text files there (init_py() and pybridge_host.cpp resolve them through
    # exeDir(), so they cannot move without a C++ change). Leave them
    # unsigned and signing the BUNDLE fails outright:
    #
    #   bin/femm_gui.app: code object is not signed at all
    #   In subcomponent: .../Contents/MacOS/python/datetime/__init__.py
    #
    # which is the man page's "signing operation will fail, unless the
    # --deep option is given". So --deep was never just a convenience here;
    # it was silently signing all 54, storing each signature in an xattr
    # because a text file has nowhere to embed one. This loop does the same
    # thing explicitly, which is what lets --deep go.
    #
    # Those xattr signatures are why the DMG stages with ditto rather than
    # cp -R: drop the xattrs and the bundle stops verifying.
    #
    # Not extended to Frameworks/ or PlugIns/ above, where the non-Mach-O
    # files are framework Resources already covered by their own bundle
    # signature.
    #
    # femm_gui itself is skipped -- signing the .app signs its own main
    # executable, and doing it here first would just be overwritten.
    while IFS= read -r _m; do
        if [ "$_m" != "$_app/Contents/MacOS/femm_gui" ]; then
            femm_sign_item "$_m" "$_log"
        fi
    done < <(find "$_app/Contents/MacOS" -type f 2>/dev/null)

    # The bundle itself, last.
    femm_sign_item "$_app" "$_log"
}

# ---- Sign a disk image ------------------------------------------------
# $1 = path to the .dmg, $2 = log file.
#
# Deliberately NOT femm_sign_item: that carries --options runtime, which is
# a property of a running process and means nothing on a disk image. The
# timestamp still matters -- it is what notarisation checks.
#
# Signing the DMG is separate from notarising it. Apple will notarise an
# unsigned image whose contents are properly signed, but an unsigned image
# fails `spctl -a -t open --context context:primary-signature`, which is
# what actually runs when someone double-clicks the download.
femm_sign_dmg() {
    if ! codesign --force --timestamp --sign "$FEMM_SIGN_ID" "$1" >>"$2" 2>&1; then
        echo "ERROR: codesign failed on $1"
        echo "--- codesign output ---"
        tail -30 "$2"
        exit 1
    fi
}

# ---- Notarise ---------------------------------------------------------
# $1 = file to submit (.zip or .dmg), $2 = label for messages.
#
# Returns only on "Accepted"; anything else prints Apple's own reasons and
# stops the build. That log call is the whole point: notarytool's summary
# says "Invalid" and nothing more, while the log names the offending path
# and what was wrong with it, which is the difference between a fixable
# failure and a guess.
femm_notarize() {
    _file="$1"
    _label="$2"

    # --wait on its own has NO time limit, which in a build script means a
    # release can hang forever on an Apple-side queue with no way to tell
    # that from slow progress. --timeout bounds it. An hour by default,
    # which is Apple's own "if it is taking longer than this, something is
    # wrong" figure rather than the 15 minutes a submission usually takes;
    # override with FEMM_NOTARY_TIMEOUT (notarytool duration syntax: 30m, 2h).
    #
    # The timeout stops US waiting, not Apple working -- the submission
    # carries on server-side, which is why the id is printed below rather
    # than discarded.
    # Worth saying out loud: this is a queue on Apple's side, and how long
    # it takes is not a function of anything local. Observed here: 25
    # minutes for a 42 MB app bundle. Silence for that long otherwise looks
    # like a hang.
    echo "  submitting $_label to Apple. This is a queue -- minutes usually,"
    echo "  but tens of minutes happens and is not a hang."
    # --no-progress because this output is captured, not watched: without it
    # notarytool emits one "Current status: In Progress" plus a growing run
    # of dots per poll, and a 25-minute wait lands in the build log as a
    # single 8 kB line that buries everything around it.
    _out="$(xcrun notarytool submit "$_file" \
                  --keychain-profile "$FEMM_NOTARY_PROFILE" --no-progress \
                  --wait --timeout "${FEMM_NOTARY_TIMEOUT:-1h}" 2>&1)" || true
    printf '%s\n' "$_out" | sed 's/^/    /'

    _status="$(printf '%s\n' "$_out" | awk -F': *' '/^ *status:/{print $2; exit}')"
    _id="$(printf '%s\n' "$_out" | awk -F': *' '/^ *id:/{print $2; exit}')"

    if [ "$_status" = "Accepted" ]; then
        return 0
    fi

    echo
    # Two different failures wearing similar output. "Invalid"/"Rejected" is
    # Apple saying no and the log says why; a timeout is Apple saying
    # nothing yet, where the log does not exist and the submission is still
    # alive. Telling them apart is the difference between "fix the bundle"
    # and "wait and staple".
    if [ "${_out#*imed out}" != "$_out" ] || [ "${_out#*imeout}" != "$_out" ] ||
       [ "$_status" = "In Progress" ]; then
        echo "ERROR: gave up waiting for notarisation of $_label after"
        echo "       ${FEMM_NOTARY_TIMEOUT:-1h}. The submission was NOT cancelled -- Apple is"
        echo "       still working on it. Pick it up with:"
        echo
        [ -n "$_id" ] && echo "         xcrun notarytool info $_id --keychain-profile $FEMM_NOTARY_PROFILE"
        echo "         xcrun stapler staple \"$_file\"    # once it reports Accepted"
        echo
        echo "       Raise FEMM_NOTARY_TIMEOUT if this is just a slow queue."
        exit 1
    fi

    echo "ERROR: notarisation of $_label came back: ${_status:-no status reported}"
    if [ -n "$_id" ]; then
        echo "--- notarytool log for $_id ---"
        xcrun notarytool log "$_id" \
              --keychain-profile "$FEMM_NOTARY_PROFILE" 2>&1 | sed 's/^/  /' || true
    fi
    exit 1
}

# ---- Staple -----------------------------------------------------------
# $1 = path to staple, $2 = label.
#
# Stapling attaches the ticket to the artifact so it validates with no
# network. Without it a first launch on a machine that has never seen the
# app has to ask Apple, and fails closed if the machine is offline.
femm_staple() {
    if ! xcrun stapler staple "$1" >/dev/null 2>&1; then
        echo "ERROR: could not staple the notarisation ticket to $2"
        xcrun stapler staple -v "$1" 2>&1 | tail -20 | sed 's/^/  /' || true
        exit 1
    fi
    echo "  ticket stapled to $2."
}
