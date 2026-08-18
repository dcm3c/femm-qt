# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# matplotlib, as femm-qt sees it: a forwarder to the real one in the CPython
# worker (see pybridge.py).  Nothing of matplotlib is shipped or reimplemented
# here -- this file exists so that `import matplotlib` finds something, and so
# that `matplotlib.rcParams[...]` reaches the object that actually has them.
#
# The worker is not started by importing this.  It starts at the first
# attribute access, so a script that imports matplotlib and then decides not to
# plot pays nothing.
import pybridge

_m = None


def _mod():
    global _m
    if _m is None:
        _m = pybridge.import_module("matplotlib")
    return _m


def __getattr__(name):
    return getattr(_mod(), name)
