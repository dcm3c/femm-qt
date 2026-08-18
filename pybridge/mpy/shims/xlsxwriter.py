# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# xlsxwriter, as femm-qt sees it: a forwarder to the real one in the CPython
# worker (see pybridge.py). Nothing of xlsxwriter is shipped or reimplemented
# here -- this file exists so that `import xlsxwriter` finds something, and so
# that a pasted example from the xlsxwriter documentation runs unchanged.
#
# `from xlsxwriter import Workbook` works too: MicroPython consults a module's
# __getattr__ for a from-import, so the single entry point this library really
# has resolves either way.
#
# WHY A SHIM AND NOT AN IMPLEMENTATION. .xlsx is a zip of XML parts and a
# useful subset would be some 800 lines -- but a subset is what it would stay,
# and the real library is 26,600 lines with charts, conditional formats and
# images that nobody wants to reimplement. There is no in-process xlsxwriter
# to shadow (unlike numpy/scipy, which are ulab natively and must NOT be
# shimmed), so forwarding costs nothing and loses nothing.
#
# WHAT IS NOT TRANSPARENT: file objects. `Workbook("sweep.xlsx")` is fine --
# the worker opens the file itself, which is the form nearly every script
# uses. But a file HANDLE belongs to the process that opened it, so
#
#     f = open("sweep.xlsx", "wb")          # femm-qt's file
#     wb = xlsxwriter.Workbook(f)           # BridgeError
#
# and the fix is pybridge.open(), which opens it in the worker instead. The
# error says so.
#
# PERFORMANCE: every call is a round trip, about 118 us. Prefer write_row and
# write_column to a loop over write -- 720 single-cell writes cost ~85 ms
# where two column writes cost ~1 ms. The same bulk-not-per-item rule as the
# rest of the bridge.
#
# The worker is not started by importing this. It starts at the first
# attribute access, so a script that imports xlsxwriter and then decides not
# to write a workbook pays nothing.
import pybridge

_m = None


def __getattr__(name):
    global _m
    if _m is None:
        _m = pybridge.import_module("xlsxwriter")
    return getattr(_m, name)
