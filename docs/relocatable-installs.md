# Relocatable installs: what a distro package or an AppImage would need

*Written 2026-08-23, after the Linux/FreeBSD/macOS packaging pass. Nothing here
is started. It is deliberately deferred until the current build has had tester
feedback — this changes how every asset is found, and revalidating that while
waiting on reports would invalidate the thing being reported on.*

## The one assumption everything rests on

femm-qt finds everything relative to the running executable, via `exeDir()`
(`libfemm/script_callbacks.cpp`). That is why a self-contained program
directory needs no configuration at all, and it is the reason the Windows
installer, the Unix install step and the macOS bundle all work with no code
that knows about any of them.

It holds as long as the program directory is **one place** and is **writable**.
Every packaging route that is not yet supported breaks one or the other:

| route | breaks |
|---|---|
| FHS distro package (`/usr/bin` + `/usr/share/femm`) | split, and read-only |
| AppImage | read-only (squashfs mount) |
| Flatpak / Snap | read-only, plus sandbox — see the end |

macOS already forced half of this work: a signed `.app` is read-only, which is
why `materialLibPath()` exists. The remaining work is generalising that.

## Inventory: everything resolved from `exeDir()`

Five sites, and it is worth knowing there are only five.

| what | where | notes |
|---|---|---|
| the four solvers | `libfemm/script_callbacks.cpp:420` | `exeDir() + doc->solverName()` |
| `python/` (in-process stdlib) | `libfemm/py_state.cpp:314` | |
| `pybridge-worker/` | `libfemm/pybridge_host.cpp:589` | **already a 2-step chain** — shipped, then a dev-tree path at `:592` |
| the four `.dat` material libraries | `libfemm/material_lib_path.cpp` | **already has the macOS branch**, seeds from `../Resources` into Application Support |
| `manual.pdf` | `gui/mainwindow.cpp:2318` | **already a 2-step chain** — beside the exe, then `../Resources` on macOS |

Three of the five already do a search rather than a single path. The pattern
exists; it is just written out three times in three shapes.

Client-side files (`femm.lua`, `lua4`, the bindings) are found by the *client*,
not by femm-qt, and are already handled — `femm-lua.sh` uses `readlink -f "$0"`,
and the six bindings search `FEMM_DIR`, then beside themselves, then `PATH`,
then `/Applications`. Nothing to do there.

## What to build

### 1. One resolver, with a search order

Replace the five sites with something like

```cpp
std::string femmResource(const char *relative);   // "python", "fkn", "manual.pdf"
```

searching, in order:

1. `exeDir() + relative` — self-contained layouts: Windows, the Unix install,
   the macOS bundle's `MacOS/`. **Must stay first**, so nothing that works
   today changes behaviour.
2. `exeDir() + "../Resources/" + relative` — the macOS bundle.
3. `exeDir() + "../share/femm/" + relative` — an FHS install found relatively,
   which works without any compiled-in prefix as long as `bin/` and `share/`
   share a parent. This covers `/usr`, `/usr/local` and `~/.local` alike.
4. A prefix baked in at configure time (`CMAKE_INSTALL_PREFIX`) — the fallback
   for layouts where even (3) fails.

The point of ordering it this way is that **(1) alone is what happens today**,
so every existing layout is unaffected and the new cases are additive.

### 2. Separate read-only from writable

Two of those five are written at runtime and the rest are not:

- **material libraries** — the GUI's material editor saves all four on every
  OK, edited or not (`gui/matlib_dialog_base.cpp`, `accept()`).
- **examples** — every example script does `open("model.fem")` then
  `mi_saveas("temp.fem")`, both relative to its own directory, and analysis
  writes the model back before solving.

`materialLibPath()` already solves the first, for macOS only: it returns a
per-user path and seeds it from a read-only copy on first use. Generalise that
to any platform where the resolved path is not writable, with the destination
being `$XDG_DATA_HOME/femm` (default `~/.local/share/femm`) on Unix.

Note this **reverses a decision** recorded in the packaging notes: XDG
relocation was declined on the grounds that the program directory is writable
and only `sudo` breaks it. That reasoning is correct for the current install
and false for every route in this document. Reversing it is the point, not an
oversight — but the *current* install should keep writing in place, which the
"only if not writable" test gives for free.

For the examples, the answer is already established on macOS and needs copying,
not inventing: ship read-only masters, and place a writable copy in the user's
own space (`~/Documents/FEMM/examples` on Linux, `~/FEMM` on macOS — see
`packaging/unix_install.sh` and `packaging/macos_dmg.sh`).

### 3. `install()` rules

There are **none** in the tree today — `grep -rn "^install(" --include=CMakeLists.txt`
returns nothing. A packager needs, at minimum:

- `include(GNUInstallDirs)` and `install(TARGETS ...)` for the GUI, the headless
  server and the four solvers;
- `install(FILES ...)` / `install(DIRECTORY ...)` for `python/`,
  `pybridge-worker/`, the `.dat` seeds, the manual, the `.desktop` file and the
  icon;
- everything honouring `DESTDIR`, which comes free with `install()` and is what
  every packaging tool builds into a staging root.

This is the mechanical part and is worth doing first: it is useful on its own,
and it forces the read-only/writable split to be made explicit.

### 4. Test coverage

`testing/_verify_all.sh` assumes the `build/bin` layout. It would need to run
against an installed tree as well — the macOS branch already does something
like this, driving `femm_gui.app` rather than `bin/`. Without that, an FHS
install is untested by construction.

## Per-route notes

**FHS distro package.** Wants (1), (2), (3). Nothing else. The maintainer's
position stands: let a packager who wants it do it — but this is the list they
would ask for.

**AppImage.** Wants (1) and (2) only; no `install()` rules needed, since
`linuxdeploy` builds an AppDir. The tempting thing to get wrong: an AppImage is
a **read-only** mount, so without (2) the material editor silently fails to
save. Also, the seven executables live in one image with a single entry point —
`AppRun` dispatches on `$ARGV0`, so `femm_headless` as a symlink to the
`.AppImage` works, busybox style. The solvers spawn correctly with no work,
because `exeDir()` inside the mount is exactly where they are.

**Flatpak / Snap: do not.** Both sandbox the application, and a sandboxed FEMM
cannot be spawned by an Octave, Python or Julia process living outside the
sandbox. That breaks all six bindings, and unlike macOS TCC there is no
directory you can move to to avoid it.

## What must not change

Whatever is done here, these have to keep working exactly as they do now, since
all three are verified on real hardware:

- Windows: `c:\femm-qt\bin` beside `examples/`, `FEMM_DIR` in the registry.
- Linux/FreeBSD: `~/.local/opt/femm-qt` with `~/Documents/FEMM` alongside.
- macOS: the bundle, `~/Library/Application Support/FEMM`, `~/FEMM`.

Keeping `exeDir() + relative` as the first entry in the search order is what
guarantees that.

## Suggested order

1. `install()` rules with `DESTDIR`. Useful alone, and forces the split to be
   named.
2. The resolver, with `exeDir()` first — a no-op for every current layout, so
   it can land and be verified before anything depends on it.
3. Generalise `materialLibPath()`'s seed-into-user-space to "wherever the
   resolved path is not writable".
4. Then, and only then, AppImage or an FHS package becomes a packaging exercise
   rather than a code change.
