# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# operator -- micropython-lib's, plus itemgetter.
#
# `_core.py` is micropython-lib's operator, copied VERBATIM (see
# pylib/README.md), and it is thinner than the name suggests: attrgetter plus
# nine functions (lt le gt ge eq ne mod truediv floordiv). Not even `add`.
#
# So this file supplies the rest of the everyday surface, and itemgetter above
# all -- `sorted(rows, key=itemgetter(1))` is how a table of results gets
# sorted by a column, and it is the spelling people reach for first.
#
# Same two-file split as datetime: a re-vendor stays a plain copy of _core.py
# and cannot silently drop these.
from ._core import *

_abs = abs          # the builtin, captured before the name is shadowed below


# -- arithmetic and bitwise, the forms reduce()/map() are given ---------------

def add(a, b):
    return a + b


def sub(a, b):
    return a - b


def mul(a, b):
    return a * b


def pow(a, b):
    return a ** b


def neg(a):
    return -a


def pos(a):
    return +a


def abs(a):
    return _abs(a)


def and_(a, b):
    return a & b


def or_(a, b):
    return a | b


def xor(a, b):
    return a ^ b


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
