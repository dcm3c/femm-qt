# Polar coordinate mode

Status: **implemented.** Originally written as a gap analysis after the manual's
Grid dialog figure was regenerated and the 2004 dialog turned out to have a
Cartesian/Polar combo that femm-qt's did not. Everything catalogued below has
since been built; the history is kept because it records why the flag must not
be stripped.

## What the setting is

Original FEMM carries a per-document flag, `Coords` (0 = cartesian,
1 = polar), stored in the `.fem`/`.fee`/`.feh`/`.fec` header as

```
[Coordinates] =  cartesian     (or polar)
```

It is a *coordinate entry and display* mode, not a change of solution
geometry: the mesh, the physics and the problem type (planar/axisymmetric)
are unaffected.

## Why it can never be stripped

Both of these were already correct before any of the work below — this was
never dead code:

| | where |
|---|---|
| **File round-trip**, all four physics | `libfemm/{mag,elec,heat,curr}_document.cpp` parse and write `[Coordinates]`; the results readers parse it too |
| **Solver behaviour: Prescribed A** | `solvers/fkn/femmedoccore.cpp:714` parses it; `prob1big.cpp:1045`, `prob2big.cpp:784`, `prob3big.cpp:698`, `prob4big.cpp:698` branch on it |

The solver branch is the important one. A Prescribed A boundary condition is
evaluated as

```
cartesian:  A = A0 + x*A1 + y*A2
polar:      A = A0 + r*A1 + theta*A2      (theta in degrees)
```

so a polar model gets materially different boundary values. **Deleting
`[Coordinates]` would silently reinterpret every polar model as cartesian and
produce wrong answers with no diagnostic**, and would break loading of
OF-authored polar models.

## What was missing, and where it now lives

Every entry below is done. Formats and the error message match OF so a model or
script authored in either program behaves the same.

| # | piece | OF reference | femm-qt |
|---|---|---|---|
| 1 | **Cartesian/Polar combo in the grid dialog** | `femmeView.cpp:1437,1443` | `MainWindow::showGridPropertiesDialog` — one dialog now serves both the Grid menu item and the toolbar button, which had carried separate copies |
| 2 | **Status bar readout in (r, theta)** | `femmeView.cpp:1248-1250` and seven sibling views | `MainWindow::updateCoordinates`, via `coorddisp::statusText`. femm-qt routes every view's mouse position through one slot, so this is a single site, not eight |
| 3 | **TAB point entry in polar** | `femmeView.cpp:1645,1667` | `editor_widget.cpp`, `results_widget.cpp`, `scalar_results_widget.cpp`, via `coorddisp::entryLabels` / `toDisplay` / `fromDisplay` |
| 4 | **`mi_/ei_/hi_/ci_setgrid` string argument** | `beladrawLua.cpp:2163` | `py_editor_common.h` — the command existed but forwarded numbers only |
| 5 | **`mo_/eo_/ho_/co_setgrid` string argument** | `belaviewLua.cpp:993` | `registerSetGrid` in `py_postproc_common.h`, replacing the numeric-only `regv` registration |

Items 4 and 5 were the sharpest compatibility break: the commands existed, so
`mo_setgrid(0.5,"polar")` from a ported script failed on *argument type* rather
than reporting "not implemented", and the grid-size half worked while the
coordinate half could not be reached at all.

### The one structural decision

`*_setgrid` does two unrelated things. Grid spacing is **view** state and is
forwarded to the GUI as before. The coordinate mode is **document** state — it
is written to the file header and read back by the solver — so it is set on the
document inside the binding rather than forwarded. That is what makes
`mi_setgrid(0.25,"polar")` work headless, where there is no view at all.

The forwarded pair is `{spacing, coords}` with a negative slot meaning "not
specified"; spacing is positive by construction and `Coords` is 0 or 1, so the
sentinel is unambiguous and a bare mode change still reaches the view to
trigger a repaint.

### Deliberately not done

OF also registers `*_set_grid` underscore aliases. femm-qt has no `_set_*`
aliases anywhere, so adding one only for this command would be inconsistent;
restoring the whole alias family is its own (larger) compatibility task.

## Shared helpers

- `libfemm/coord_mode.h` — `parseCoordMode()` and OF's `"Unknown option for set
  grid"` error, used by all eight bindings.
- `gui/coord_display.h` — status text, entry-dialog labels, and the
  (x,y) <-> (radius, degrees) conversions, used by the status bar and all three
  point-entry dialogs.

## Related manual state

`manual.tex` describes the Cartesian/Polar drop list in the Grid section. That
paragraph was trimmed in commit 2965440 when the figure was regenerated against
a dialog that had lost the combo; it is now restored (with one wording fix: the
drop list is in the dialog, not "in the edit box"), and `griddlg.png` was
recaptured from the rebuilt dialog.
