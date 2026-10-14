# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# pandas, as femm-qt sees it: a forwarder to the real one in the CPython
# worker (see pybridge.py). Nothing of pandas is shipped or reimplemented
# here -- this file exists so that `import pandas as pd` finds something, and
# so that a pasted example from the pandas documentation runs unchanged.
#
# `from pandas import DataFrame` works too: MicroPython consults a module's
# __getattr__ for a from-import.
#
# WHY A SHIM AND NOT AN IMPLEMENTATION. pandas is 635,000 lines of Python on
# top of 13 MB of compiled Cython, and that Cython is written against the
# *CPython* C API, which MicroPython does not have. It is not a port, it is a
# rewrite, and it would sit on numpy's C API -- the thing ulab replaces rather
# than implements. There is no in-process pandas to shadow (unlike
# numpy/scipy, which ARE ulab here and must NOT be shimmed), so forwarding
# costs nothing and loses nothing.
#
# pandas is NOT installed for you. If the bridge interpreter lacks it the
# import fails at first use with a message naming that interpreter and the
# exact pip line -- see import_module() in pybridge.py.
#
# The trap that message exists for: discovery prefers an interpreter that can
# also PLOT, so a venv with pandas but no matplotlib loses to one with
# matplotlib but no pandas -- and FEMMQT_PYTHON is only a preference, so
# setting it at such a venv silently gets you a different python and a bare
# "No module named 'pandas'". pybridge.set_interpreter(path) is the
# authoritative one: a pin is the sole candidate and is used even when it
# cannot plot. Both measured on this build.
#
# WHAT IS NOT TRANSPARENT: a DataFrame is a proxy, not a local object.
# Scalars come back by value -- float(df["loss"].mean()) is a real float, and
# so is every element of a column you iterate -- but a Series or an ndarray
# stays server-side as a handle. To get numbers into ulab, fetch them:
#
#     arr = pybridge.fetch(df["loss"].to_numpy())   # -> ulab ndarray
#     np.mean(arr)                                  # now it is local
#
# `np.mean(df["loss"])` does not work: ulab cannot see inside a handle.
# Going the other way is transparent -- a ulab array crosses by value, so
# pd.DataFrame({"v": my_ulab_array}) is fine and takes the binary path.
#
# File HANDLES belong to the process that opened them, so
#
#     f = open("sweep.csv")                 # femm-qt's file
#     pd.read_csv(f)                        # BridgeError
#
# and the fix is either a path string (read_csv/to_csv open the file
# themselves, which is what nearly every script writes) or pybridge.open().
#
# PERFORMANCE: every attribute and call is a round trip, about 50 us. Keep
# the work server-side and pull results once -- measured on this build,
# `df["a"].sum()` over 50,000 rows costs 0.8 ms and fetching those 50,000
# floats into ulab costs 2.0 ms, but 2,000 separate `df["a"].iloc[i]` reads
# cost 345 ms. The same bulk-not-per-item rule as the rest of the bridge.
#
# Submodules are not forwarded: `import pandas.api.types` will not resolve.
# Use pybridge.import_module("pandas.api.types") for those.
#
# The worker is not started by importing this. It starts at the first
# attribute access, so a script that imports pandas and then decides not to
# use it pays nothing.
import pybridge

_m = None


def __getattr__(name):
    global _m
    if _m is None:
        _m = pybridge.import_module("pandas")
    return getattr(_m, name)
