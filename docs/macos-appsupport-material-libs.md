# macOS: relocate the writable material libraries to Application Support

**Status:** P0 and P1 implemented, neither yet run on a Mac. P2 outstanding. Needed before femm-qt can ship as
a code-signed `.app` in `/Applications`.

## Problem

The four material-library data files —

    matlib.dat  (magnetics)   statlib.dat (electrostatics)
    heatlib.dat (heat flow)   condlib.dat (current flow)

— are located at runtime via `exeDir()+"<name>.dat"` (the directory of the
running executable):

- read by the `*_getmaterial` handlers: `py_mag_editor.cpp` (matlib),
  `py_elec_editor.cpp` (statlib), `py_heat_editor.cpp` (heatlib),
  `py_curr_editor.cpp` (condlib);
- **written** by the GUI material-library editor (`gui/matlib_dialog*.cpp`,
  `saveLibrary()`).

Because the editor *writes* them, today's macOS build (`macos_build.sh`) keeps
**one** real copy inside the bundle (`femm_gui.app/Contents/MacOS/*.dat`) and
points `bin/` at it with relative symlinks, so the GUI (runs from the bundle)
and `femm_headless` (runs from `bin/`) resolve to the same mutable file.

**This does not survive `/Applications`.** A code-signed bundle is effectively
read-only: writing into `Contents/MacOS/` invalidates the code signature, needs
admin rights under `/Applications`, and can land in a randomized read-only path
under App Translocation for a quarantined app. So the "edit the `.dat` inside the
bundle" model has to go for a distributed app.

**And the write is far easier to trigger than "adding a material".**
`MatLibDialogBase::accept()` calls `saveLibrary()` unconditionally on **every
OK**, edited or not. So opening the Material Library, dragging a stock material
into the model and pressing OK — the ordinary way anyone assigns materials —
rewrites the `.dat`. On a signed bundle that is the first thing a new user would
do. The relocation is therefore not protecting an occasional workflow; it is
protecting the common one.

**A no-op OK is not a no-op on disk either.** The save is a full round trip
through the parser: every material is read into a struct and written back out at
`%.17g`. Tag coverage is complete — every tag present in all four shipped
libraries is one the parsers read, and folder name/URL/vendor survive — so
nothing is *lost*. But the reformatting rewrites 22 of `statlib.dat`'s 78
numeric fields (28%) and 21 of `heatlib.dat`'s 621 (3.4%): `3.7` becomes
`3.7000000000000002`. Values are preserved exactly; it is cosmetic churn in a
file the user never edited. Worth fixing independently by making `saveLibrary()`
a no-op when the library tree is unmodified — that helps every platform, not
just macOS.

This inverts the mutable/immutable split we already use: the **solvers** are
immutable build artifacts (fine to duplicate into the bundle — they can't drift),
but the **`.dat` files are user-mutable**, so on macOS they must move *out* of the
read-only bundle to a per-user, writable location.

## Goal

On macOS, resolve every material-library path to a **per-user, writable** copy in
Application Support, seeded on first use from a **read-only** copy shipped inside
the bundle. Windows/Linux behavior unchanged for now (see §7).

## Design

### 1. One resolver, used by every reader and the writer

Add a helper in **libfemm** (MIT, Qt-free — it lives below the GUI, so it must
NOT use `QStandardPaths`):

```cpp
// script_callbacks.h  (or a small util TU)
// Absolute path to a writable material-library file, creating the containing
// directory and seeding a first-run copy from the read-only bundle/install as
// needed. name = "matlib.dat" / "statlib.dat" / "heatlib.dat" / "condlib.dat".
std::string materialLibPath(const char *name);
```

Replace all `exeDir()+"<name>.dat"` call sites (the four editors) **and** the GUI
editor's read/write path with `materialLibPath(name)` so reader and writer always
agree.

### 2. Resolution + first-run seed (macOS branch)

```
materialLibPath(name):
  #ifdef __APPLE__
    userDir  = $HOME + "/Library/Application Support/FEMM/"
    userPath = userDir + name
    if (!exists(userPath)):
        create_directories(userDir)
        seed = seedPath(name)                 // read-only original, see below
        if (exists(seed)):
            copy(seed -> userPath); chmod u+w userPath
        // else: dev tree without a bundle -> fall through, file simply absent
    return userPath
  #else
    return exeDir() + name                     // unchanged on Windows/Linux
  #endif
```

- `$HOME/Library/Application Support/FEMM/` — the standard per-user app-data
  location. Subdir `FEMM` matches `setApplicationName("FEMM")`. Use `getenv("HOME")`
  directly (not Qt) since this is libfemm.
- Paths are UTF-8 `std::string`; open with `femm_fopen` as today (Unicode-safe).

### 3. The read-only seed inside the bundle

Ship the pristine libraries in the idiomatic read-only location and compute the
seed relative to the running executable:

```
seedPath(name):  return exeDir() + "../Resources/" + name   // Contents/Resources/
```

`exeDir()` is `…/femm_gui.app/Contents/MacOS/`; `Contents/Resources/` is its
sibling. `femm_headless` inside the bundle resolves the same way.

The seed is a **chain**, not an alternative return path: `Contents/Resources/`
first, then `exeDir()` itself. That is what lets P0 land before P1 — today's
`macos_build.sh` still puts the `.dat` files in `Contents/MacOS/`, so the second
link finds them; when P1 moves the seeds to `Contents/Resources/`, the first link
takes over and nothing else changes. The resolver always *returns* the
Application Support path either way, so there is only ever one writable copy.

**There is no second copy to keep in step.** An earlier draft worried about
`bin/femm_headless` and the bundled `femm_gui` resolving to different files, and
proposed keeping the `bin/*.dat` symlinks as a dev fallback. That is moot as of
`e5ad776`: on macOS the regression suite now runs the binaries *inside the
bundle*, because that is the layout that ships. `bin/femm_headless` exists only
as the source for `macos_build.sh`'s `cp` and is not run by anything. One
binary location, one resolution path.

### 4. `macos_build.sh` changes

- Copy the seed `.dat` files into **`Contents/Resources/`** (read-only originals),
  not `Contents/MacOS/`. `Contents/Resources/` already exists and already carries
  the license files (`macos_build.sh:231`), so this is an existing location.
- **Delete the `bin/*.dat` relative-symlink step**, but keep a plain `cp` of the
  seeds into `bin/`. The symlinks existed because `bin/` and the bundle shared
  one *mutable* file; a read-only seed in both places cannot drift, because the
  writable copy is in Application Support either way.
  `bin/` still needs one: all six language bindings search `FEMM_DIR` then
  `../build/bin`, trying `<exe>`, `<exe>.exe` and
  `<exe>.app/Contents/MacOS/<exe>`. `femm_gui` matches the `.app` form and comes
  from the bundle, but **`femm_headless` has no `.app` form**, so it always
  resolves to the loose `bin/femm_headless` whose `exeDir()` is `bin/`. Missed
  on the first pass (`bb33316`), fixed in `e8e425f`.
- Solvers/headless/lua4 copies into `Contents/MacOS/` stay as-is (immutable).

## 5. Behavior that falls out for free

- **Reset to defaults:** delete `~/Library/Application Support/FEMM/matlib.dat`
  (or a menu item that does) → re-seeded from the bundle on next use.
- **GUI + headless share one file** with no symlink and no `exeDir()` divergence.
- **Signed, read-only bundle** — nothing is ever written inside it.

## 5a. Out of scope, but found while surveying this

**Eight parsers, five path sites.** Each physics has *two* independent
line-parsers for its library format: the script reader (`*_getmaterial`, a linear
scan that stops at the first name match and never writes) and the GUI reader
(`readMaterialBlock`, called per block by the base-class folder walker, which
materialises every material into `m_libMaterials`). They are functionally
identical today — the material-property tag sets match exactly in all four
physics — but they are maintained separately, and so are the four writers.

This relocation touches only the *paths*, of which there are five: the four
`exeDir()+name` readers and the dialog's `m_libPath`. The parser duplication is
untouched and is its own latent hazard: adding a material property means editing
eight parsers and four writers, and a miss is silent in an unpleasant direction —
the GUI would display and re-save a field that `*_getmaterial` drops, so the same
model built by script and by hand would differ. Same shape as the
ResultsWidget/ScalarResultsWidget unification already on the roadmap; not to be
folded in here.

## 6. Edge cases / open questions

- **App upgrade with updated seed libraries.** The user's edited copy takes
  precedence (never clobber edits). If we ever want to surface new stock
  materials, that's an explicit merge/reset UI — out of scope here; note it.
- **Seed writable bit.** Bundle copies are read-only; `chmod u+w` the seeded user
  copy (the current build already `chmod u+w`s the bundle copies — move that
  intent to the seed step).
- **First-run race.** Single interactive launch; ignore.
- **Missing `$HOME`.** Degenerate; fall back to `exeDir()+name`.

## 6a. `FEMM_DIR` on macOS — one run location, whatever it is set to

`FEMM_DIR` may sensibly be either `<repo>/build/bin` or an installed app's
`Contents/MacOS` (the latter being the only one that exists in a distribution,
since a shipped `.app` has no `bin/`). Having those resolve to *different
binaries* was a genuine mess: `femm_gui` matched the `.app` form and came from
the bundle, while `femm_headless` — which has no `.app` of its own — matched the
bare form and came from the loose copy in `bin/`.

All six clients now try `<dir>/femm_gui.app/Contents/MacOS/<exe>` **before** the
bare `<dir>/<exe>`. On macOS that resolves *both* binaries into the bundle from
either setting, and from the no-`FEMM_DIR` fallback to `../build/bin` as well.
On Windows and Linux the form never matches and nothing changes.

So the bundle is the only place anything runs from on macOS: the regression
suite (`e5ad776`), the bindings (this), and by construction a distributed app.
`bin/` keeps a read-only seed anyway, purely so that a developer who runs
`bin/femm_headless` by hand still finds materials; nothing automated goes there.

Documented for users in the four binding READMEs that mention `FEMM_DIR`
(`luafemm/README.md`, `luafemm/femm-lua.md`, `pyfemm/pyfemm.md`,
`octavefemm/octavefemm.md`) — it had been recorded only in
`docs/macos-bundle-fixes.md`, which `docs/.gitignore` excludes.

## 7. Windows / Linux analog (not now, but symmetric)

The same read-only-install problem exists under `C:\Program Files\…` (admin) and a
system prefix on Linux. When it matters, the same resolver gains branches:
`%APPDATA%\FEMM\` on Windows, `$XDG_DATA_HOME/femm` (or `~/.local/share/femm`) on
Linux, each seeded from the install dir. Keeping the single `materialLibPath()`
chokepoint now makes that a localized change later.

## 8. Phasing

- **P0 — DONE (`1effc98`).** `materialLibPath()` added; the four readers and the
  GUI editor routed through it. Seeds from `Contents/Resources/` if present,
  else `exeDir()`, so it worked against the unchanged build script.
- **P1 — DONE (`macos_build.sh`).** Seeds moved to `Contents/Resources/`, no
  longer `chmod u+w` (they are meant to stay read-only; the resolver makes the
  *user's* copy writable), and the `bin/*.dat` symlink step deleted. The write
  path is now fully outside the bundle.
- **P2 — outstanding.** (With signing/notarization.) Verify a signed
  `/Applications` install: edit a material, relaunch, edit persists; delete the
  Application Support copy, relaunch, it re-seeds.

**Neither P0 nor P1 has been executed on a Mac.** Both were written on Windows,
where the macOS branch is `#ifdef`-ed out and the build script never runs — so a
green Windows suite says nothing about either. First macOS run is the real test:
`./macos_build.sh && bash testing/_verify_all.sh`, and the `MaterialLib` row is
the one that exercises the seeding end to end.

## 9. Related

- Path handling / `femm_fopen`: [`reference_unicode_paths`] — the resolved path is
  a UTF-8 `std::string` opened via `femm_fopen`, so this composes with the Unicode
  work.
- The immutable-vs-mutable rationale (duplicate solvers, single-source the `.dat`)
  is the same principle, applied one level up: for a read-only bundle the single
  source must live *outside* it.
