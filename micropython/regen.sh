#!/usr/bin/env bash
# Regenerate the vendored micropython_embed/ package from the pinned upstream
# MicroPython tree. Run this ONLY when bumping the MicroPython version or
# changing mpconfigport.h (normal builds just compile the committed package).
#
# Windows-local convenience (hard-codes the MinGW path); the portable
# equivalent is the Phase-10 CMake `regen-micropython` target.
set -e
cd "$(dirname "$0")"
export PATH="/c/Qt/Tools/mingw1310_64/bin:/usr/bin:$PATH"
# Where the upstream tree lives. Must match MICROPYTHON_TOP in
# micropython_embed.mk, which is relative to this same directory -- they
# disagreed until 2026-08 (UP one level too shallow), so a regen died in the
# packager before reaching the copies below.
UP=../../micropython-1.28.0

rm -rf build-embed
rm -rf micropython_embed 2>/dev/null || true

# embed.mk scans only py/ core; micropython_embed.mk adds the POSIX VFS sources
# to the qstr + root-pointer scan so genhdr/ includes their qstrs and the
# vfs_cur / vfs_mount_table root pointers.
#
# USER_C_MODULES=./ulab adds ulab (numpy/scipy ndarrays) to the qstr + module
# scan so genhdr/{qstrdefs,moduledefs}.h know about `ulab` and its symbols. The
# glob is $(USER_C_MODULES)/*/micropython.mk, which matches ./ulab/code/. ulab's
# own .c are NOT copied into the package (the embed packager only ships py/*);
# they are compiled directly by femm's CMake against the generated genhdr.
# NB: USER_C_MODULES must NOT have a leading "./". The qstr/module extractor
# mangles the source path into the per-file name (genhdr/qstr/<mangled>.qstr);
# a leading "./" becomes a leading "." so the file is HIDDEN, and the collection
# step globs "*.qstr" which skips dotfiles — silently dropping every ulab qstr
# and the module registration. Use a bare relative path so names aren't hidden.
mingw32-make -f micropython_embed.mk CC=gcc PYTHON=python SHELL=/usr/bin/sh \
    USER_C_MODULES=ulab

# The stock embed package copies only extmod/modplatform.h; bundle the POSIX
# VFS sources we enabled (MICROPY_VFS_POSIX) so the package stays self-contained.
cp "$UP"/extmod/vfs.c "$UP"/extmod/vfs.h \
   "$UP"/extmod/vfs_posix.c "$UP"/extmod/vfs_posix.h \
   "$UP"/extmod/vfs_posix_file.c "$UP"/extmod/vfs_reader.c \
   micropython_embed/extmod/

# Standard-library modules enabled in mpconfigport.h. The flag alone only tells
# the build they exist -- the packager ships py/ and nothing else from extmod,
# so each module's source has to come along too, or the link fails on the
# module object the generated moduledefs.h now references.
cp "$UP"/extmod/modjson.c "$UP"/extmod/modre.c "$UP"/extmod/modrandom.c \
   "$UP"/extmod/modbinascii.c "$UP"/extmod/modhashlib.c \
   "$UP"/extmod/modtime.c "$UP"/extmod/modtime.h \
   micropython_embed/extmod/

# re1.5 is the regex engine behind `re`. Like sha256 below, modre.c #includes
# its .c files, so they must be present but not separately compiled.
mkdir -p micropython_embed/lib/re1.5
cp "$UP"/lib/re1.5/*.c "$UP"/lib/re1.5/*.h micropython_embed/lib/re1.5/

# modhashlib.c #includes lib/crypto-algorithms/sha256.c (a .c, deliberately) --
# so the file must be present but must NOT also be compiled on its own, or every
# sha256_* symbol is defined twice. femm-qt's CMakeLists filters it out of the
# glob for exactly this reason.
mkdir -p micropython_embed/lib/crypto-algorithms
cp "$UP"/lib/crypto-algorithms/sha256.c "$UP"/lib/crypto-algorithms/sha256.h \
   micropython_embed/lib/crypto-algorithms/

# timeutils backs time.localtime/mktime; interrupt_char.c provides
# mp_hal_set_interrupt_char + mp_interrupt_char, used by micropython.kbd_intr()
# (enabled at ROM level CORE_FEATURES). The stock embed package copies only
# gchelper* from shared/runtime, so bundle both.
#
# sys_stdio_mphal.c joins them: modsys.c only DECLARES mp_sys_stdout_obj and
# friends, so without it MICROPY_PY_SYS_STDFILES cannot link and sys.stdout
# does not exist. It writes through mp_hal_stdout_tx_strn, which is the route
# print() already takes to the console.
cp "$UP"/shared/runtime/interrupt_char.c \
   "$UP"/shared/runtime/sys_stdio_mphal.c \
   micropython_embed/shared/runtime/
mkdir -p micropython_embed/shared/timeutils
cp "$UP"/shared/timeutils/timeutils.c "$UP"/shared/timeutils/timeutils.h \
   micropython_embed/shared/timeutils/

# Apply FEMM-specific patches (complex formatting, comparisons, int()).
# These are not upstream-able — see patches/README.md.
#
# The patch files carry full repo-relative paths (a/femm-qt/solvers/...), so
# they must be applied from the repo root with NO --directory. The previous
# `git apply --directory=micropython` double-prefixed the path and
# silently failed, which is how the patches got dropped in the de5dafe regen
# (objstr/objcomplex complex formatting + comparisons). Resolve the repo root
# explicitly so this works regardless of the caller's CWD.
echo "Applying FEMM patches..."
REPO_ROOT="$(git rev-parse --show-toplevel)"
for p in patches/*.patch; do
    [ -f "$p" ] || continue
    git -C "$REPO_ROOT" apply "$(pwd)/$p" && echo "  Applied $(basename "$p")" \
        || { echo "  FAILED: $(basename "$p") — manual re-apply needed"; exit 1; }
done

# Record which mpconfigport.h this package was generated against, so the CMake
# staleness guard stays quiet until the config actually changes again.
cmake -P stamp_mpconfig.cmake

echo "Regenerated micropython_embed/ (with POSIX VFS sources + FEMM patches)."
