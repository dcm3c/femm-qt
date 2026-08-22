# Verifying femm-qt on macOS

macOS was the platform femm-qt had never been built or run on. Everything else
(Windows, Ubuntu 24.04) is exercised regularly; this was the gap.

Work through it **in order** — a build failure hides everything after it, and a
packaging failure makes the runtime checks meaningless.

## Status: passed, 2026-08-18

Host: Apple M1, macOS 26.5.1, arm64, clang from the Command Line Tools. Built
clean with no warnings on the first attempt. Sections 1–4 below are all green:
`_verify_all.sh` reports 37 rows, ALL DONE, with `FEMMQT_PYTHON` unset so that
discovery stood on its own.

**Notarization is the one open item**, and it needs a Developer ID rather than
another test run — see §3.

The pass was not a formality: it found a real defect in `InTriangleTest`, and
not the one it looked like. The visible symptom was arm64-only (clang contracts
`a*b - c*d` into an FMA, which destroys the exact zero the predicate needed at a
vertex), but the underlying bug — a point on a slanted **outer boundary** edge
being rejected, because there is no second triangle to catch a wrong sign — was
live on every platform including the shipped Windows build. Fixed by giving the
predicate a scaled tolerance; `libfemm/point_location.h` carries the reasoning,
and `testing/PointLocationTest.py` is the regression test. Measured
independently on both machines, on different meshes: 138 of 416 slanted
boundary-edge midpoints lost on Windows, 117 of 414 on macOS, and zero on
either once the tolerance was in.

---

## Status: a second batch, written blind, 2026-08-22

Everything in this batch was written on Windows and **has never executed on a
Mac**. The macOS branches are `#ifdef`-ed out there and `macos_build.sh` never
runs, so a green Windows suite says nothing about any of it. Work §0 below
before anything else; it is all new since the 2026-08-18 pass.

A Developer ID is **not** needed for any of it. It gates notarisation and
stapling — i.e. distribution — not verification. `macos_build.sh` already
applies an ad-hoc signature, which is enough to exercise every check here,
including App Translocation (§0.6).

---

## 0. The material-library relocation and the bundle-only layout

Nine commits, in dependency order: `e5ad776`, `1effc98`, `bb33316`, `e8e425f`,
`8302faa`, `aee14a9`, `c0374fd`, `d801ffc`.

**What changed, in one paragraph.** The four writable material libraries used to
live in `Contents/MacOS/`, which codesign seals as nested code — and the
material dialog saves on *every* OK, edited or not, so the first ordinary use of
the Material Library would break a signed bundle. They now ship read-only in
`Contents/Resources/` and are copied to `~/Library/Application Support/FEMM/` on
first use. Along the way the bundle became the only place anything runs from on
macOS: the suite drives it, the bindings resolve into it, and the loose copies
in `bin/` are deleted after being copied in.

### 0.1 Build and run the suite

```bash
./macos_build.sh clean
bash testing/_verify_all.sh
```

Expect **44 rows, ALL DONE** (was 37 at the last pass; the new ones are
`ConsolelessPrint`, `NumpyProxy`, `MaterialLib`, `SaveBeforeRun`, `CreateMesh`,
`Fft`, `MagDirTransform`).
`MaterialLib` is the one that matters here — it loads a material from each of
the four libraries and reads the values back out of a saved document, so it
fails if the seeding did not happen or came from the wrong place.

### 0.2 The libraries landed in the right places

```bash
ls build/bin/femm_gui.app/Contents/Resources/*.dat     # 4 read-only seeds
ls ~/Library/Application\ Support/FEMM/                # 4 writable copies
ls build/bin/*.dat 2>/dev/null                         # MUST be empty
ls build/bin/femm_headless build/bin/fkn 2>/dev/null   # MUST be empty
```

The last two are the point of `aee14a9`: no loose copies beside the bundle.

### 0.2b Examples, bindings and documents are in the bundle

```bash
ls build/bin/femm_gui.app/Contents/Resources/examples | wc -l     # 56
ls build/bin/femm_gui.app/Contents/Resources/bindings             # 6 directories
ls build/bin/femm_gui.app/Contents/Resources/*.pdf                # manual + 2
```

The example list is extracted from `packaging/femm-qt.iss` at build time rather
than kept as a second list, so a count other than 56 means the manifest changed
or the extraction broke -- the script fails loudly if it extracts nothing at all.

Then check both halves of the examples arrangement.

The copy in the bundle is a read-only master: open one from
`Contents/Resources/examples/` in the GUI and press Analyze. It must report that
it could not save the file, **not** solve silently. That is the whole point of
`dae3314`; before it, the run went ahead against the shipped geometry.

The copy the DMG puts beside the app is the usable one. Drag `Examples`
somewhere of your own and run a SCRIPT from it -- `Force-vs-Position.lua` is the
sharpest test, since it does `open("Roters-Ch9Fig6.fem")` and then
`mi_saveas("temp.fem")`, both relative to its own directory. It must run to
completion. If it stops at the saveas, the examples went out read-only and no
example script in the distribution can run.

### 0.3 Re-seeding works

```bash
rm ~/Library/Application\ Support/FEMM/matlib.dat
build/bin/femm_gui.app/Contents/MacOS/femm_headless -run testing/MaterialLibTest.py
ls ~/Library/Application\ Support/FEMM/matlib.dat      # back again
```

### 0.4 The bundle is never written to

Open the GUI, open the Material Library dialog, press OK without editing
anything, then:

```bash
codesign --verify --deep --strict build/bin/femm_gui.app && echo "signature intact"
```

Before this batch that sequence broke the signature. It is the single most
important check here, because it is the ordinary way a user assigns materials.

### 0.5 The bindings reach the bundle

The one path `MaterialLibTest` cannot cover — it runs in-process, while the
bindings spawn a separate `femm_headless`. `femm_headless` has no `.app` form,
so before `8302faa` it always resolved to the loose copy in `bin/`.

```bash
export FEMM_DIR="$PWD/build/bin"
python3 -c "import sys; sys.path.insert(0,'pyfemm'); import femm; \
            femm.openfemm(1); femm.newdocument(0); femm.mi_getmaterial('Air'); \
            print('binding ok'); femm.closefemm()"
```

It must find `build/bin/femm_gui.app/Contents/MacOS/femm_headless`, **not**
`build/bin/femm_headless` — which no longer exists, so a wrong resolution is a
hard failure rather than a silent one. Repeat with `FEMM_DIR` pointing at
`Contents/MacOS` directly; both must work.

Then the Lua client, which finds `lua4` inside the bundle now (`d801ffc`):

```bash
build/bin/femm-lua.sh -h -e "openfemm(1); print('lua ok')"
```

### 0.6 App Translocation — no Developer ID required

A quarantined app runs from a randomised read-only path. Simulate it:

```bash
cp -R build/bin/femm_gui.app /tmp/femm_gui.app
xattr -w com.apple.quarantine "0081;00000000;Safari;" /tmp/femm_gui.app
open /tmp/femm_gui.app
```

Materials must still load and save. This is precisely what the relocation was
for, and it is testable today.

### 0.7 Drag it to /Applications

```bash
cp -R build/bin/femm_gui.app /Applications/
open /Applications/femm_gui.app
```

Check in the GUI: solve an example, open the Material Library, and press **F1**
— the manual is new in the bundle (`c0374fd`) and has never worked on macOS.

### 0.8 Nothing carries a Homebrew path

The one thing I could not reason about from Windows. The solvers and
`femm_headless` are copied in *after* `macdeployqt`, so their rpaths are never
rewritten — harmless in theory, since they link no Qt, but worth confirming:

```bash
otool -L build/bin/femm_gui.app/Contents/MacOS/* | grep -i "homebrew\|/opt/\|/usr/local"
```

Anything listed here works on your machine and fails on a downloader's.

### 0.9 If you rebuild with `ninja` instead of `macos_build.sh`

`_verify_all.sh` should stop with `STALE:` and tell you to reassemble. `ninja`
relinks `femm_gui` straight into the bundle but re-copies nothing else, so
without that check the suite would silently test the previous build.

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

Two missing includes of exactly this shape were fixed pre-emptively before the
first pass (`<cstdlib>` in `py_os.cpp`, `<cstring>` in `py_bind.cpp`): both
compiled on MinGW purely by luck of transitive includes.

**Do not go looking for more of them speculatively.** An audit of exactly that
kind was run on 2026-08-18 and found sixteen further cases across the tree.
Every one was real, and every one was reverted: the tree compiled on all three
platforms before the change and after it, so ten files were being touched —
the mesher and a widely-included header among them — to prevent a hypothetical
break on a future toolchain that trims its transitive includes. That is not a
trade worth making while the tree is being packaged and shipped.

The audit itself was kept, as a standing fix list with file, line and symbol
for each case — it lives with the platform-pass hand-offs on the dev machine
rather than in this repository, so it will not be in your checkout; ask for it
if a build ever needs it. It is known-good rather than theoretical: applied, it
built clean and kept the suite green.

**Fix one of these when a toolchain actually fails on it**, or when there is a
reason to be editing that file anyway. A missing include that breaks a real
build is a bug like any other; one that no compiler objects to is not.

The same applies to any tooling built to hunt for them. A one-translation-unit-
per-header check was written and also reverted; note if it is ever revived that
it does **not** catch the case people expect it to. `gui/editor_widget.h` has
`std::vector` members and no `<vector>` of its own, and compiles clean either
way, because `<QWidget>` supplies it. Finding that class of thing needs the
project-header-closure method instead, and the two are complementary.

## 2. Does it run

```bash
cd femm-qt/testing
bash _verify_all.sh
```

The harness derives every path from its own location, so it needs no editing
(it was Windows-only until 2026-08-18). Expect ~30 rows, all reporting
agreement.

`PyBridge*` rows SKIP cleanly in two cases, neither of which is a failure: no
usable CPython 3 at all, and — the commoner one on macOS — a CPython that
answers the handshake but has no matplotlib. The second used to fall through
the skip and die three different ways further in, which is what happened on the
first pass here; `_skip_if_no_backend()` in the three tests now names the
interpreter and the missing piece instead.

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
- **`FQReference` IS EXPECTED TO FAIL HERE, and that is not a regression.**
  Read the row as "not applicable", not as red. clang contracts `a*b+c` into a
  fused multiply-add on arm64 by default, which changes what the MESHER
  produces -- measured, same model, FMA vs not: 16277 nodes against 16357, with
  ~64% of vertices in only one of the two meshes. So macOS solves a different
  discrete problem, and the ~1e-3 movement this test reports is ordinary
  mesh-to-mesh discretisation scatter. Every OF suite stays green throughout,
  which is the check that matters: 2-3% tolerances sized for exactly this.
  Turning contraction off was considered and rejected on 2026-08-19 -- it costs
  10-14% on AC harmonic solves and the answers were never wrong. See the header
  of `testing/FQReferenceCompare.py` for the full reasoning and for the one way
  to get the detector back here (a second, macOS-generated reference snapshot).
- **`HILineIntegralTest`** is where the point-location defect surfaced: it read
  `13 agree, 2 flagged` on the first pass, with the temperature drop coming back
  as exactly `-273.15`. That number was `0 - T(other endpoint)` and nothing more
  — `heatFlow1`'s only BC is `Tset = 273.14999999999998` — so it is a
  coincidence of that fixture rather than a recognisable signature. On a 300 K
  model the same failure returns `-300`. It now reads `15 agree, 0 flagged`, and
  `ho_getpointvalues(0.0, 10.0)` returns `288.35208758038` instead of `None`.
- **`PointLocation`** is new, and exists because of this platform pass, which
  it passed on arm64 in all four physics. It
  probes mesh nodes, interior-edge midpoints and slanted boundary-edge
  midpoints in all four physics — every one of them provably inside the mesh.
  The interior-edge row is a control that passes even when the predicate is
  broken, so a run where *only* the boundary-edge rows lose points is the
  signature of the arithmetic, not of the mesh. See `libfemm/point_location.h`.

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

**Result of the first pass (2026-08-18): all of the above passed**, including
the copy step that had never executed. `codesign --verify --deep --strict`
accepts the bundle, and the `.py` files are genuinely *sealed*, not merely
tolerated — appending a line to `python/csv.py` in a copy of the bundle breaks
verification with "invalid or unsupported format for signature / In
subcomponent: …/python/csv.py". Two things follow. codesign treats each `.py`
in the code directory as **nested code**, so any post-signing edit invalidates
the whole app signature — `macos_build.sh` already orders this correctly
(macdeployqt → copy assets → re-sign) and that ordering is now a constraint to
preserve. And **notarization remains untested**: non-Mach-O files in
`Contents/MacOS/` run against Apple's bundle-layout guidance, and ad-hoc
signing passing is not evidence that a Developer ID + notarized build will.
That needs a real signing identity to settle, not another test run. The
idiomatic fallback if it is rejected is `Contents/Resources/python/` with
`exeDir()/../Resources/python`.

`exeDir()` was confirmed to resolve from *inside* the bundle rather than
passing for the wrong reason: with `build/bin/python/` and
`build/bin/pybridge-worker/` temporarily moved aside, the bundled
`femm_headless` still imported `datetime` and evaluated the `re` brace patch.

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
raise in the script, not kill femm-qt. Confirmed correct on the first pass.

**Do not set `FEMMQT_PYTHON` when running the suite.** It short-circuits
`candidates()` to the one forced interpreter (`pybridge_host.cpp:390`), which
is exactly what `PyBridgeDiscoveryTest.py` is built to exercise — it stands up
a matplotlib-less venv and checks that discovery passes over it. With the
override set, that row reports "discovery settled for a python that cannot
plot while a capable one existed", which is an artefact of the override rather
than a real failure. The first pass needed the override because Apple's
`/usr/bin/python3` was not a candidate at all; it is one now (guarded on the
Command Line Tools being installed, so the xcrun stub is never executed), so
the suite should run clean without it.

---

## Reporting back

Most useful, in order: the **first** compiler error verbatim; then any
`_verify_all.sh` row that is not agreement, with its full line; then whether
the bundle contains `python/` and passes codesign. A screenshot of a plotted
figure from inside the .app is the single best "it works" signal, since it
exercises the bundle layout, the bridge, and the GUI at once.
