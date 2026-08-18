# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
#
# re -- the built-in engine, with its worst gap closed and its missing module
# functions supplied.
#
# This SHADOWS the built-in `re`: `python/` is on sys.path and a module there
# wins over a built-in of the same name. The engine itself is still the C one,
# reached here as `ure` (MicroPython's own alias for it), so matching is no
# slower -- only compilation passes through this file, and compiled patterns
# are cached.
#
# WHY IT EXISTS. The engine (re1.5) treats `{` as an ORDINARY CHARACTER, so a
# counted repetition compiles without complaint and then never matches:
#
#     re.search(r"\d{3}", "abc123")     ->  None      (CPython: '123')
#     re.search(r"a{2}",  "aa")         ->  None      (CPython: 'aa')
#     re.search(r"a{2}",  "a{2}")       ->  'a{2}'    (CPython: None)
#
# No error and no warning -- a ported script quietly stops finding things, and
# the third line shows it can match the WRONG thing rather than nothing. That
# is the failure mode worth spending a module on. `expand_braces()` below
# rewrites `x{m,n}` into repeated atoms before the engine sees it, so the
# pattern means what it says.
#
# WHAT IS STILL MISSING, and deliberately not faked:
#
#   * the flags -- IGNORECASE, MULTILINE, DOTALL, VERBOSE and the I/M/S/X
#     aliases. The engine has no notion of them, and a name that exists but
#     does nothing would be a fresh silent-wrong-answer bug of exactly the kind
#     this module is here to remove. They stay absent, so using one is an
#     immediate AttributeError.
#   * lookahead `(?=)`, negative lookahead `(?!)`, lookbehind `(?<=)`, named
#     groups `(?P<n>)` and inline flags `(?i)`. The engine rejects these at
#     compile time with a clear error, which is the right outcome; nothing here
#     tries to emulate them.
import ure as _engine

__all__ = ["compile", "match", "search", "fullmatch", "sub", "subn", "split",
           "findall", "finditer", "escape", "purge", "error"]

# The engine raises ValueError for a pattern it cannot compile. CPython raises
# re.error; naming it here lets `except re.error` work either way.
error = ValueError

_MAX_REPEAT = 1000          # a single {n}
_MAX_EXPANSION = 20000      # the whole rewritten pattern


# ---------------------------------------------------------------------------
# {m,n} -> repeated atoms
# ---------------------------------------------------------------------------

def _atom_start(pat, end):
    """Index where the atom ending at `end` (exclusive) begins, or -1.

    Walks BACKWARDS from the quantifier, because that is the only direction in
    which "the thing being repeated" is defined: a group, a character class, an
    escape, or one plain character.
    """
    if end <= 0:
        return -1
    i = end - 1
    ch = pat[i]

    if ch == ")":
        depth = 0
        while i >= 0:
            c = pat[i]
            if c == ")" and not _escaped(pat, i):
                depth += 1
            elif c == "(" and not _escaped(pat, i):
                depth -= 1
                if depth == 0:
                    return i
            i -= 1
        return -1

    if ch == "]":
        while i >= 0:
            if pat[i] == "[" and not _escaped(pat, i):
                return i
            i -= 1
        return -1

    if _escaped(pat, i):        # the char is the tail of an escape like \d
        return i - 1
    return i


def _escaped(pat, i):
    """True if pat[i] is preceded by an odd number of backslashes."""
    n = 0
    j = i - 1
    while j >= 0 and pat[j] == "\\":
        n += 1
        j -= 1
    return n % 2 == 1


def _read_quantifier(pat, i):
    """Parse `{m}`, `{m,}` or `{m,n}` at pat[i]; return (m, n, end) or None.

    None means "not a quantifier", and the brace is then left alone as a
    literal -- which is what CPython does with, say, `a{foo}`.
    """
    j = i + 1
    lo = ""
    while j < len(pat) and "0" <= pat[j] <= "9":
        lo += pat[j]
        j += 1
    if not lo:
        return None
    if j < len(pat) and pat[j] == "}":
        return int(lo), int(lo), j + 1
    if j >= len(pat) or pat[j] != ",":
        return None
    j += 1
    hi = ""
    while j < len(pat) and "0" <= pat[j] <= "9":
        hi += pat[j]
        j += 1
    if j >= len(pat) or pat[j] != "}":
        return None
    return int(lo), (int(hi) if hi else None), j + 1


def expand_braces(pat):
    """Rewrite every `x{m,n}` as repeated atoms. Exposed for testing."""
    out = []
    i = 0
    n = len(pat)
    in_class = False
    _just_expanded = False      # was the last thing emitted a {m,n} expansion?
    while i < n:
        ch = pat[i]

        if ch == "\\" and i + 1 < n:        # an escape is two characters
            out.append(pat[i:i + 2])
            _just_expanded = False
            i += 2
            continue
        if in_class:
            out.append(ch)
            if ch == "]":
                in_class = False
            i += 1
            continue
        if ch == "[":
            out.append(ch)
            in_class = True
            i += 1
            continue
        if ch != "{":
            out.append(ch)
            _just_expanded = False
            i += 1
            continue

        q = _read_quantifier(pat, i)
        if q is None:
            out.append(ch)                  # not a quantifier: a literal brace
            _just_expanded = False
            i += 1
            continue
        lo, hi, end = q

        built = "".join(out)
        start = _atom_start(built, len(built))
        # A well-formed quantifier with nothing before it is an ERROR in
        # CPython, not a literal -- only MALFORMED ones (a{foo}) are literal,
        # and those never get here because _read_quantifier returned None.
        if start < 0:
            raise error("nothing to repeat at position %d" % i)
        atom = built[start:]

        # `a{1}{2}` and `a*{2}` are "multiple repeat" in CPython. After an
        # expansion the atom would be the previous repetition, and after `*`
        # or `+` it would be the quantifier character itself -- both nonsense,
        # so refuse them the way CPython does rather than inventing a meaning.
        if atom in ("*", "+", "?") or _just_expanded:
            raise error("multiple repeat at position %d" % i)

        # A CAPTURING group cannot be expanded without changing what .group(k)
        # and \1 refer to -- CPython keeps one group that holds the LAST
        # repetition, and no arrangement of copies reproduces that. Say so
        # rather than quietly renumbering the groups.
        if atom.startswith("(") and not atom.startswith("(?"):
            raise error("cannot repeat a capturing group with {%s}: write "
                        "(?:...) instead of (...), or repeat it by hand"
                        % pat[i + 1:end - 1])

        if lo > _MAX_REPEAT or (hi is not None and hi > _MAX_REPEAT):
            raise error("repeat count above %d in %r" % (_MAX_REPEAT, pat))

        out = [built[:start]]

        if hi is None:                      # {m,}
            if lo == 0:
                out.append(atom + "*")
            else:
                out.append(atom * (lo - 1) + atom + "+")
        elif hi == lo:                      # {m}
            out.append(atom * lo)
        else:                               # {m,n}
            if hi < lo:
                raise error("min repeat greater than max repeat in %r" % pat)
            out.append(atom * lo + (atom + "?") * (hi - lo))

        if sum(len(s) for s in out) > _MAX_EXPANSION:
            raise error("pattern expands beyond %d characters: %r"
                        % (_MAX_EXPANSION, pat))
        _just_expanded = True
        i = end
    return "".join(out)


# ---------------------------------------------------------------------------
# compile, with a cache
# ---------------------------------------------------------------------------

_cache = {}
_CACHE_MAX = 64


def compile(pattern):
    """Compile a pattern, expanding {m,n} first.

    Cached, because the expansion is pure Python and a script that calls
    re.search() in a loop would otherwise pay for it on every iteration --
    which is exactly what CPython's own cache is for.
    """
    hit = _cache.get(pattern)
    if hit is not None:
        return hit
    rx = _engine.compile(expand_braces(pattern))
    if len(_cache) >= _CACHE_MAX:
        _cache.clear()          # CPython empties it too rather than evicting
    _cache[pattern] = rx
    return rx


def purge():
    _cache.clear()


# ---------------------------------------------------------------------------
# the module-level functions
# ---------------------------------------------------------------------------

def match(pattern, string):
    return compile(pattern).match(string)


def search(pattern, string):
    return compile(pattern).search(string)


def fullmatch(pattern, string):
    """Match only if the pattern consumes the WHOLE string.

    Done by anchoring rather than by comparing end positions: the engine
    backtracks to satisfy a trailing `$`, so `a+` against "aaa" matches all
    three rather than stopping at the first.
    """
    return _engine.compile("(?:" + expand_braces(pattern) + ")$").match(string)


def sub(pattern, repl, string, count=0):
    return compile(pattern).sub(repl, string, count) if count \
        else compile(pattern).sub(repl, string)


def subn(pattern, repl, string, count=0):
    """As sub(), but returns (result, number_of_substitutions)."""
    n = 0
    for _ in finditer(pattern, string):
        n += 1
        if count and n >= count:
            break
    return sub(pattern, repl, string, count), n


def split(pattern, string, maxsplit=0):
    return compile(pattern).split(string, maxsplit) if maxsplit \
        else compile(pattern).split(string)


def finditer(pattern, string):
    """Every non-overlapping match, left to right, as match objects."""
    rx = compile(pattern)
    pos = 0
    n = len(string)
    while pos <= n:
        m = rx.search(string[pos:])
        if m is None:
            return
        s, e = m.span()
        yield _Shifted(m, pos)
        # An empty match would loop forever; step one character past it, which
        # is what CPython does.
        pos += e if e > s else (s + 1)


class _Shifted:
    """A match object whose offsets are relative to the whole string.

    finditer searches a suffix at a time (the engine has no `pos` argument), so
    the raw spans are relative to that suffix. Groups pass through untouched.
    """

    def __init__(self, m, offset):
        self._m = m
        self._o = offset

    def group(self, *a):
        return self._m.group(*a)

    def groups(self):
        return self._m.groups()

    def span(self, *a):
        s, e = self._m.span(*a)
        return (s + self._o if s >= 0 else s, e + self._o if e >= 0 else e)

    def start(self, *a):
        return self.span(*a)[0]

    def end(self, *a):
        return self.span(*a)[1]


def findall(pattern, string):
    """Every match as a string, or as a tuple when the pattern has groups.

    CPython's shape, which surprises people the first time: no groups gives the
    whole match, one group gives that group, several give a tuple.
    """
    out = []
    for m in finditer(pattern, string):
        try:
            groups = m.groups()
        except Exception:
            groups = ()
        if not groups:
            out.append(m.group(0))
        elif len(groups) == 1:
            out.append(groups[0] if groups[0] is not None else "")
        else:
            out.append(tuple("" if g is None else g for g in groups))
    return out


# CPython escapes exactly this set and leaves ASCII letters, digits and '_'
# alone (3.7 onward).
_SPECIAL = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f"


def escape(pattern):
    out = []
    for ch in pattern:
        if ch in _SPECIAL:
            out.append("\\" + ch)
        else:
            out.append(ch)
    return "".join(out)
