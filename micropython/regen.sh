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
UP=../micropython-1.28.0

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

# interrupt_char.c provides mp_hal_set_interrupt_char + mp_interrupt_char, used
# by micropython.kbd_intr() (enabled at ROM level CORE_FEATURES). The stock
# embed package copies only gchelper* from shared/runtime, so bundle it too.
cp "$UP"/shared/runtime/interrupt_char.c \
   micropython_embed/shared/runtime/

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
