# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# operator -- micropython-lib's, plus itemgetter.
#
# `_core.py` is micropython-lib's operator, copied VERBATIM (see
# pylib/README.md). It used to be thinner than the name suggests -- attrgetter
# plus nine comparison and division functions, not even `add` -- and this file
# was written to make up the difference. Upstream has since added the
# arithmetic and bitwise pair-functions (add, sub, mul, pow, neg, and_, or_,
# xor), so those eight came out of here when _core.py was refreshed on
# 2026-08-29; they arrive through `import *` instead.
#
# Seven of the eight upstream bodies are character-for-character what was here
# (`def add(a, b): return a + b` has one spelling -- the name and the parameter
# names are fixed by CPython's API and the body is one expression). The eighth,
# pow, differs only in spacing: `a**b` upstream against `a ** b` here. That
# lone difference is the useful evidence -- the single place style COULD vary
# is the single place the two did, which is what convergence looks like rather
# than copying.
#
# Seventeen still have no upstream, and itemgetter above all --
# `sorted(rows, key=itemgetter(1))` is how a table of results gets sorted by a
# column, and it is the spelling people reach for first. So the two-file shape
# stays; only its size changed.
#
# Same two-file split as datetime: a re-vendor stays a plain copy of _core.py
# and cannot silently drop these.
from ._core import *

_abs = abs          # the builtin, captured before the name is shadowed below


# -- arithmetic and bitwise, the forms reduce()/map() are given ---------------

def pos(a):
    return +a


def abs(a):
    return _abs(a)


def invert(a):
    return ~a


def lshift(a, b):
    return a << b


def rshift(a, b):
    return a >> b


# -- logic and identity -------------------------------------------------------

def not_(a):
    return not a


def truth(a):
    return bool(a)


def is_(a, b):
    return a is b


def is_not(a, b):
    return a is not b


# -- sequences and mappings ---------------------------------------------------

def contains(a, b):
    return b in a


def getitem(a, b):
    return a[b]


def setitem(a, b, c):
    a[b] = c


def delitem(a, b):
    del a[b]


def concat(a, b):
    return a + b


def countOf(a, b):
    n = 0
    for x in a:
        if x == b:
            n += 1
    return n


def indexOf(a, b):
    for i, x in enumerate(a):
        if x == b:
            return i
    raise ValueError("sequence.index(x): x not in sequence")


def itemgetter(*items):
    """A callable returning item(s) from its operand, as CPython's does.

    One argument returns the item; several return a tuple of them, which is
    what makes `key=itemgetter(0, 2)` a multi-column sort.
    """
    if not items:
        raise TypeError("itemgetter expected 1 argument, got 0")
    if len(items) == 1:
        item = items[0]

        def g(obj):
            return obj[item]
    else:
        def g(obj):
            return tuple(obj[i] for i in items)
    return g


def methodcaller(name, *args, **kwargs):
    """A callable that invokes a named method on its operand."""
    def caller(obj):
        return getattr(obj, name)(*args, **kwargs)
    return caller
