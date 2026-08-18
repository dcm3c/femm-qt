# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# zipfile, as femm-qt sees it: a forwarder to the real one in the CPython
# worker (see pybridge.py). femm-qt has no zip implementation of its own.
#
# WHY FORWARD RATHER THAN WRITE ONE. A write-only zip is about 150 lines, but a
# useful one needs DEFLATE, which means vendoring moddeflate.c plus uzlib and
# regenerating the MicroPython package -- a build step that has silently
# dropped patches before. Store-only archives are legal but roughly ten times
# larger. Forwarding costs 15 lines and gets the real thing, compression and
# reading included.
#
# THE BOUNDARY BARELY BITES HERE, which is what makes zipfile a good candidate.
# Its API is mostly PATHS, and both processes see the same filesystem:
#
#     import zipfile
#     with zipfile.ZipFile("run.zip", "w", zipfile.ZIP_DEFLATED) as z:
#         z.write("coil.fem")
#         z.write("coil.ans")
#
# All of that crosses by value. `z.writestr(name, data)` and `z.read(name)`
# carry bytes across, which is fine at any size the bridge handles (8 MB in
# ~36 ms). Only a file OBJECT cannot cross -- so pass paths, or open it in the
# worker with pybridge.open(). Cost is one round trip per call, about 118 us,
# so bundling fifty result files is single-digit milliseconds.
#
# IT IS A STANDARD-LIBRARY NAME, and that is the one thing to be careful about.
# A user expects `import zipfile` to work the way `import json` does, so the
# bare bridge error ("no usable Python 3 found") reads as a non-sequitur here
# in a way it does not for matplotlib. _explain() below re-raises with the
# reason, because the surprising part is not that Python is missing -- it is
# that a standard module needed it.
import pybridge

_m = None


def _explain(e):
    return ImportError(
        "zipfile is not built into femm-qt -- it is provided by a Python 3 "
        "installation on this machine, and none was usable.\n" + str(e))


def __getattr__(name):
    global _m
    if _m is None:
        try:
            _m = pybridge.import_module("zipfile")
        except pybridge.BridgeError as e:
            raise _explain(e)
    return getattr(_m, name)
