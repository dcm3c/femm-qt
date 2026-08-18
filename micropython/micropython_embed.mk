# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker
#
# Regenerates the self-contained micropython_embed/ source package from the
# pinned upstream MicroPython tree, using THIS directory's mpconfigport.h.
#
# Usage (from micropython/):
#     make -f micropython_embed.mk
#
# Output: ./micropython_embed/ (py, extmod, shared/runtime, genhdr, port).
# Compile every .c under that tree into the femm build (see Phase 10 CMake).

# Location of the pinned upstream MicroPython repository.
MICROPYTHON_TOP = ../../micropython-1.28.0

# The embed port scans only the py/ core for qstrs. We also enable the POSIX
# VFS (extmod/vfs*.c), so add those sources to the qstr scan and they get their
# qstrs (mount, ilistdir, statvfs, ...) into genhdr/qstrdefs.generated.h.
# The sources themselves are copied into the package by build_test.sh / the
# Phase-10 CMake regen step (the stock embed package omits extmod).
SRC_QSTR += extmod/vfs.c extmod/vfs_posix.c extmod/vfs_posix_file.c extmod/vfs_reader.c

# Same for the stdlib modules enabled in mpconfigport.h: without them in the
# scan, genhdr/ has neither their qstrs (dumps, loads, sha256, localtime, ...)
# nor the MP_REGISTER_MODULE entries, so `import json` would fail even though
# modjson.c compiled.
SRC_QSTR += extmod/modjson.c extmod/modre.c extmod/modrandom.c \
            extmod/modbinascii.c extmod/modhashlib.c extmod/modtime.c

include $(MICROPYTHON_TOP)/ports/embed/embed.mk
