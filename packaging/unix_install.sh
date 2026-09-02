#!/usr/bin/env bash
# ======================================================================
#  unix_install.sh - install a built femm-qt into the user's home
#
#  Not run directly; linux_build.sh and freebsd_build.sh call it for
#  their `install` argument.  Usage:
#
#      packaging/unix_install.sh <femm-qt-root> <build-dir> [prefix]
#
#  Installs what is already in <build-dir>/bin.  It does NOT build:
#  you install once and build many times, and an install that forced a
#  rebuild would be a nuisance.  It fails clearly if there is nothing to
#  install.
#
#  ---- Why the user's home, and not /usr/local ----------------------
#
#  femm-qt resolves almost everything relative to the running executable
#  (exeDir() in libfemm/script_callbacks.cpp): the solvers, the python/
#  scripting assets, the PyBridge worker, the manual, and the four
#  material libraries.  A self-contained directory therefore installs
#  with no code changes at all, which is exactly what the Windows
#  installer does -- c:\femm-qt, PrivilegesRequired=lowest, never
#  Program Files.
#
#  It also keeps the material libraries and the examples WRITABLE, which
#  they have to be: the material editor saves the libraries, and every
#  example script opens a model and immediately saves it under another
#  name.  Under /usr/local both would be read-only, and each would need
#  its own copy-into-the-user's-home resolver first.
#
#  An FHS install (/usr/local/bin plus /usr/local/share/femm) is a
#  reasonable thing to want, and it is what a distribution packager
#  would need.  It now exists, as CMake install() rules -- see the top of
#  CMakeLists.txt -- so this script no longer has to try to be that too,
#  and refuses to (see the root check below).
# ======================================================================
set -eu

ROOT="${1:?usage: unix_install.sh <femm-qt-root> <build-dir> [prefix] [--modify-path]}"
BUILD="${2:?usage: unix_install.sh <femm-qt-root> <build-dir> [prefix] [--modify-path]}"
shift 2

# --modify-path is OPT-IN, and deliberately so. Shell startup files are
# personal configuration: they get managed in git repos, synced with chezmoi or
# stow, and on institutional machines they live on an NFS home and are policed.
# An installer that edits one without being asked produces a dirty diff at
# best, and at worst is silently reverted by the next sync -- so the failure is
# invisible until PATH mysteriously stops working. Set against that, the cost
# of NOT editing is that the user reads one printed line and adds it, which is
# cheap and obvious. rustup made the same call and it is the pattern nobody
# complains about.
PREFIX="$HOME/.local"
MODIFY_PATH=0
for arg in "$@"; do
    case "$arg" in
        --modify-path)    MODIFY_PATH=1 ;;
        --no-modify-path) MODIFY_PATH=0 ;;
        -*) echo "unix_install.sh: unknown option $arg" >&2; exit 2 ;;
        *)  PREFIX="$arg" ;;
    esac
done

# This installs for exactly ONE user. Besides the program directory it writes
# documents and examples into $HOME, and optionally edits a login profile --
# all of which belong to whoever runs it. Run under sudo those land in ROOT's
# home while the program itself is shared, so every other user on the machine
# gets a program with no examples and no documents, and the uninstall manifest
# records one user's personal paths for everybody.
#
# That is not a warning worth printing and continuing past, because the result
# looks like it worked. CMake's install() rules do the shared case properly.
if [ "$(id -u)" = "0" ]; then
    echo "unix_install.sh: refusing to run as root." >&2
    echo >&2
    echo "This installer sets up ONE user's environment: it writes examples and" >&2
    echo "documents into \$HOME and may edit your shell profile. As root those go" >&2
    echo "to /root while the program is shared, which is not what you want." >&2
    echo >&2
    echo "For a system-wide or packaged install, use the CMake rules instead:" >&2
    echo "    cmake --install build --prefix /usr/local" >&2
    echo "(DESTDIR is honoured, for building a package.)" >&2
    echo >&2
    echo "For a single user, run this WITHOUT sudo -- the default prefix is" >&2
    echo "~/.local and needs no privileges." >&2
    exit 1
fi

BIN="$BUILD/bin"
APPDIR="$PREFIX/opt/femm-qt"

if [ ! -x "$BIN/femm_gui" ] && [ ! -x "$BIN/femm_gui.exe" ]; then
    echo "ERROR: no femm_gui in $BIN -- build first."
    exit 1
fi
for needed in femm_headless fkn belasolv hsolv csolv; do
    if [ ! -e "$BIN/$needed" ]; then
        echo "ERROR: $BIN/$needed is missing -- the build is incomplete."
        exit 1
    fi
done

echo "Installing femm-qt into $APPDIR"

# ---- 1. The program directory ---------------------------------------
# Everything from bin/ verbatim: the executables, the material
# libraries, python/, pybridge-worker/, the Lua client assets and the
# manual. exeDir() then finds all of it exactly as it does in the build
# tree, because the layout is the same.
rm -rf "$APPDIR"
mkdir -p "$APPDIR"
cp -R "$BIN/." "$APPDIR/"

# ---- 2. Examples, bindings and documents ----------------------------
bash "$ROOT/packaging/stage_extras.sh" "$APPDIR" "$ROOT"

# The examples must stay writable: an example script does
#   open("model.fem")  then  mi_saveas("temp.fem")
# both relative to its own directory.
chmod -R u+w "$APPDIR/examples"

# ---- 2b. ... and a copy somewhere a human can find ------------------
# The program directory is deliberately out of the way, and on Ubuntu it is
# worse than out of the way: GNOME Files hides ~/.local outright, so the
# examples are unreachable from a file manager without knowing to press
# Ctrl+H. Windows never has this problem -- c:\femm-qt is in plain sight --
# and macOS solves it by handing the examples over as a loose folder in the
# DMG. This is the Linux equivalent of that folder.
#
# The shipped examples are REPLACED on every install, exactly like the
# documents, and unlike almost anything else a program puts in your home
# directory. They are official reference material: when one of them is wrong
# -- coilgun.py opened a model that was never shipped, open1.py wrote to an
# absolute path on the maintainer's machine, both fixed in 535945e -- the fix
# has to reach people who already installed. Preserving edits would mean
# nobody ever gets a corrected example again.
#
# Keeping edits was tried and is actively wrong here, for a reason specific to
# this program: *_analyze saves the document before solving, so MERELY RUNNING
# an example rewrites its .fem. Any "has the user modified it?" test therefore
# says yes for every example anyone has ever opened, and updates would stop
# reaching precisely the people using them.
#
# What is never touched is anything NOT in the manifest -- your own models,
# your own scripts, and the temp.fem/temp.ans the examples themselves drop
# here. Only the listed files are written, and every example script's second
# line is mi_saveas("temp.fem"), so working in a copy is the habit they teach.
#
# xdg-user-dir gives the localised name, so this lands in ~/Dokumente for a
# German desktop rather than creating an English "Documents" beside it. It
# prints $HOME when the XDG directories are not configured, which is a fine
# answer too; ~/Documents and then $HOME are the fallbacks when the tool is
# absent altogether.
DOCS=""
if command -v xdg-user-dir >/dev/null 2>&1; then
    DOCS=$(xdg-user-dir DOCUMENTS 2>/dev/null || echo "")
fi
[ -n "$DOCS" ] && [ -d "$DOCS" ] || DOCS="$HOME/Documents"
[ -d "$DOCS" ] || DOCS="$HOME"
WORKDIR="$DOCS/FEMM/examples"

mkdir -p "$WORKDIR"
ex_n=0
for f in "$APPDIR/examples"/*; do
    [ -f "$f" ] || continue
    if cp -f "$f" "$WORKDIR/"; then
        ex_n=$((ex_n + 1))
    fi
done
chmod -R u+w "$WORKDIR"
echo "  your copy: $ex_n refreshed in $WORKDIR"

# ---- 2c. The documents, likewise where they can be seen -------------
# The manual, the OctaveFEMM and Lua references and the MathFEMM notebook are
# staged into the program directory, which is to say into a folder Ubuntu's
# file manager refuses to display. A manual nobody can open is not a manual.
#
# Same idea as the examples with the opposite refresh rule: nobody edits a
# PDF, so these are overwritten on every install and are therefore always the
# ones that match the program you just installed. The examples are the user's
# and are never touched; the documents are ours and are always fresh.
#
# manual.pdf stays in the program directory as well -- Help > Manual opens
# applicationDirPath()/manual.pdf and would not find it here.
DOCDIR="$DOCS/FEMM/docs"
mkdir -p "$DOCDIR"
n_docs=0
for doc in "$APPDIR/manual.pdf" \
           "$APPDIR/octavefemm.pdf" \
           "$APPDIR/femm-lua.pdf" \
           "$APPDIR/usage.nb" \
           "$APPDIR/readme.txt" \
           "$APPDIR/pyfemm.md" \
           "$APPDIR/octavefemm.md" \
           "$APPDIR/femm-lua.md" \
           "$APPDIR/luafemm.md"; do
    if [ -f "$doc" ]; then
        cp -f "$doc" "$DOCDIR/"
        n_docs=$((n_docs + 1))
    fi
done

# ...and this platform's build guide, which is also where the install and
# uninstall instructions live -- the thing you want after an install, not
# before it.
case "$(uname -s)" in
    FreeBSD) guide="$ROOT/docs/freebsd-build.pdf" ;;
    *)       guide="$ROOT/docs/linux-build.pdf" ;;
esac
if [ -f "$guide" ]; then
    cp -f "$guide" "$DOCDIR/"
    n_docs=$((n_docs + 1))
fi
echo "  documents: $n_docs in $DOCDIR"

# ---- 3. Somewhere on PATH -------------------------------------------
# ~/.local/bin is on PATH by default on current distributions. Symlinks
# rather than copies, so an upgrade of the program directory is picked
# up without re-linking.
mkdir -p "$PREFIX/bin"

# The GUI and the headless server, but also the four solvers and the Lua 4
# interpreter, because each of those is a usable command in its own right:
# `fkn model` solves model.fem and writes model.ans, which is the natural
# thing to drive from a Makefile or a batch queue, and lua4 is a stock Lua
# interpreter meant to be run directly. Nothing in solvers/ resolves exeDir,
# so they are position-independent; femm-lua.sh finds its own folder through
# readlink -f "$0", and everything else that cares reads /proc/self/exe, which
# the kernel resolves. Linking them is therefore safe in every case.
links=""
for exe in femm_gui femm_headless fkn belasolv hsolv csolv lua4; do
    if [ -e "$APPDIR/$exe" ]; then
        ln -sf "$APPDIR/$exe" "$PREFIX/bin/$exe"
        links="$links $exe"
    fi
done
if [ -f "$APPDIR/femm-lua.sh" ]; then
    ln -sf "$APPDIR/femm-lua.sh" "$PREFIX/bin/femm-lua"
    links="$links femm-lua"
fi

# ---- 4. Desktop entry and icon --------------------------------------
mkdir -p "$PREFIX/share/applications"
mkdir -p "$PREFIX/share/icons/hicolor/256x256/apps"
[ -f "$ROOT/gui/icons/femm.png" ] &&
    cp "$ROOT/gui/icons/femm.png" \
       "$PREFIX/share/icons/hicolor/256x256/apps/femm-qt.png"

cat > "$PREFIX/share/applications/femm-qt.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=FEMM
GenericName=Finite Element Method Magnetics
Comment=2D electromagnetic, electrostatic, heat flow and current flow analysis
Exec=$APPDIR/femm_gui %f
Icon=femm-qt
Terminal=false
StartupWMClass=femm_gui
Categories=Science;Engineering;Physics;
Keywords=FEM;magnetics;electrostatics;finite element;
DESKTOP

# Harmless if absent, and the entry shows up without it on most desktops.
command -v update-desktop-database >/dev/null 2>&1 &&
    update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

# The icon theme is cached, and a newly created hicolor directory is exactly
# the case the cache has not seen. Also harmless where the tool is absent.
command -v gtk-update-icon-cache >/dev/null 2>&1 &&
    gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true


echo
echo "Installed."
echo "  program     $APPDIR"
echo "  commands    $PREFIX/bin --$links"
echo "  menu entry  $PREFIX/share/applications/femm-qt.desktop"
echo "  examples    $WORKDIR"
echo "  documents   $DOCDIR"

# ---- 5. Make those commands reachable -------------------------------
# "It installed, and femm_gui: command not found" is the likeliest support
# question this script can produce, so it handles the PATH itself rather than
# leaving it to the reader. Three cases, and only one of them is an edit:
#
#   already on PATH ....... nothing to do.
#   the login profile puts it there ....... also nothing to do. Debian and
#       Ubuntu ship a ~/.profile that adds ~/.local/bin IF IT EXISTS AT LOGIN,
#       and it exists now, so the next login picks it up. Appending our own
#       line would duplicate what the distro already does.
#   neither ....... append one line, once, marked so it can be found again.
#       This is the custom-prefix case, and FreeBSD, whose ~/.profile has no
#       such snippet.
#
# What NO installer can do is fix the shell it was launched from: a child
# process cannot change its parent's environment. So the current terminal
# always needs a new shell or a re-source, and that much is announced.
#
# Pass --modify-path to have the line added for you; without it the
# installer only tells you what to add.
#
# The login profile, and NOT ~/.bashrc, though that would make "open a new
# terminal" true (it is read by every interactive bash shell). Decided
# 2026-08-22, and the deciding case is headless use over ssh: an interactive
# `ssh host` IS a login shell and reads ~/.profile, while Debian's ~/.bashrc
# opens with `case $- in *i*) ;; *) return;; esac` and so bails out of
# anything non-interactive. ~/.bashrc would have helped the case that already
# works and missed the one that matters. (`ssh host femm_headless -run s.py`
# reads neither -- that wants a full path, and no installer can fix it.)
# WHICH startup file the shell will actually read is the whole question, and
# it is not the same as "the login profile":
#
#   zsh   .zprofile is read by LOGIN shells only, and a terminal window on a
#         desktop is interactive NON-login, so it never sees it. .zshrc is
#         read by both, so that is the only reliable target. Reported from
#         GhostBSD: the line went into .zprofile and echo $PATH never showed
#         it. Logging out would not have helped either -- nothing reads
#         ~/.zprofile for you the way a Linux session reads ~/.profile.
#   fish  config.fish is read by every fish shell.
#   sh/bash  ~/.profile is a login file, but the graphical session reads it at
#         login and every terminal inherits that environment, so it works
#         after a logout -- which is what the message below says.
#
# csh/tcsh get no file at all: they read none of the above, and
# `export VAR=value` is not their syntax. Writing there would be a silent
# no-op reported as success, so they are told what to add instead.
shellname="$(basename "${SHELL:-sh}")"
case "$shellname" in
    zsh)       profile="$HOME/.zshrc" ;;
    fish)      profile="$HOME/.config/fish/config.fish" ;;
    csh|tcsh)  profile="" ;;
    *)         profile="$HOME/.profile" ;;
esac

case ":${PATH-}:" in
    *":$PREFIX/bin:"*) path_state=live ;;
    *)                 path_state=absent ;;
esac

if [ "$path_state" = absent ] && [ -n "$profile" ]; then
    # Does that file already arrange it? Either explicitly, or via the
    # Debian/Ubuntu snippet, which is conditional on the directory existing.
    if [ -f "$profile" ] && grep -q "$PREFIX/bin" "$profile"; then
        path_state=in_profile          # a line naming it outright -- ours, or
                                       # one the user wrote. Nothing to add.
    elif [ "$PREFIX" = "$HOME/.local" ] && [ -f "$profile" ] &&
         grep -q "local/bin" "$profile"; then
        path_state=distro_snippet      # Debian/Ubuntu's conditional block
    fi
fi

if [ "$path_state" = absent ] && [ -z "$profile" ]; then
    path_state=absent_csh
fi

if [ "$path_state" = absent ] && [ "$MODIFY_PATH" -eq 1 ]; then
    mkdir -p "$(dirname "$profile")"
    {
        echo ""
        echo "# added by femm-qt install"
        if [ "$shellname" = fish ]; then
            echo "fish_add_path $PREFIX/bin"
        else
            echo "export PATH=\"$PREFIX/bin:\$PATH\""
        fi
    } >> "$profile"
    path_state=edited
fi

# How the user picks this up. NOT "open a new terminal": ~/.profile and
# ~/.zprofile are LOGIN files, and a terminal window on a desktop is an
# interactive non-login shell, so it never re-reads them -- the graphical
# session read the profile once, when you logged in. Verified on Ubuntu: a
# fresh terminal still could not find femm_gui, and only logging out and back
# in worked. (On a text console or over ssh a new login would do it, but that
# is not how most people will meet this.) fish is the exception: config.fish
# is read by every fish shell, so there a new terminal really is enough.
# Which advice is true depends on the FILE, not the shell. ~/.profile is read
# once at login by the desktop session; .zshrc and config.fish are read by
# every interactive shell, where a new terminal genuinely is enough.
case "$profile" in
    "$HOME/.profile")
        reload_where="Log out and back in, or for this shell only:"
        reload_cmd=". $profile" ;;
    *config.fish)
        reload_where="Open a new terminal, or for this one:"
        reload_cmd="source $profile" ;;
    *)
        reload_where="Open a new terminal, or for this one:"
        reload_cmd=". $profile" ;;
esac

echo
case "$path_state" in
    live)
        ;;
    in_profile)
        echo "$PREFIX/bin is not on this shell's PATH, but $profile already"
        echo "adds it. $reload_where"
        echo "    $reload_cmd"
        ;;
    distro_snippet)
        echo "$PREFIX/bin is not on this shell's PATH, but your $profile"
        echo "already adds it -- it only checks at login, and the directory did"
        echo "not exist until just now. $reload_where"
        echo "    $reload_cmd"
        ;;
    edited)
        echo "Added $PREFIX/bin to your PATH in $profile."
        echo "$reload_where"
        echo "    $reload_cmd"
        ;;
    absent)
        echo "$PREFIX/bin is not on your PATH, so those commands will not work"
        echo "in a terminal yet. Add this line to $profile:"
        if [ "$shellname" = fish ]; then
            echo "    fish_add_path $PREFIX/bin"
        else
            echo "    export PATH=\"$PREFIX/bin:\$PATH\""
        fi
        echo "or re-run the install with --modify-path and it will be added for"
        echo "you, in a marked block. Nothing is written to your shell files"
        echo "unless you ask."
        ;;
    absent_csh)
        echo "$PREFIX/bin is not on your PATH, and your shell is $shellname,"
        echo "which reads neither ~/.profile nor ~/.zshrc -- so nothing was"
        echo "changed for you. Add this to ~/.cshrc:"
        echo "    set path = ( \$path $PREFIX/bin )"
        ;;
esac
if [ "$path_state" != live ]; then
    echo "(The menu entry and the desktop icon work either way -- they run the"
    echo " program by its full path.)"
fi
echo
# ---- 6. Write down what was done, for the uninstaller ---------------
# LAST, deliberately: it records $path_state and $profile, which step 5
# above is what sets. Placed before step 5 it aborted the whole install
# under `set -u` -- after the files were in place but before the PATH was
# dealt with, so the install looked complete and silently was not.
# The install makes choices that cannot be re-derived with confidence later:
# which solver links it managed to create, whether it edited a login profile,
# and above all where the documents went -- that comes from xdg-user-dir,
# which is localised and which the user may reconfigure. An uninstaller that
# guessed would either leave files behind or delete the wrong ones, so it
# reads this instead. Written last, when every decision has been made.
{
    echo "# femm-qt install manifest -- read by packaging/unix_uninstall.sh."
    echo "# Delete this and the uninstaller will refuse to touch anything."
    echo "dir $APPDIR"
    echo "dir $DOCDIR"
    for c in $links; do echo "link $PREFIX/bin/$c"; done
    echo "file $PREFIX/share/applications/femm-qt.desktop"
    echo "file $PREFIX/share/icons/hicolor/256x256/apps/femm-qt.png"
    echo "keep $WORKDIR"
    [ "$path_state" = edited ] && echo "profile $profile"
} > "$APPDIR/uninstall-manifest.txt"

B="$APPDIR/bindings"
echo "The language bindings are in $B."
echo "They locate femm-qt on PATH by themselves -- nothing to set up for that."
echo "What each language still needs is its own search path pointed at the"
echo "binding file, which is a separate thing and cannot be done from here:"
echo "  Octave/MATLAB  addpath('$B/mfiles')"
echo "                 (put it in ~/.octaverc, or MATLAB's pathtool, to persist)"
echo "  Python         sys.path.append('$B/pyfemm')  -- or pip install pyfemm-qt"
echo "  Julia          include(\"$B/juliafemm/juliafemm.jl\")"
echo "  Scilab         exec('$B/scifemm/scifemm.sci')"
echo "  Mathematica    Get[\"$B/mathfemm/mathfemm.m\"]"
echo "  Lua            femm-lua, already on PATH"
echo "FEMM_DIR still overrides which femm-qt they talk to, if you have several."
echo
echo "The examples are in $WORKDIR."
echo "The shipped ones are replaced on every install, so corrections reach"
echo "you; anything else you keep in there is left alone. Work in a copy --"
echo "which is what every example script does on its second line."
case "$(uname -s)" in
    FreeBSD) build_script="freebsd_build.sh" ;;
    *)       build_script="linux_build.sh" ;;
esac
echo "To uninstall:  ./$build_script uninstall"
echo "It removes the program, the commands, the menu entry and the documents,"
echo "and leaves $WORKDIR alone -- it may hold work of"
echo "your own alongside the shipped examples."
