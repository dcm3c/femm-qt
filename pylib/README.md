# pylib — pure-Python standard library modules

Modules that CPython has and this interpreter does not, written in Python and
staged into `bin/python/` (which `init_py()` puts on `sys.path`) by the
`femm_pylib` CMake target. Both installers ship that directory --
`packaging/femm-qt.iss` by wildcard, `macos_build.sh` into
`Contents/MacOS/python` -- so a new module here needs no packaging change, only
a rebuild.

These are **not** PyBridge shims. `bin/python/matplotlib/` forwards every
attribute to a CPython worker over a socket; everything here runs in-process,
needs no CPython installed, and returns real values rather than proxies. The
test for which one a new module wants is roughly: is it a large native-code
dependency femm-qt has no business shipping (bridge it), or is it a few hundred
lines of ordinary Python (put it here)?

## datetime

`datetime/_core.py` is [micropython-lib][1]'s `python-stdlib/datetime/datetime.py`,
version 4.1.0, **copied verbatim**. Do not edit it. Re-vendoring is a plain file
copy:

    cp <micropython>/lib/micropython-lib/python-stdlib/datetime/datetime.py \
       femm-qt/pylib/datetime/_core.py

`datetime/_core.py` is MIT, and the notice travels with it as
`datetime/LICENSE.micropython-lib` rather than as a header inside the file --
which is what keeps the file byte-identical to upstream and the re-vendor a
plain copy. The whole directory is staged, so that notice lands in
`bin/python/datetime/` too: deliberate, and the reason a stray copy of the
directory is still a compliant one. Both installers ship that notice as `LICENSE-micropython-lib.txt`
(see `packaging/femm-qt.iss` and `macos_build.sh`); it is a *different*
copyright holder from `LICENSE-MicroPython.txt`, which covers the interpreter.
Add the same pair for any future module vendored here.

`datetime/__init__.py` is femm-qt's, and is purely additive — it imports
everything from `_core` and then attaches what upstream lacks: `strftime`,
`strptime`, `isocalendar`, `ctime`, `utcnow`, `utcfromtimestamp`. Nothing in
`_core` is overridden except `timezone.utc`, which upstream leaves unnamed. That
split is the whole point of the two-file layout: a re-vendor cannot silently
drop the additions, and `diff` against upstream stays empty.

Two things it fixes rather than adds, both found by running the tests below:

- **`NotImplemented` does not exist in MicroPython**, and `_core`'s
  `timezone.__eq__` returns it. Every equality test on a timezone-*aware*
  datetime raised `NameError`, because `datetime.__eq__` opens with
  `self._tz == None`. `__init__.py` binds the name to `False` in `_core`'s
  globals.
- **`time.localtime(t)` returned UTC.** Not a datetime bug at all — see
  `micropython/patches/06-modtime-host-timezone.patch`. `datetime.now()` was off
  by the whole UTC offset until that was fixed.

### Known divergences from CPython

- `f"{dt:%Y-%m-%d}"` does not work. MicroPython's `str.format` never calls
  `__format__`, so the interpreter rejects the spec before datetime sees it.
  Use `dt.strftime("%Y-%m-%d")`.
- `%a %A %b %B %p %c %x %X` are the C locale (English), always. There is no
  locale database in the interpreter, and CPython's own answers for these vary
  by platform and machine.
- An unknown directive passes through unchanged (`"%Q"` → `"%Q"`), matching
  glibc and macOS. CPython on *Windows* raises `ValueError` instead.
- `repr()` carries upstream's spelling, which shows the tzinfo and fold
  arguments even when they are defaults.

## csv

Written here, not vendored, because there is nothing to vendor: CPython's `csv`
is a thin Python shell (`DictReader`, `DictWriter`, `Sniffer`, the dialect
presets) around `_csv`, a **built-in C module** that holds the entire reader and
writer. micropython-lib has no csv at all.

So `csv.py` is a from-scratch implementation of the whole module. The parser is
a character state machine using CPython's own state names, because the awkward
cases -- a quote inside an unquoted field, text after a closing quote, a field
spanning lines -- are defined by nothing except what the reference
implementation does with them.

Verified by generating every observable behaviour (721 lines: 31 inputs x 9
dialects for the reader, 20 rows x 9 dialects for the writer, plus dict
wrappers, registry, errors and round trips) under both interpreters and
diffing. It is byte-identical to CPython 3.13. The four bugs it had along the
way were all in one place -- deciding whether a field needs quoting *before*
escaping it instead of during -- and all four still round-tripped through this
same module, so only the comparison caught them.

Divergences, all forced:

- `DictReader` yields an `OrderedDict`. MicroPython's dict is not
  insertion-ordered, so a plain dict would shuffle the columns; `OrderedDict` is
  a real dict subclass here, so this is *closer* to CPython 3.8+ behaviour, not
  further from it.
- `Sniffer` is an independent heuristic rather than a port. It agrees with
  CPython on all twelve realistic samples tested, including semicolon exports
  and quoted files; on a genuinely ambiguous file the two may differ, because
  there is no correct answer to agree on.
- No `__class_getitem__` (`csv.DictReader[str, str]`), which needs `types`.

**`newline=""` matters here**, and is the reason for
`micropython/patches/07-vfs-newline-arg.patch`. The writer emits `\r\n`; text
mode on Windows then translates the `\n` to `\r\n`, so every record ends
`\r\r\n` and Excel shows a blank row between each pair. That is CPython's
behaviour too -- which is exactly why `open(path, "w", newline="")` exists, and
it used to raise `TypeError` here. Now it does what CPython does, and a file
written this way is byte-identical to one CPython writes.

## Licensing, and why the shapes above exist

The shapes are not stylistic. **No vendored file is ever modified**, which is
what keeps the licensing simple:

- an upstream file stays byte-identical, carries no femm-qt copyright, and sits
  next to a `LICENSE.micropython-lib` that reproduces micropython-lib's MIT
  notice. Every directory holding upstream code has its own copy, so a stray
  copy of one directory is still a compliant copy;
- femm-qt's additions live in a **separate file** (`__init__.py` beside
  `_core.py`), carry femm-qt's own SPDX header, and change upstream behaviour
  only from the outside — by rebinding a name, never by editing the file;
- a module written here from scratch (`csv`, `glob`) is femm-qt's alone.

So there is no "modified vendored package" anywhere in `pylib/`, and no file
claims a copyright it should not. That is checkable rather than asserted —
`tests/license_audit.py` byte-compares every vendored file against the upstream
tree and looks for stray copyright claims:

    python femm-qt/pylib/tests/license_audit.py <path-to-micropython-lib>

If it ever reports a difference, one of the shapes above has been broken and
the licensing needs revisiting before the file is shipped.

The one place femm-qt *does* modify third-party code is
`micropython/micropython_embed/`, and that is handled differently: upstream's
copyright header is retained, every change is marked inline with a
`FEMM patch NN` comment, and `micropython/patches/README.md` explains each one.

## The rest of the batch

Everything else here came from micropython-lib. Three shapes, and which one a
module takes is decided by whether femm-qt had to change anything:

| shape | modules | why |
|---|---|---|
| verbatim, in `vendor/` | `abc`, `argparse`, `base64`, `bisect`, `contextlib`, `copy`, `fnmatch`, `functools`, `heapq`, `inspect`, `pathlib`, `pickle`, `pprint`, `stat`, `string`, `traceback`, `types`, `ucontextlib` | nothing needed changing |
| `_core.py` + `__init__.py` | `datetime`, `itertools`, `logging`, `operator`, `shutil`, `tempfile` | upstream was incomplete or assumed a different `os` |
| written here | `csv`, `glob`, `textwrap` | no usable MicroPython implementation exists |
| a wrapper over a built-in | `re` | the C engine is sound but has one silent-wrong-answer bug |

`vendor/` is copied wholesale into `bin/python/`, so adding another verbatim
module needs no CMake edit. The package shapes are listed one by one on
purpose: each has a femm-qt half that a careless re-vendor could drop.

What the packages add, and why each was necessary rather than nice:

- **`itertools`** — upstream is 74 lines (count, cycle, repeat, chain, islice,
  tee, starmap, accumulate). Added `groupby` above all, which is how a flat
  sweep becomes per-parameter runs, plus `product`, `permutations`,
  `combinations`, `compress`, `filterfalse`, `takewhile`, `dropwhile`,
  `zip_longest`.
- **`operator`** — upstream is `attrgetter` plus nine comparison/arithmetic
  functions. Not even `add`. Added `itemgetter` (the spelling people reach for:
  `sorted(rows, key=itemgetter(1))`), `methodcaller`, and the everyday
  arithmetic/logic/sequence set.
- **`shutil`** — upstream's `rmtree` walks with `os.ilistdir` and `disk_usage`
  wants `os.statvfs`; femm-qt's `os` has neither. `rmtree` is replaced with a
  `listdir`-based one, and `copyfile`/`copy`/`copy2`/`copytree`/`move` added —
  upstream has only `copyfileobj`, so there was no way to copy a *file*.
- **`logging`** — upstream leaves `Handler.formatter` as `None` and then calls
  `self.formatter.format(record)`, so `log.addHandler(FileHandler("run.log"))`
  — three words of ordinary Python — raised `AttributeError`. CPython falls
  back to a default `Formatter`; `__init__.py` supplies one. (It also could not
  be vendored at all until `sys.stderr` existed.)
- **`tempfile`** — upstream defaults to `/tmp`, which does not exist on
  Windows. `__init__.py` picks the directory from `TMPDIR`/`TEMP`/`TMP` before
  upstream's code runs, leaving its `mkdtemp`/`mkstemp` logic untouched.

One documented subset: **`string` is the constants plus `translate()`**, which
is all micropython-lib ships. CPython also has `Template`, `Formatter` and
`capwords`; those are absent and femm-qt does not add them, because the
constants are what `string` is actually used for. That is a deliberate gap, not
a broken module, and `PylibTest.py` asserts it so it stays deliberate.

`textwrap` is written here because upstream's cannot even be imported: its
`wordsep_re` uses regex lookahead and lookbehind, which re1.5 rejects
("regex too complex" — a construct gap, not a size limit), and its
`_whitespace_only_re` passes `re.MULTILINE`, and this `re` has no flags at all.
Both run at import, so no argument avoids them. Ours uses no regex, matches
CPython on ordinary prose, and does not break on hyphens — which is the one
feature that needed the lookahead.

**`re` is the odd one out**: it *shadows* the built-in module rather than
replacing it. `python/` is on `sys.path` and wins over a built-in of the same
name, while the C engine stays reachable inside as `ure` — so matching is
exactly as fast as before and only compilation passes through Python (cached).

It exists for one bug. The engine treats `{` as an ordinary character, so a
counted repetition compiles without complaint and then never matches:

    re.search(r"\d{3}", "abc123")   ->  None     (CPython: '123')
    re.search(r"a{2}",  "a{2}")     ->  'a{2}'   (CPython: None)

No error, no warning — a ported script quietly stops finding things, and the
second line shows it matching the *wrong* thing. `expand_braces()` rewrites
`x{m,n}` into repeated atoms before the engine sees it. A capturing group is
refused with an error naming the fix (`(?:...)`), because expanding it would
renumber the groups.

It also supplies the module functions the built-in lacks — `findall`,
`finditer`, `split`, `subn`, `fullmatch`, `escape`, `purge` — and matches
CPython on all 83 lines of the comparison corpus, including the two patterns
CPython rejects.

The flags (`IGNORECASE`, `MULTILINE`, `DOTALL`, `VERBOSE`, `I`/`M`/`S`/`X`)
are deliberately **absent**, not present-and-inert: the engine cannot honour
them, and a name that exists but silently does nothing would be exactly the
class of bug this module removes. Lookaround, named groups and inline flags
are likewise left to fail at compile, which they already do clearly.

`glob` is written here because micropython-lib's only glob is in `unix-ffi`
and calls libc. It supports `*`, `?`, `[seq]`, `[!seq]` and recursive `**`,
and matches CPython's two surprising rules: a wildcard does not match a leading
dot, and the results are unsorted.

## Testing

`testing/DateTimeTest.py`, `testing/CsvTest.py` and `testing/PylibTest.py` are
the regression tests, all three in `_verify_all.sh`. `PylibTest.py` also runs
under CPython, where exactly one check fails — print's default separator, a tab
here and a space there, which is FEMM's own convention. `CsvTest.py` also runs under CPython
(`python testing/CsvTest.py`) and passes everything there except its two
`newline=` cases, which assert a femm-qt-specific restriction -- so if a new
assertion fails under CPython, the assertion is wrong rather than the module.

The upstream conformance suite is kept here too, at
`tests/test_datetime_upstream.py` (521 tests, derived from CPython's
`datetimetester.py`). It is not part of `_verify_all.sh` because it needs
micropython-lib's `unittest`, which femm-qt does not ship. To run it after a
re-vendor, assemble a scratch directory:

    cp -r femm-qt/pylib/datetime          /tmp/dt/
    cp -r <micropython>/lib/micropython-lib/python-stdlib/unittest/unittest /tmp/dt/
    cp femm-qt/pylib/tests/test_datetime_upstream.py /tmp/dt/
    build/bin/femm_headless -run /tmp/dt/test_datetime_upstream.py

Expect **521 tests, 0 failures, 25 errors**. All 25 errors are the suite's own
`LocalTz` helper calling `os.getenv`, which femm-qt's `os` module does not have;
they are not datetime failures. Anything else is a regression.

## Provenance, for the record

`datetime/_core.py` is not a port of CPython's `Lib/datetime.py`; it is an
independent implementation by Lorenzo Cappelletti, so nothing here is under the
Python Software Foundation license. `tests/test_datetime_upstream.py` cites
CPython's `datetimetester.py` as its reference (its `--stdlib` mode runs the
same assertions against the real thing), but it too is MIT under micropython-lib
and is not shipped. `datetime/__init__.py` was written here, with CPython used
only as an oracle to diff *output* against -- no CPython source was consulted.

[1]: https://github.com/micropython/micropython-lib
