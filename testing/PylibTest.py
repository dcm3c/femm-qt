# The pure-Python stdlib batch in pylib/. Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/PylibTest.py
#
# datetime and csv have their own suites (they are big); this covers the rest:
# the modules taken verbatim from micropython-lib, the two that needed pieces
# adding (itertools.groupby, operator.itemgetter), and glob, which was written
# here because no MicroPython implementation exists.
#
# It also pins sys.stdout/sys.stderr, which are not a module at all but are
# what `logging` and a great deal of ordinary Python need -- and which did not
# exist until MICROPY_PY_SYS_STDFILES was switched on.
#
# Every expected value is CPython 3.13's, so this file runs under CPython too
# (`python testing/PylibTest.py`) as a cross-check. It fails there in exactly
# the places femm-qt deliberately differs, and nowhere else:
#
#   * print's default separator -- a tab here, a space there (FEMM's own
#     convention, inherited from the Lua original);
#   * the eight re flags, which CPython HAS and femm-qt deliberately does not,
#     because the engine cannot honour them;
#   * repeating a capturing group with {m,n}, which CPython supports and
#     femm-qt refuses rather than renumbering the groups;
#   * string.Template, which CPython has and micropython-lib does not ship.
#
# Any OTHER failure under CPython means the assertion is wrong, not the module.

import os
import sys

import contextlib
import copy
import fnmatch
import functools
import glob
import itertools
import operator
import shutil
import tempfile
import traceback

fails = []


def check(label, got, want):
    if got != want:
        fails.append("%s: got %r, want %r" % (label, got, want))


# --- sys.stdout / sys.stderr ------------------------------------------------
check("sys.stdout exists", hasattr(sys, "stdout"), True)
check("sys.stderr exists", hasattr(sys, "stderr"), True)
check("sys.stdout.write returns a count", sys.stdout.write(""), 0)
# print's keywords, which the femm-qt override used to swallow. The default
# separator stays a TAB -- that is FEMM's convention, not an oversight.
check("print sep default is a tab", True, True)          # exercised below


class _Cap:
    def __init__(self):
        self.s = []

    def write(self, t):
        self.s.append(t)
        return len(t)

    def value(self):
        return "".join(self.s)


cap = _Cap()
print("a", "b", file=cap)
print("c", "d", sep="-", end="", file=cap)
check("print(file=) reaches the object", cap.value(), "a\tb\nc-d")

# --- copy -------------------------------------------------------------------
src = {"name": "coil", "turns": [1, 2, 3], "nested": {"x": [4]}}
shallow = copy.copy(src)
deep = copy.deepcopy(src)
src["turns"].append(4)
check("copy.copy shares the inner list", shallow["turns"], [1, 2, 3, 4])
check("copy.deepcopy does not", deep["turns"], [1, 2, 3])
src["nested"]["x"].append(5)
check("deepcopy is recursive", deep["nested"]["x"], [4])

# --- functools / operator ---------------------------------------------------
check("reduce", functools.reduce(lambda a, b: a + b, [1, 2, 3, 4]), 10)
check("reduce with initial", functools.reduce(lambda a, b: a + b, [], 7), 7)
check("partial", functools.partial(lambda a, b: a - b, 10)(3), 7)
check("operator.add", operator.add(2, 3), 5)
# itemgetter is the femm-qt addition -- upstream has attrgetter but not this,
# and sorting a table of results by a column is what people actually write.
rows = [("c", 3, 0.5), ("a", 1, 2.5), ("b", 2, 1.5)]
check("itemgetter one index", operator.itemgetter(1)(rows[0]), 3)
check("itemgetter several", operator.itemgetter(0, 2)(rows[0]), ("c", 0.5))
check("sort by column",
      [r[0] for r in sorted(rows, key=operator.itemgetter(1))], ["a", "b", "c"])
check("methodcaller", operator.methodcaller("upper")("ab"), "AB")


class _P:
    def __init__(self):
        self.v = 9


check("attrgetter still there", operator.attrgetter("v")(_P()), 9)

# --- itertools --------------------------------------------------------------
check("count+islice", list(itertools.islice(itertools.count(3), 4)), [3, 4, 5, 6])
check("chain", list(itertools.chain([1], [2, 3])), [1, 2, 3])
check("accumulate", list(itertools.accumulate([1, 2, 3])), [1, 3, 6])
# groupby is the femm-qt addition, and the reason for it: a flat sweep becomes
# per-parameter runs.
data = [("A", 1), ("A", 2), ("B", 3), ("A", 4)]
check("groupby consecutive runs",
      [(k, [v for _, v in g]) for k, g in itertools.groupby(data, key=operator.itemgetter(0))],
      [("A", [1, 2]), ("B", [3]), ("A", [4])])
check("groupby after sorting",
      [(k, len(list(g))) for k, g in
       itertools.groupby(sorted(data, key=operator.itemgetter(0)),
                         key=operator.itemgetter(0))],
      [("A", 3), ("B", 1)])
check("groupby with no key", [k for k, _ in itertools.groupby([1, 1, 2, 2, 1])],
      [1, 2, 1])
check("groupby on empty", list(itertools.groupby([])), [])
check("product", list(itertools.product([1, 2], "ab")),
      [(1, "a"), (1, "b"), (2, "a"), (2, "b")])
check("product repeat", len(list(itertools.product([0, 1], repeat=3))), 8)
check("permutations", list(itertools.permutations([1, 2, 3], 2)),
      [(1, 2), (1, 3), (2, 1), (2, 3), (3, 1), (3, 2)])
check("combinations", list(itertools.combinations([1, 2, 3], 2)),
      [(1, 2), (1, 3), (2, 3)])
check("compress", list(itertools.compress("abcd", [1, 0, 1, 0])), ["a", "c"])
check("filterfalse", list(itertools.filterfalse(lambda x: x % 2, range(6))),
      [0, 2, 4])
check("takewhile", list(itertools.takewhile(lambda x: x < 3, [1, 2, 3, 1])),
      [1, 2])
check("dropwhile", list(itertools.dropwhile(lambda x: x < 3, [1, 2, 3, 1])),
      [3, 1])
check("zip_longest", list(itertools.zip_longest([1, 2, 3], "ab", fillvalue="-")),
      [(1, "a"), (2, "b"), (3, "-")])

# --- fnmatch ----------------------------------------------------------------
check("fnmatch", fnmatch.fnmatch("coil.fem", "*.fem"), True)
check("fnmatch negative", fnmatch.fnmatch("coil.ans", "*.fem"), False)
check("fnmatch filter", fnmatch.filter(["a.fem", "b.ans", "c.fem"], "*.fem"),
      ["a.fem", "c.fem"])

# --- contextlib -------------------------------------------------------------
# It imports ucontextlib, which lives in a DIFFERENT micropython-lib directory
# (micropython/ rather than python-stdlib/) -- vendoring one without the other
# is why this used to fail at import.
_log = []


@contextlib.contextmanager
def _marker(name):
    _log.append("enter " + name)
    try:
        yield name
    finally:
        _log.append("exit " + name)


with _marker("a") as m:
    _log.append("body " + m)
check("contextmanager order", _log, ["enter a", "body a", "exit a"])

_log = []
try:
    with _marker("b"):
        raise ValueError("boom")
except ValueError:
    _log.append("caught")
check("contextmanager unwinds on error", _log, ["enter b", "exit b", "caught"])

# --- traceback --------------------------------------------------------------
try:
    raise RuntimeError("a script failure")
except RuntimeError as e:
    text = "".join(traceback.format_exception(type(e), e, None))
check("traceback names the error", "RuntimeError" in text and "a script failure" in text,
      True)

# --- tempfile / shutil / glob ----------------------------------------------
root = tempfile.mkdtemp()
try:
    check("mkdtemp made a directory", os.path.isdir(root), True)
    names = ["coil.fem", "magnet.fem", "coil.ans", "notes.txt", ".hidden.fem"]
    for n in names:
        f = open(root + "/" + n, "w")
        f.write("x")
        f.close()
    os.mkdir(root + "/sub")
    f = open(root + "/sub/deep.fem", "w")
    f.write("x")
    f.close()

    got = sorted([os.path.basename(p) for p in glob.glob(root + "/*.fem")])
    check("glob *.fem", got, ["coil.fem", "magnet.fem"])
    check("glob does not match a leading dot", ".hidden.fem" in got, False)
    check("glob ? wildcard",
          sorted([os.path.basename(p) for p in glob.glob(root + "/coil.???")]),
          ["coil.ans", "coil.fem"])
    check("glob [seq]",
          sorted([os.path.basename(p) for p in glob.glob(root + "/[cm]*.fem")]),
          ["coil.fem", "magnet.fem"])
    check("glob a literal name",
          [os.path.basename(p) for p in glob.glob(root + "/notes.txt")],
          ["notes.txt"])
    check("glob no match is an empty list", glob.glob(root + "/*.nope"), [])
    check("glob into a subdirectory",
          [os.path.basename(p) for p in glob.glob(root + "/sub/*.fem")],
          ["deep.fem"])
    check("glob a directory component with a wildcard",
          [os.path.basename(p) for p in glob.glob(root + "/s*/*.fem")],
          ["deep.fem"])
    rec = sorted([os.path.basename(p)
                  for p in glob.glob(root + "/**/*.fem", recursive=True)])
    check("recursive ** finds both levels", rec,
          ["coil.fem", "deep.fem", "magnet.fem"])
    check("iglob is an iterator", sorted([os.path.basename(p) for p in
                                          glob.iglob(root + "/*.fem")]),
          ["coil.fem", "magnet.fem"])
    check("has_magic", (glob.has_magic("*.fem"), glob.has_magic("a.fem")),
          (True, False))
    check("escape", glob.glob(glob.escape(root) + "/coil.fem") != [], True)

    # shutil, on the same tree
    shutil.copyfile(root + "/coil.fem", root + "/copy.fem")
    check("shutil.copyfile", os.path.exists(root + "/copy.fem"), True)
finally:
    shutil.rmtree(root)
check("shutil.rmtree removed it", os.path.exists(root), False)

# --- the rest of the drop-in batch ------------------------------------------
# Vendored verbatim and needing nothing from femm-qt, but "it imports" is not
# the same as "it works", and these are the calls a script would actually make.
import abc
import argparse
import base64
import bisect
import heapq
import inspect
import pathlib
import pickle
import pprint
import stat
import string

check("pathlib join", str(pathlib.Path("m") / "coil.fem").replace("\\", "/"),
      "m/coil.fem")
check("pathlib name/stem/suffix",
      (pathlib.Path("m/coil.fem").name, pathlib.Path("m/coil.fem").stem,
       pathlib.Path("m/coil.fem").suffix), ("coil.fem", "coil", ".fem"))
check("string constants", (string.digits, string.ascii_lowercase[:3]),
      ("0123456789", "abc"))
check("pprint.pformat", pprint.pformat({"a": 1}), "{'a': 1}")
check("stat", (stat.S_ISDIR(0o040755), stat.S_ISREG(0o100644)), (True, True))
check("bisect", (bisect.bisect_left([1, 3, 5, 7], 5),
                 bisect.bisect_right([1, 3, 5, 7], 5)), (2, 3))
_L = [1, 3, 5]
bisect.insort(_L, 4)
check("bisect.insort", _L, [1, 3, 4, 5])
check("heapq nsmallest/nlargest",
      (heapq.nsmallest(2, [5, 1, 3, 2]), heapq.nlargest(2, [5, 1, 3, 2])),
      ([1, 2], [5, 3]))
_h = []
heapq.heappush(_h, 3)
heapq.heappush(_h, 1)
check("heapq heap order", heapq.heappop(_h), 1)
_raw = bytes([1, 255, 0, 128])
check("base64 b64 round trip", base64.b64decode(base64.b64encode(_raw)), _raw)
check("base64 b16encode", base64.b16encode(_raw), b"01FF0080")
check("base64 b32 round trip", base64.b32decode(base64.b32encode(_raw)), _raw)
check("base64 urlsafe", base64.urlsafe_b64encode(bytes([251, 255])), b"-_8=")
check("pickle dict round trip",
      pickle.loads(pickle.dumps({"a": [1, 2]}))["a"], [1, 2])
check("pickle tuple round trip",
      pickle.loads(pickle.dumps((1, "x", 2.5))), (1, "x", 2.5))
check("inspect isfunction/isclass",
      (inspect.isfunction(lambda: 0), inspect.isclass(int)), (True, True))

_ap = argparse.ArgumentParser()
_ap.add_argument("--turns", type=int)
_ap.add_argument("--name")
_ns = _ap.parse_args(["--turns", "250", "--name", "coil"])
check("argparse parses", (_ns.turns, _ns.name), (250, "coil"))


class _AbstractBase(abc.ABC):
    pass


check("abc gives a subclassable ABC",
      issubclass(type("D", (_AbstractBase,), {}), _AbstractBase), True)

# --- logging and textwrap, the two that needed the interpreter fixed first ---
# logging writes to sys.stderr, which did not exist until MICROPY_PY_SYS_STDFILES
# was enabled; and upstream leaves Handler.formatter None, so a FileHandler
# added the ordinary way raised AttributeError until pylib supplied the default
# CPython has.
import logging

_LOG = "_pylibtest.log"
_lg = logging.getLogger("femmtest")
_fh = logging.FileHandler(_LOG)
_lg.addHandler(_fh)
_lg.setLevel(logging.INFO)
_lg.info("solving %s", "coil.fem")
_fh.close()
_f = open(_LOG)
_logged = _f.read()
_f.close()
os.remove(_LOG)
check("logging FileHandler needs no explicit formatter",
      "solving coil.fem" in _logged, True)

# textwrap is femm-qt's own: micropython-lib's needs regex lookahead AND
# re.MULTILINE, and this interpreter's re has neither.
import textwrap

_T = ("The finite element mesh is refined until the element size falls below "
      "the requested tolerance everywhere in the model.")
check("textwrap.wrap width", [len(l) <= 40 for l in textwrap.wrap(_T, 40)],
      [True] * len(textwrap.wrap(_T, 40)))
check("textwrap.wrap first line", textwrap.wrap(_T, 40)[0],
      "The finite element mesh is refined until")
check("textwrap.fill rejoins", textwrap.fill(_T, 40),
      "\n".join(textwrap.wrap(_T, 40)))
check("textwrap indents",
      textwrap.wrap(_T, 40, initial_indent="* ", subsequent_indent="  ")[0],
      "* The finite element mesh is refined")
# CPython fills the CURRENT line before breaking a long word, which is the
# part that is easy to get wrong.
check("textwrap breaks a long word after filling the line",
      textwrap.wrap("short " + "x" * 50, 20),
      ["short xxxxxxxxxxxxxx", "x" * 20, "x" * 16])
check("textwrap.dedent", textwrap.dedent("    a\n      b\n\n    c\n"),
      "a\n  b\n\nc\n")
check("textwrap.indent", textwrap.indent("a\nb\n\nc\n", "> "),
      "> a\n> b\n\n> c\n")
check("textwrap.shorten", textwrap.shorten(_T, 40),
      "The finite element mesh is refined [...]")

# --- re: the brace bug, and the module functions that were missing ----------
# pylib/re shadows the built-in engine to fix one silent-wrong-answer bug: the
# engine treats `{` as an ordinary character, so \d{3} compiled fine and never
# matched. Everything here is checked against CPython 3.13.
import re

check("braces: counted repeat matches", re.search(r"\d{3}", "abc123").group(0),
      "123")
check("braces: exact count", re.search(r"a{2}", "aa").group(0), "aa")
check("braces: a literal brace is no longer matched by a{2}",
      re.search(r"a{2}", "a{2}"), None)
check("braces: range", re.search(r"a{2,3}", "xaaay").group(0), "aaa")
check("braces: open-ended", re.search(r"a{1,}", "aaa").group(0), "aaa")
check("braces: zero-or-more form", re.search(r"a{0,}", "aaa").group(0), "aaa")
check("braces: on a class", re.search(r"[0-9]{4}", "year 2026").group(0), "2026")
check("braces: on a non-capturing group",
      re.search(r"(?:ab){2}", "abab").group(0), "abab")
check("braces: too few to match", re.search(r"a{3}", "aa"), None)
check("braces: anchored", re.search(r"^\d{4}$", "20260"), None)
check("braces: an escaped brace stays literal",
      re.search(r"x\{2}", "x{2}").group(0), "x{2}")
check("braces: a malformed one stays literal",
      re.search(r"a{foo}", "a{foo}").group(0), "a{foo}")
# CPython rejects these two; so do we, rather than inventing a meaning.
for bad in (r"{2}", r"a{1}{2}"):
    try:
        re.search(bad, "aa")
        fails.append("re should have rejected %r" % bad)
    except re.error:
        pass
# A capturing group cannot be expanded without renumbering the groups, so it
# is refused with an error that says what to write instead.
try:
    re.search(r"(ab){2}", "abab")
    fails.append("re should refuse to repeat a capturing group")
except re.error as e:
    check("capturing-group message names the fix", "(?:...)" in str(e), True)

check("findall no groups", re.findall(r"\d+", "a1 b22 c333"), ["1", "22", "333"])
check("findall one group", re.findall(r"(\d+)", "x12y34"), ["12", "34"])
check("findall several groups", re.findall(r"(\w)(\d)", "a1 b2"),
      [("a", "1"), ("b", "2")])
check("findall empty matches", re.findall(r"a*", "baac"), ["", "aa", "", ""])
check("finditer spans", [m.span() for m in re.finditer(r"\d+", "a1 b22")],
      [(1, 2), (4, 6)])
check("split", re.split(r"\s+", "a  b   c"), ["a", "b", "c"])
check("split maxsplit", re.split(r",", "a,b,c,d", 2), ["a", "b", "c,d"])
check("subn counts", re.subn(r"a", "X", "aaa"), ("XXX", 3))
check("subn with a limit", re.subn(r"a", "X", "aaa", 2), ("XXa", 2))
check("subn with no match", re.subn(r"z", "X", "aaa"), ("aaa", 0))
check("fullmatch whole string", re.fullmatch(r"a+", "aaa").group(0), "aaa")
check("fullmatch rejects a prefix", re.fullmatch(r"a+", "aaab"), None)
check("fullmatch with braces", re.fullmatch(r"\d{4}", "2026").group(0), "2026")
check("escape", re.escape("a.b"), "a\\.b")
check("escape round trips through search",
      re.search(re.escape("a+b"), "x a+b y").group(0), "a+b")
check("purge empties the cache", (re.purge(), len(re._cache))[1], 0)

# The flags are deliberately ABSENT rather than present-and-inert: a name that
# exists but does nothing would be a fresh silent-wrong-answer bug.
for flag in ("IGNORECASE", "MULTILINE", "DOTALL", "VERBOSE", "I", "M", "S", "X"):
    check("re has no %s (deliberate)" % flag, hasattr(re, flag), False)

# The ordinary things the engine already did must be untouched.
check("re still groups", re.match(r"(\w+)_(\d+)", "coil_42").groups(),
      ("coil", "42"))
check("re still subs with a backreference",
      re.sub(r"(\w+)@(\w+)", r"\2:\1", "a@b"), "b:a")
check("re still subs with a function",
      re.sub(r"\d+", lambda m: "N", "a1b22"), "aNbN")
check("re still does non-greedy", re.search(r"<.*?>", "<a><b>").group(0), "<a>")
# ...including for the modules that import it.
import fnmatch as _fn
check("fnmatch is unaffected by the shadow",
      (_fn.fnmatch("coil.fem", "*.fem"), _fn.fnmatch("coil.ans", "*.fem")),
      (True, False))

# Documented subset, so that a later reader knows it is deliberate rather than
# broken: micropython-lib's string is the constants plus translate(). CPython
# also has Template, Formatter and capwords. The constants are what `string` is
# actually used for, so it is left as upstream ships it.
check("string has no Template (documented subset)", hasattr(string, "Template"),
      False)

if fails:
    print("pylib check: %d FAILED" % len(fails))
    for msg in fails:
        print("   " + msg)
else:
    print("pylib check: 22 stdlib modules behave, re handles {m,n} and has "
          "findall/split/subn/fullmatch/escape, glob walks a real tree, "
          "textwrap matches CPython, and sys.stdout exists")
