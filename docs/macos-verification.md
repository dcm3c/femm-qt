# Verifying femm-qt on macOS

macOS is the platform femm-qt has never been built or run on. Everything else
(Windows, Ubuntu 24.04) is exercised regularly; this is the gap.

Work through it **in order** — a build failure hides everything after it, and a
packaging failure makes the runtime checks meaningless.

---

## 1. Does it build

```bash
cd femm-qt
bash macos_build.sh
```

Report any compiler error **verbatim**, including the file and line. The
following have never been compiled by clang, so they are where a first failure
is most likely:

| file | what has never compiled |
|---|---|
| `micropython/femm_modtime.c` | the whole `#else` branch — `localtime_r`, `mktime`, `femm_epoch_utc_offset`. The Windows branch is the exercised one. |
| `micropython/micropython_embed/extmod/vfs.c`, `vfs_posix_file.c` | patch 07's POSIX side: a plain `open()` with `(void)no_translate`. |
| `micropython/micropython_embed/shared/runtime/sys_stdio_mphal.c` | newly vendored (it defines `sys.stdout`/`stderr`/`stdin`). Reached by the CMake glob, but new. |
| `libfemm/py_os.cpp` | `os.getenv`'s POSIX branch. |
| `libfemm/py_bind.cpp` | the two-argument `OSError(errno, msg)` path. |
| `libfemm/py_state.cpp` | `exec_python_file` now opens `"rb"` and resizes to the byte count actually read. |
| `libfemm/pybridge_host.cpp` | `_NSGetEnviron()` and `SO_NOSIGPIPE`, which compile on **no** other platform. `SO_NOSIGPIPE` is macOS's only SIGPIPE defence — it has no `MSG_NOSIGNAL`. |

Two missing includes of exactly this shape were already fixed pre-emptively
(`<cstdlib>` in `py_os.cpp`, `<cstring>` in `py_bind.cpp`). Both compiled on
MinGW purely by luck of transitive includes, so **look for more of that shape**
rather than assuming those were the last two.

## 2. Does it run

```bash
cd femm-qt/testing
bash _verify_all.sh
```

The harness derives every path from its own location, so it needs no editing
(it was Windows-only until 2026-08-18). Expect ~30 rows, all reporting
agreement. `PyBridge*` rows SKIP cleanly if no usable CPython 3 is installed —
that is not a failure.

Four rows are new since the last platform pass, and two of them are more
informative than they look:

- **`DateTime`** leans hardest on patch 06 (the local/UTC clock fix). Note that
  macOS `localtime_r` accepts pre-1970 timestamps where Windows' `localtime_s`
  refuses them, so `femm_epoch_utc_offset`'s fallback path will **not** be
  taken here. Passing means the common path is right, not that the fallback is.
- **`Csv`** and **`RunScriptSize`** should pass *trivially*, because macOS text
  mode does not translate line endings at all — the bugs they pin (`\r\r\n`
  from `newline=""`, and a CRLF script mis-sized by `ftell`) cannot occur.
  So a failure here does **not** mean a platform quirk; it means the
  `newline=""` patch or the script-loader rewrite broke something real.
- **`Pylib`** covers 22 stdlib modules; a failure names the module.

## 3. Does it ship

`macos_build.sh` copies `bin/python/` and `bin/pybridge-worker/` into
`femm_gui.app/Contents/MacOS/` — **that step has never executed.** Check:

```bash
ls build/bin/femm_gui.app/Contents/MacOS/python
ls build/bin/femm_gui.app/Contents/MacOS/pybridge-worker
ls build/bin/femm_gui.app/Contents/Resources/LICENSE-micropython-lib.txt
codesign --verify --deep --strict build/bin/femm_gui.app && echo signature ok
```

The signature check matters: `.py` files now sit inside `Contents/MacOS/`,
which is the code directory. If codesign objects, the fix is a placement
question, not a build one — say so rather than deleting the files.

Then prove `exeDir()` finds them from *inside the bundle* (not from `build/bin`,
which would pass for the wrong reason):

```bash
build/bin/femm_gui.app/Contents/MacOS/femm_headless -run /tmp/check.py
```

with `/tmp/check.py`:

```python
from datetime import datetime
print("datetime:", datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
import csv, glob, re
print("re braces:", re.search(r"\d{3}", "abc123").group(0))   # must be '123'
import matplotlib          # needs a CPython with matplotlib; skip if absent
print("matplotlib:", matplotlib.__version__)
```

## 4. PyBridge specifically

The original reason macOS mattered. `testing/PyBridgeInterruptTest.py` is the
one that would catch a `SO_NOSIGPIPE` mistake — a worker dying mid-call should
raise in the script, not kill femm-qt.

---

## Reporting back

Most useful, in order: the **first** compiler error verbatim; then any
`_verify_all.sh` row that is not agreement, with its full line; then whether
the bundle contains `python/` and passes codesign. A screenshot of a plotted
figure from inside the .app is the single best "it works" signal, since it
exercises the bundle layout, the bridge, and the GUI at once.
