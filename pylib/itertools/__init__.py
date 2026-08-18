# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# itertools -- micropython-lib's, plus the pieces it leaves out.
#
# `_core.py` is micropython-lib's itertools, copied VERBATIM (see
# pylib/README.md). It is a 74-line subset: count, cycle, repeat, chain,
# islice, tee, starmap, accumulate. Everything else CPython has is missing,
# and the ones added below are the ones a results-processing script actually
# reaches for -- groupby above all, which is how you turn a flat sweep into
# per-parameter runs.
#
# Same two-file split as datetime, for the same reason: a re-vendor stays a
# plain copy of _core.py and cannot silently drop these.
from ._core import *


class groupby:
    """Consecutive runs sharing a key, as (key, group) pairs.

    CPython's contract including its sharp edge: every group is drawn from the
    SAME underlying iterator, so a group is only valid until the next one is
    asked for. Sort first if equal keys are not already adjacent.

    A class rather than a generator because the groups have to share mutable
    state with the parent -- that is what makes the sharp edge sharp, and a
    version without it would quietly buffer the whole input instead.
    """

    def __init__(self, iterable, key=None):
        self.keyfunc = (lambda x: x) if key is None else key
        self.it = iter(iterable)
        self.tgtkey = self.currkey = self.currvalue = object()
        self.id = None

    def __iter__(self):
        return self

    def __next__(self):
        self.id = object()          # invalidates any group still being held
        while self.currkey == self.tgtkey:
            self.currvalue = next(self.it)      # StopIteration ends the whole
            self.currkey = self.keyfunc(self.currvalue)
        self.tgtkey = self.currkey
        return (self.currkey, self._grouper(self.tgtkey, self.id))

    def _grouper(self, tgtkey, gid):
        while self.id is gid and self.currkey == tgtkey:
            yield self.currvalue
            try:
                self.currvalue = next(self.it)
            except StopIteration:
                return
            self.currkey = self.keyfunc(self.currvalue)


def product(*iterables, repeat=1):
    """The cartesian product -- nested loops as an iterator.

    A parameter sweep over two or three variables is exactly this, and writing
    it out as nested for-loops is where the indexing mistakes live.
    """
    pools = [tuple(p) for p in iterables] * repeat
    result = [[]]
    for pool in pools:
        result = [x + [y] for x in result for y in pool]
    for prod in result:
        yield tuple(prod)


def permutations(iterable, r=None):
    pool = tuple(iterable)
    n = len(pool)
    r = n if r is None else r
    if r > n:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[k] for k in indices[:r])
                break
        else:
            return


def combinations(iterable, r):
    pool = tuple(iterable)
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != i + n - r:
                break
        else:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)


def compress(data, selectors):
    for d, s in zip(data, selectors):
        if s:
            yield d


def filterfalse(predicate, iterable):
    if predicate is None:
        predicate = bool
    for x in iterable:
        if not predicate(x):
            yield x


def takewhile(predicate, iterable):
    for x in iterable:
        if not predicate(x):
            return
        yield x


def dropwhile(predicate, iterable):
    it = iter(iterable)
    for x in it:
        if not predicate(x):
            yield x
            break
    for x in it:
        yield x


def zip_longest(*iterables, fillvalue=None):
    iters = [iter(i) for i in iterables]
    live = len(iters)
    if not live:
        return
    while True:
        row = []
        alive = 0
        for i in range(len(iters)):
            try:
                row.append(next(iters[i]))
                alive += 1
            except StopIteration:
                row.append(fillvalue)
        if not alive:
            return
        yield tuple(row)
