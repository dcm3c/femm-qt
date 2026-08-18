# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# matplotlib.colors -> the worker's matplotlib.colors.  See matplotlib/__init__.py.
# Any submodule without a stub here is still reachable:
#     m = pybridge.import_module("matplotlib.tri")
import pybridge

_m = None


def __getattr__(name):
    global _m
    if _m is None:
        _m = pybridge.import_module("matplotlib.colors")
    return getattr(_m, name)
