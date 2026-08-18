# Special-method dispatch for user-defined classes, and the bulk-data buffer
# path. Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/ProxyDunderTest.py
#
# Why this exists: a class that stands in for an object living somewhere else
# -- the PyBridge proxy (docs: pybridge-implementation-plan.md), or any wrapper
# a user writes -- is only usable if the interpreter actually dispatches the
# dunders it defines. MicroPython's default set is small and the gaps are
# silent: without MICROPY_PY_ALL_SPECIAL_METHODS, `w * 2` raises TypeError
# while `w + 2` works; without MICROPY_PY_REVERSE_SPECIAL_METHODS, `2 * w`
# raises while `w * 2` works; without MICROPY_PY_DELATTR_SETATTR, `w.attr = 1`
# succeeds and does nothing.
#
# All four flags are cherry-picked onto CORE in mpconfigport.h, which means a
# future ROM-level or upstream change could take them away without anything
# else noticing. Hence this file.
#
# It also pins two *negative* facts the proxy design depends on, so that if a
# future MicroPython gains them, whoever reads this knows the design can be
# simplified: there is no object.__setattr__, and instance __dict__ is
# read-only. Together they are why proxy state cannot live on the instance
# once __setattr__ is defined.

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


def raises(label, fn, exc_name):
    try:
        fn()
        fails.append("%s: expected %s, nothing raised" % (label, exc_name))
    except Exception as e:
        if type(e).__name__ != exc_name:
            fails.append("%s: expected %s, got %s: %s"
                         % (label, exc_name, type(e).__name__, e))


_log = []


class W:
    """A stand-in for a proxy: every operation reports which dunder ran."""

    def __getattr__(self, name):       return "getattr:" + name
    def __setattr__(self, name, v):    _log.append(("setattr", name, v))
    def __delattr__(self, name):       _log.append(("delattr", name))
    def __call__(self, *a, **k):       return ("call", a, tuple(sorted(k.items())))
    def __getitem__(self, k):          return "getitem:%r" % (k,)
    def __setitem__(self, k, v):       _log.append(("setitem", k, v))
    def __delitem__(self, k):          _log.append(("delitem", k))
    def __len__(self):                 return 3
    def __contains__(self, x):         return x == 7
    def __bool__(self):                return True
    def __iter__(self):                return iter([1, 2, 3])
    def __enter__(self):               return "entered"
    def __exit__(self, *e):            return False
    def __repr__(self):                return "<W>"
    def __str__(self):                 return "W!"
    def __add__(self, o):              return "add"
    def __radd__(self, o):             return "radd"
    def __sub__(self, o):              return "sub"
    def __rsub__(self, o):             return "rsub"
    def __mul__(self, o):              return "mul"
    def __rmul__(self, o):             return "rmul"
    def __truediv__(self, o):          return "truediv"
    def __rtruediv__(self, o):         return "rtruediv"
    def __pow__(self, o):              return "pow"
    def __neg__(self):                 return "neg"
    def __abs__(self):                 return "abs"
    def __eq__(self, o):               return "eq"
    def __ne__(self, o):               return "ne"
    def __lt__(self, o):               return "lt"
    def __le__(self, o):               return "le"
    def __gt__(self, o):               return "gt"
    def __ge__(self, o):               return "ge"


w = W()

# --- attribute protocol --------------------------------------------------
check("__getattr__", w.anything, "getattr:anything")

del _log[:]
w.title = "x"
check("__setattr__ dispatches", _log, [("setattr", "title", "x")])
check("__setattr__ suppressed the local store", w.title, "getattr:title")

del _log[:]
del w.title
check("__delattr__ dispatches", _log, [("delattr", "title")])

# --- call, subscript, container ------------------------------------------
check("__call__", w(1, k=2), ("call", (1,), (("k", 2),)))
check("__getitem__", w["k"], "getitem:'k'")
del _log[:]
w["k"] = 5
del w["k"]
check("__setitem__/__delitem__", _log, [("setitem", "k", 5), ("delitem", "k")])
check("__len__", len(w), 3)
check("__contains__", 7 in w, True)
check("__bool__", bool(w), True)
check("__iter__", [x for x in w], [1, 2, 3])

# --- with, repr ----------------------------------------------------------
with w as v:
    check("__enter__", v, "entered")
check("__repr__", repr(w), "<W>")
check("__str__", str(w), "W!")

# --- arithmetic, forward and reverse -------------------------------------
check("__add__", w + 1, "add")
check("__radd__", 1 + w, "radd")
check("__sub__", w - 1, "sub")
check("__rsub__", 1 - w, "rsub")
check("__mul__", w * 2, "mul")
check("__rmul__", 2 * w, "rmul")
check("__truediv__", w / 2, "truediv")
check("__rtruediv__", 2 / w, "rtruediv")
check("__pow__", w ** 2, "pow")
check("__neg__", -w, "neg")
check("__abs__", abs(w), "abs")

# --- comparison ----------------------------------------------------------
check("__eq__", w == 1, "eq")
check("__lt__", w < 1, "lt")
check("__le__", w <= 1, "le")
check("__gt__", w > 1, "gt")
check("__ge__", w >= 1, "ge")

# --- storing internal state on a class that forwards __setattr__ ----------
# object.__setattr__ and object.__delattr__ come with MICROPY_PY_DELATTR_SETATTR
# (objobject.c) -- the same flag that makes __setattr__ dispatch at all. So a
# proxy can hold its own state on the instance, CPython-style, and does not
# need an id()-keyed side table. Before the flag there was no way in, which is
# what the earlier PyBridge draft designed around.
class S:
    def __init__(self, h):
        object.__setattr__(self, "_h", h)     # bypasses our own __setattr__

    def __getattr__(self, name):
        return "forwarded-get:" + name

    def __setattr__(self, name, v):
        _log.append(("fwd-set", name, v))


del _log[:]
s = S(42)
check("object.__setattr__ stores on the instance", s._h, 42)
s.title = "x"
check("other assignments still forward", _log, [("fwd-set", "title", "x")])
check("internal state survives a forwarded set", s._h, 42)
check("a miss still reaches __getattr__", s.figure, "forwarded-get:figure")

# instance __dict__ stays read-only, flag or no flag -- object.__setattr__ is
# the way in, not dict mutation.
class Plain:
    def __init__(self):
        self.x = 1


def _dict_store():
    o = Plain()
    o.__dict__["k"] = 5


check("instance __dict__ is readable", Plain().__dict__, {"x": 1})
raises("instance __dict__ is read-only", _dict_store, "TypeError")

# --- bulk data: memoryview over a ulab array must be a view, not a copy ---
import array

import numpy as np

a = array.array("d", [1.0, 2.0, 3.0])
mv = memoryview(a)
check("memoryview(array) length", len(bytes(mv)), 24)

v = np.linspace(0.0, 4.0, num=5)
mvv = memoryview(v)
check("memoryview(ndarray) bytes", len(bytes(mvv)), 40)
check("ndarray.tobytes agrees", len(v.tobytes()), 40)
# The point of the flag: writing through the view changes the array itself.
# If this were a copy, v[0] would still be 0.
mvv[0] = 9.0
check("memoryview(ndarray) is a view, not a copy", v[0], 9.0)

# tobytes() refuses a non-dense array, so the codec has to densify a slice
# before taking the fast path (pybridge plan §5.4).
sliced = np.linspace(0.0, 1.0, num=6)[::2]
raises("tobytes rejects a non-dense slice", sliced.tobytes, "ValueError")
check("densified slice encodes", len(np.array(sliced).tobytes()), 24)

if fails:
    print("proxy dunder check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("proxy dunder check: full special-method dispatch incl. reverse ops "
          "and __setattr__; memoryview over ulab is a view")
