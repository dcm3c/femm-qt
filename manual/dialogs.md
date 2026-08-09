# Manual dialog-figure inventory (Phase 1)

Scope: **dialog screenshots only**, Windows. Canvas/plot/schematic figures and
toolbar strips are inventoried at the end but are **out of scope**.

Status: inventory only, no code written.

---

## 0. How the manual builds (read this before planning Phase 3)

> **SUPERSEDED — the manual now builds with pdflatex and PNG figures.**
> See §6 for what changed and why the constraint below no longer applies.
> The rest of this section describes the state at inventory time.

| | |
|---|---|
| Engine | `latex` → `dvips` → `ps2pdf` (MiKTeX). `manual.tex` line 1 is `% !TEX program = latex`. **Not** pdflatex/XeLaTeX. |
| Graphics driver | `\usepackage[dvips]{graphicx}` (and `[dvips]{hyperref}`) |
| Call syntax | `\centerline{\includegraphics{name.ps}}` — **extensions ARE written explicitly**, 69 of 71 calls carry no options at all |
| Sizing | 69/71 calls have **no `width=`**. Only `open1.ps` and `open1b.ps` use `[width=6in]`. Everything else is displayed at its natural size, i.e. **the EPS `%%BoundingBox` alone sets the printed size**. |
| Figures | 71 `\includegraphics` calls → 61 distinct files (53 `.ps`, 20 `.eps` on disk; 12 files on disk are unreferenced). All referenced files exist. |
| Structure | Every figure is inside a `figure` environment with a `\caption` (71 envs / 73 captions). |
| Build products | `manual.pdf`, `.dvi/.ps/.aux/...` are gitignored; only `.tex` and the figure files are tracked. |

### Consequence for Phase 3 — PNG cannot be used

The plan's "emit PNGs using existing basenames" **will not work**. `dvips`
embeds PostScript; it cannot include PNG, and the `.tex` names the extension
explicitly. Three options, in order of preference:

1. **Emit EPS under the existing basenames** (`griddlg.ps` stays `griddlg.ps`).
   Zero `.tex` edits. `QWidget::grab()` → PNG → wrap as EPS (raster-in-EPS is
   fine; the current files are exactly that — ImageMagick and `pnmtops`
   wrappers around screenshots).
2. Emit EPS under new names and edit the 35 `\includegraphics` lines. More
   churn, no benefit.
3. Convert the manual to `pdflatex`. **Rejected**: all 61 existing figures are
   `.ps`/`.eps`, so this forces converting every canvas/plot/schematic figure
   too — far outside this scope.

**Sizing is the lever, and it is free.** Because there is no `width=`, the
printed size is `BoundingBox ÷ 72` inches. Today's dialogs range from
187×24 pt (a toolbar) to 588×282 pt. A 2× or 3× device-pixel-ratio grab must
therefore carry a BoundingBox equal to the *logical* size in points, not the
pixel count, or the figure will print 2–3× too large. This is the single
easiest thing to get wrong.

**Correction (measured, not inferred).** An earlier draft of this document said
the existing figures were "≈300 dpi already". That was wrong — it was measured
off a 300 dpi *re-render*, not the source. Reading the embedded raster
parameters directly (the `DisplayImage` argument block: position, size in
points, depth, size in pixels) shows `griddlg.ps` displays a **197×114 pixel**
image at **197×114 points**. That is **1 pixel per point = 72 dpi native**, and
it holds across all three generators (ImageMagick, pnmtops, Word2TeX).

So every figure in the manual today prints at 72 dpi. That is the real reason
they look soft, and it means a regenerated 2× or 3× DPR grab is a genuine
quality gain of 4–16× in pixel count, not a wash.

---

## 1. Dialog figures

35 figures show dialogs. `tex:line` is the `\includegraphics` call site.
All have no `width=` option unless noted. "State" is the proposed concrete
population, chosen to match the 2004 figure so the replacement is a drop-in.

### Magnetics (chapter in `manual.tex`)

| # | figure | tex:line | caption | 2004 dialog | femm-qt class | proposed state |
|---|---|---|---|---|---|---|
| 1 | `probdef.ps` | manual:1020 | Problem Definition dialog. | Problem Definition: Planar / Inches / Frequency (Hz) 0 / Depth 1 / Solver Precision 1e-008 / Min Angle 30 / Comment "Add comments here." | `ProblemDialog` (needs `MagDocument*`) | exactly as 2004 |
| 2 | `propdef.ps` | manual:1090 | Property Definition dialog box | Property Definition: empty Property Name combo; Add/Delete/Modify Property; OK | `PropertyListDialog(doc, propType)` | empty list, as 2004 |
| 3 | `nodeprop.ps` | manual:1132 | Nodal Property dialog. | Nodal Property: "New Point Property", radio Specified Potential (selected) / Point Current; Specified Vector Potential Wb/m = 0; Point Current Amps = 0 (disabled) | `PointPropEditorDialog` | name "New Point Property", A=0 |
| 4 | `bdryprop.ps` | manual:1168 | Boundary Property dialog. | Boundary Property: name "zero", BC Type "Prescribed A", small-skin-depth µ/σ greyed, Prescribed A A0..A2=0, φ=0 | `BoundaryEditorDialog` | name "zero", type Prescribed A, all zeros |
| 5 | `blockprop.ps` | manual:1282 | Block Property dialog. | Block Property: "Air", Linear B-H, µx=µy=1, φhx/φhy=0, Hc=0, σ=0, J=0, "Not laminated or stranded", lam/strand fields greyed | `MaterialEditorDialog` (`fromProp`) | `FMaterialProp` for Air |
| 6 | `bhdata.ps` | manual:1326 | B-H data entry dialog. | B-H Curve Data for M-19 Steel: two list columns B/H, buttons Plot B-H Curve, Log Plot, Read B-H points from text file, OK/Cancel | `BHDataDialog` | M-19 Steel B/H pairs from `matlibs` |
| 7 | `fe_matlib.ps` | manual:1470 | Materials Library dialog. | Materials Library: left tree Library Materials (Air, PM Materials → Alnico/NdFeB/Ceramic/SmCo expanded, Soft Magnetic, Non-Magnetic Conductors); right Model Materials: Air, SmCo 27 MGOe, 1018 Steel, Coil | `MatLibDialog(doc)` | doc with those 4 model materials; SmCo branch expanded |
| 8 | `circprop.ps` | manual:1539 | Circuit Property dialog | Circuit Property: name "circuit", radio Parallel / **Series** selected, Circuit Current Amps = 1 | `CircuitEditorDialog` | name "circuit", Series, 1 A |
| 9 | `circuitrslt.ps` | manual:2268 | Circuit results dialog. | Circuit Properties (read-only): combo "Current"; Results block: Total current 1000 A, Voltage Drop, Flux Linkage, Flux/Current, Voltage/Current, Real/Reactive/Apparent Power | **inline `QDialog`** in `magnetics_handler.cpp` | `tutorial.fem`, circuit "Coil": 1 A, voltage drop, flux linkage, inductance, power |
| 10 | `pltdlg.ps` | manual:1869 | X-Y Plot dialog. | X-Y Plot of Field Values: Plot Type "\|B\| (Magnitude of flux density)", points 150, Write data to text file unchecked, File Formatting "Multicolumn text w/ legend" | `xyplot::prompt` (`gui/xy_plot.h`) | Quantity + Number of points only — **no Write-to-text-file checkbox and no File Formatting combo** |
| 11 | `griddlg.ps` | manual:972 | Grid Properties dialog. | Grid Properties: Grid Size 0.25, **Coordinates: Cartesian** combo, OK/Cancel | **inline `QDialog`** ("Grid Properties") in `MainWindow::showGridPropertiesDialog` | as 2004 (Coordinates combo restored) |

### Electrostatics (`manual.tex`, belaman* figures)

| # | figure | tex:line | caption | 2004 dialog | femm-qt class | proposed state |
|---|---|---|---|---|---|---|
| 12 | `belaman6.eps` | manual:2293 | Problem Definition dialog. | Problem Definition: Planar / Inches / Depth 1 / Solver Precision 1e-008 / Min Angle 30 / Comment (no Frequency row) | `ProblemDialog` (`ElecDocument*`) | as 2004 |
| 13 | `belaman7.eps` | manual:2369 | Property Definition dialog box. | Property Definition: Property Name = "Nylon" | `PropertyListDialog(doc, …)` | one property "Nylon" |
| 14 | `belaman8.eps` | manual:2397 | Nodal Property dialog. | Nodal Property: Specified Potential (selected) / Point Charge Density; Specified Potential 0 | `ElecPointPropEditorDialog` | as 2004 |
| 15 | `belaman9.eps` | manual:2431 | Boundary Property dialog. | Boundary Property: "New Boundary", BC Type "Fixed Voltage", Fixed Voltage 0, Surface Charge Density greyed, Mixed c0/c1 greyed | `ElecBoundaryEditorDialog` | as 2004 |
| 16 | `belaman10.eps` | manual:2501 | Block Property dialog. | Block Property: "New Material", Relative εr=1, εz=1, Charge Density C/m^3 = 0 | `ElecMaterialEditorDialog` | as 2004 |
| 17 | `bd_matlib.ps` | manual:2529 | Materials Library dialog | Materials Library: flat list Air, Delrin, Ethanol, Germanium, Kapton 100/150, Lexan, Marble, Mica, Mylar, Nylon, Paper, Polyamide, Polyethylene, Polypropylene…; Model Materials empty | `ElecMatLibDialog(doc)` | empty model side |
| 18 | `belaman12.eps` | manual:2577 | Conductor Property dialog. | **Title says "Circuit Property"** but content is the conductor editor: "New Conductor", Prescribed Voltage / **Total Charge, C** selected = 0 | `ElecConductorEditorDialog` | as 2004 — *title is corrected in femm-qt; see §3* |
| 19 | `belaman19.eps` | manual:2724 | X-Y Plot dialog. | X-Y Plot of Field Values: Plot Type "V (Voltage)", 150 points, Multicolumn text w/ legend | `PlotDialog` | as 2004 |
| 20 | `belaman21.eps` | manual:2982 | Conductor results dialog. | Conductor Properties (read-only): "New Conductor"; Voltage = 6168.01 Volts, Charge = 1e-008 Coulombs | **inline `QDialog`** in `electrostatics_handler.cpp` | `bdemo2.fee`, conductor "+100V": 100 V, 4.48e-7 C |

### Heat flow (`heatflow.tex`)

| # | figure | tex:line | caption | 2004 dialog | femm-qt class | proposed state |
|---|---|---|---|---|---|---|
| 21 | `belaman6.eps` | heat:19 | Problem Definition dialog. | **same file as #12** (electrostatics figure reused) | `ProblemDialog` (`HeatDocument*`) | regenerate separately — see §3 |
| 22 | `hpropdef.ps` | heat:95 | Property Definition dialog box. | Property Definition: Property Name = "Copper, Pure" | `PropertyListDialog` | one property "Copper, Pure" |
| 23 | `hnodeprop.ps` | heat:123 | Nodal Property dialog. | Nodal Property: Specified Temperature (selected) / Point Heat Generation; Specified Temperature 0; Heat Generation W/m greyed | `HeatPointPropEditorDialog` | as 2004 |
| 24 | `hbdryprop.ps` | heat:146 | Boundary Property dialog. | Boundary Property: "T=100", BC Type "Fixed Temperature", Fixed Temperature K = 100, Heat Flux greyed, Convection h/T0 greyed, Radiation Beta/T0 greyed (with the two rendered PDE images) | `HeatBoundaryEditorDialog` | name "T=100", Fixed Temperature 100 |
| 25 | `hblockprop.ps` | heat:172 | Block Property dialog. | Block Property: "Copper, Pure", T-k Curve "Thermal Conductivity Depends on Temperature", Kx=Ky=401 greyed, Edit Nonlinear Thermal Conductivity Curve, Volume heat generation 0 | `HeatMaterialEditorDialog` | Copper, Pure from `matlibs` |
| 26 | `hmatlib.ps` | heat:206 | Materials Library dialog | Materials Library: tree Metallic Solids / **Nonmetallic Solids expanded** (Aluminum Oxide, Asbestos, Beryllium Oxide, Bricks, Carbon, Cement Mortar, Concrete, Coal, Diamonds, Earths, Glasses, Ice, Insulations); Model Materials: Copper, Pure | `HeatMatLibDialog(doc)` | as 2004 |
| 27 | `hcircprop.ps` | heat:246 | Conductor Property dialog. | **Title says "Circuit Property"**; content: "New Conductor", Fixed Temperature K / **Total Heat Flow, W** selected = 0 | `HeatConductorEditorDialog` | as 2004 — title corrected in femm-qt |
| 28 | `hpltdlg.ps` | heat:378 | X-Y Plot dialog. | X-Y Plot of Field Values: Plot Type "T (Temperature)", 150 points | `PlotDialog` | as 2004 |
| 29 | `hcircres.ps` | heat:506 | Conductor results dialog. | Conductor Properties (read-only): "thi…"; Temperature = 100 K, Heat Flux = 807.72 W | **inline `QDialog`** in `heat_handler.cpp` | `testing/heatFlow1.feh`, conductor "Bdry": 288.35 K, 100 W |

### Current flow (`cflow.tex`)

| # | figure | tex:line | caption | 2004 dialog | femm-qt class | proposed state |
|---|---|---|---|---|---|---|
| 30 | `cd1.ps` | cflow:19 | Problem Definition dialog. | Problem Definition: **Axisymmetric** / Meters / Frequency Hz 60 / Depth greyed / Precision 1e-008 / Min Angle 30 / Comment "capacitance between spheres" | `ProblemDialog` (`CurrDocument*`) | as 2004 (note the comment text is a leftover from the electrostatics example) |
| 31 | `hpropdef.ps` | cflow:90 | Property Definition dialog box. | **same file as #22** (heat-flow figure reused) | `PropertyListDialog` | regenerate separately — see §3 |
| 32 | `cd2.ps` | cflow:115 | Nodal Property dialog. | Nodal Property: Specified Voltage (selected) / Point Current Density; Specified Voltage 0; Current Density A/m greyed | `CurrPointPropEditorDialog` | as 2004 |
| 33 | `cd3.ps` | cflow:138 | Boundary Property dialog. | Boundary Property: "open_bc", BC Type **Mixed**, Fixed Voltage & Surface Current greyed, Mixed c0 = `I*4.450600224214277e-011`, c1 = 0 | `CurrBoundaryEditorDialog` | as 2004 (keeps the complex-literal display) |
| 34 | `cd4.ps` | cflow:164 | Block Property dialog. | Block Property: "Air", σr/σz = 0, εr/εz = 1, loss tangent r/z = 0 | `CurrMaterialEditorDialog` | as 2004 |
| 35 | `cd5.ps` | cflow:249 | Conductor Property dialog. | Conductor Property: "+100V", **Fixed Voltage** selected = 100, Total Current Amps greyed | `CurrConductorEditorDialog` | as 2004 |
| 36 | `cd6.ps` | cflow:381 | X-Y Plot dialog. | X-Y Plot of Field Values: "V (Voltage)", 150 points | `PlotDialog` | as 2004 |
| 37 | `cd7.ps` | cflow:484 | Conductor results dialog. | Conductor Properties (read-only): "+100V"; Voltage = 100 V, Current = I*0.000168775 A, Volt/Curr = -I*592504 Ohms, Curr/Volt = I*1.68775e-006 S | **inline `QDialog`** in `curr_handler.cpp` | `cdemo2.fec`, conductor "+100V": 100 V, 1.6877e-4j A — the 2004 numbers, in j notation rather than I* |

*(37 rows over 35 distinct files: `belaman6.eps` and `hpropdef.ps` each appear
in two chapters — see §3.)*

---

## 2. femm-qt dialogs with no 2004 counterpart (manual gaps)

These exist in the GUI and are documented nowhere in the manual:

| femm-qt class | what it is | note |
|---|---|---|
| `CurrMatLibDialog` | Materials Library, current flow | **The other three physics each have a Materials Library figure; current flow has none.** Clearest gap. |
| `ArcPropDialog` / `ArcDialog` | arc-segment properties (max segment angle, boundary, group) | manual describes arcs in prose only |
| `SegmentPropDialog` | line-segment properties | ditto |
| `GroupDialog` | group-number assignment | |
| `CopyMoveDialog` | copy/move/rotate selection | |
| `PrefsDialog`, `ScalarPrefsDialog` | postprocessor display preferences | per-physics variants |
| `PrePrefsDialog` | pre-processor display preferences | |
| `SolverProgressDialog` | solver progress window | 2004 had a separate solver window, not a dialog |
| `OutputDialog` | generic text output window | |

---

## 3. Same widget repeated across chapters

The four physics share one widget family; the manual sometimes reuses the
*same image file* across chapters and sometimes captures a per-physics copy.

**Literally the same file in two chapters** (regenerating once fixes both, but
the two chapters arguably deserve physics-correct copies):

- `belaman6.eps` — electrostatics Problem Definition, also used as the
  **heat-flow** Problem Definition (`heat:19`). Harmless today (neither has a
  Frequency row) but it is an electrostatics capture.
- `hpropdef.ps` — heat-flow Property Definition ("Copper, Pure"), also used as
  the **current-flow** Property Definition (`cflow:90`), where the property
  name is wrong for the chapter.

**Per-physics copies of the same dialog** (4 × the same widget, 4 files):

| widget | magnetics | electrostatics | heat | current |
|---|---|---|---|---|
| Problem Definition | `probdef.ps` | `belaman6.eps` | *(reuses belaman6)* | `cd1.ps` |
| Property Definition | `propdef.ps` | `belaman7.eps` | `hpropdef.ps` | *(reuses hpropdef)* |
| Nodal Property | `nodeprop.ps` | `belaman8.eps` | `hnodeprop.ps` | `cd2.ps` |
| Boundary Property | `bdryprop.ps` | `belaman9.eps` | `hbdryprop.ps` | `cd3.ps` |
| Block Property | `blockprop.ps` | `belaman10.eps` | `hblockprop.ps` | `cd4.ps` |
| Materials Library | `fe_matlib.ps` | `bd_matlib.ps` | `hmatlib.ps` | **missing** |
| Circuit/Conductor Property | `circprop.ps` | `belaman12.eps` | `hcircprop.ps` | `cd5.ps` |
| Circuit/Conductor results | `circuitrslt.ps` | `belaman21.eps` | `hcircres.ps` | `cd7.ps` |
| X-Y Plot | `pltdlg.ps` | `belaman19.eps` | `hpltdlg.ps` | `cd6.ps` |

**Stale titles worth noting:** `belaman12.eps` and `hcircprop.ps` are captioned
"Conductor Property dialog" but the 2004 title bar says *Circuit Property* —
the old code reused the magnetics dialog title. femm-qt sets
`"Conductor Properties"` correctly, so regenerated figures will differ from the
old ones in the title bar. That is a fix, not a regression, but it changes the
picture relative to the surrounding prose in a way worth a glance.

---

## 4. Phase 2 blockers found (must be settled before the harness)

1. **The GUI is not separable from `main()`.** `gui/CMakeLists.txt` has a
   single `add_executable(femm_gui main.cpp mainwindow.cpp …)` — no library
   target. `tools/figgen` needs a `femm_gui_lib` static library holding
   everything except the entry point. Complication: `gui/main.cpp` is not only
   `int main()` (line 614) — the preceding ~600 lines are the solver-spawn and
   script-callback plumbing that the dialogs' host may need. The split is
   therefore *main.cpp → app_support.cpp (into the lib) + main.cpp (entry
   only)*, not simply "exclude main.cpp".

2. **Four results dialogs and the grid dialog are anonymous locals, not
   classes.** `Circuit/Conductor Properties` is built inline in each of
   `magnetics_handler.cpp:555`, `electrostatics_handler.cpp:183`,
   `heat_handler.cpp:125`, `curr_handler.cpp:224`, and Grid Size inline in
   `mainwindow.cpp:1615`. The rule "instantiate each dialog class directly,
   never synthesize clicks" cannot be met for figures 9, 20, 29, 37 and 11
   without first extracting these into named classes. Recommendation: extract
   them (they are small and near-identical — a single
   `ConductorResultsDialog` parameterized by physics would collapse all four),
   and treat it as a prerequisite commit with its own diff.

3. **Everything else is clean.** The 30 remaining dialogs are real classes, and
   most take only `QWidget *parent`. Population APIs already exist in the right
   shape — e.g. `MaterialEditorDialog::fromProp(const FMaterialProp&)`,
   `setName()`. Seven need a document: `ProblemDialog`, `BlockPropDialog`,
   `NodePropDialog`, `SegmentPropDialog`, `ArcPropDialog`, `MatLibDialog`,
   `PropertyListDialog` — all satisfied by constructing an empty
   `MagDocument` / `ElecDocument` / `HeatDocument` / `CurrDocument`, which are
   plain libfemm objects needing no GUI.

---

## 5. Out of scope, for completeness

**Toolbar strips (11 files, 16 call sites).** Not dialogs, but equally stale
(they are 2004 16-colour icons): `analmode.ps`, `belaman3.ps` ×2,
`belaman13.eps` ×3, `belaman18.eps`, `cd8.ps`, `gridbtn.ps` ×2,
`hplotbar.ps` ×4, `hresbar.ps`, `intplot.ps`, `modebtn.ps`, `spawnbtn.ps`.
If these are ever regenerated they need `QToolBar::grab()`, not a dialog
recipe — a different harness shape.

**Canvas / plot / schematic (15 files).** Explicitly excluded by the brief:
`ax1.ps`, `bhcurve.ps`, `eqmag.ps`, `ezlam.ps`, `hardlam.ps`, `lamscheme.ps`,
`manf.ps`, `manfshft.ps`, `mass.ps`, `mcontour1.ps`, `open1.ps` (`width=6in`),
`open1b.ps` (`width=6in`), `recoil.ps`, `belaman20.eps`, `thisside.ps`.
Note `belaman20.eps` and `thisside.ps` are **the same contour-direction
drawing stored twice**.

**Unreferenced files on disk (12):** `belaman1.eps`, `belaman2.eps`,
`belaman3.eps`, `belaman4.eps`, `belaman5.eps`, `belaman14.eps`,
`belaman15.eps`, `belaman16.eps`, `belaman17.eps`, `grafmode.ps`, `icon.ps`,
`viewbtn.ps` — leftovers from earlier editions; leave them alone. (Note
`belaman3.eps` is unreferenced while `belaman3.ps` *is* used — same stem,
different extension, easy to confuse when writing recipes.)

---

## 6. Toolchain migration to pdflatex + PNG (done)

Decision: move the manual off `dvips` rather than accept the EPS-only
constraint. This removes the Phase 3 blocker entirely — regenerated dialog
figures can be plain PNGs.

**What made it safe:** every one of the 61 figures is *already raster* —
zero vector operators in any of them (checked for `lineto`/`curveto`/`rlineto`
against `image`/`colorimage`). The "schematics" and "plots" are rasterised
bitmaps too. So converting to PNG loses nothing; there was no vector artwork
to protect.

**Figure conversion.** All 61 rendered at **72 dpi**, which is a 1:1 pixel
transcription of the embedded raster (native is 1 px per pt, see the
correction above), and written with a pHYs chunk of 72 dpi. Verified: output
pixel dimensions equal the source BoundingBox in points for all 61, so
pdflatex computes exactly the natural size the BoundingBox used to give and
every figure prints at its original size. The `.ps`/`.eps` originals are left
in place.

**Source changes** (`manual.tex` preamble + 71 include sites):

```
% !TEX program = latex          ->  pdflatex
\usepackage[dvips]{graphicx}    ->  \usepackage{graphicx}
                                  + \DeclareGraphicsExtensions{.png,.pdf,.jpg,.eps,.ps}
\usepackage[dvips]{hyperref}    ->  \usepackage{hyperref}
\includegraphics{name.ps}       ->  \includegraphics{name}      (all 71)
```

Extensions are now **stripped from every include**, which is what gives the
drop-in property the harness wants: `figgen` writes `griddlg.png`, the
extension search finds it first, and no `.tex` is touched. `.ps` stays last in
the search order as a fallback. `pslatex` needs no change; there are no
`\special`, psfrag or pstricks constructs anywhere.

**Build verification** (`pdflatex` ×3, from `femm-qt/manual`):

| check | dvips build | pdflatex build |
|---|---|---|
| pages | 163 | **163** |
| errors / rerun requests | — | **0 / 0** |
| image XObjects in PDF | 61 | **61** |
| Overfull hboxes | 31 | **31** (same set) |
| PDF size | 1,212,216 B | **953,254 B** (−21%) |
| PostScript figures still pulled in | — | **none** (log shows only `.png`) |

All 163 pages were rendered from both PDFs and compared pixel-wise. 160 differ
by 1–2% of pixels; inspection of the worst (p019, a pure-table page with no
figures at all) shows identical layout with differences confined to hairline
rules and glyph edges — the expected dvips-vs-pdflatex Type 1 rasterisation
difference. Figure pages (checked p028, Block Property) show the figure at the
same position, same size, same caption, no clipping. Text pages (checked p157)
match line break for line break.

### Consequence for Phase 2/3

- Phase 3's "emit PNG" plan works as originally written.
- The BoundingBox sizing trap is **gone**: a PNG's printed size is
  `pixels ÷ pHYs dpi`. `figgen` must therefore write a correct DPI into each
  PNG — for a 2× DPR grab of a dialog that is logically *W*×*H* pt, emit
  2·*W* × 2·*H* px tagged **144 dpi** to keep today's printed size, and the
  figure gains real resolution rather than growing on the page.
- Everything in §1–§5 (the recipe list, the state to populate, the two
  blockers) is unaffected.

---

## 7. Status: what is regenerated and what is still 2004

25 of the 35 dialogs now come from the live femm-qt GUI (commits aacaa12,
23ea2f0). Everything below is still a 2004 Windows screenshot.

### 7a. Dialogs still to do (none)

| figure | tex:line | caption | why it did not automate |
|---|---|---|---|
| ~~`griddlg`~~ | manual:972 | Grid Properties dialog. | **DONE** — the duplicate `S` accelerator was fixed (Set Grid Si&ze), so it now automates. The Coordinates (Cartesian/Polar) combo it had lost was since restored, so the dialog is titled Grid Properties again and the 2004 prose came back verbatim. The recipe now opens it by mnemonic (`%g` then `z`); the old `{DOWN}`×3 walk stopped landing on the item. |
| ~~`bhdata`~~ | manual:1326 | B-H data entry dialog. | **DONE** — captured by hand; the click path (material editor, tick Nonlinear B-H curve, a material that has a curve) was not worth automating for one figure. |
| ~~`pltdlg`~~ | manual:1869 | X-Y Plot dialog (magnetics) | **DONE** — the recipe now opens a setup `.py` instead of a document (a positional script runs and leaves the GUI up, unlike `-run`), which solves, loads the solution and lays a two-point contour across the model. The dialog is then opened by invoking the toolbar action through UI Automation (`"@Plot X-Y"`), since the menu mnemonic did not survive SendKeys here. |
| ~~`belaman19`~~ | manual:2724 | X-Y Plot dialog (electrostatics) | **DONE** — as above. Electrostatics and heat flow had been using a bare `QInputDialog` with no point count; all four physics now share one dialog (`gui/xy_plot.h`). |
| ~~`hpltdlg`~~ | heatflow:378 | X-Y Plot dialog (heat) | **DONE** — as above. |
| ~~`cd6`~~ | cflow:381 | X-Y Plot dialog (current flow) | **DONE** — as above. |
| ~~`circuitrslt`~~ | manual:2268 | Circuit results dialog (magnetics) | **DONE** — setup `.py` solves `tutorial.fem` (which has a circuit) and the toolbar action is invoked by UI Automation. The `.ans`-plus-menu recipe never worked; a setup script is the general answer for postprocessor figures. |
| ~~`belaman21`~~ | manual:2982 | Conductor results dialog (electrostatics) | **DONE** — `bdemo2.fee`. Note the model has to have a conductor defined or the dialog just reads "No conductors defined in this problem." |
| ~~`hcircres`~~ | heatflow:506 | Conductor results dialog (heat) | **DONE** — `testing/heatFlow1.feh`; no *example* heat model defines a conductor. |
| ~~`cd7`~~ | cflow:484 | Conductor results dialog (current flow) | **DONE** — `cdemo2.fec`. Its numbers match the 2004 figure to 5 digits (100 V, 1.6877e-4 A, -5.925e5 Ohms), an incidental cross-check on the physics. |

**All 35 dialog figures now come from the current GUI.** What remains of the
2004 artwork is the 11 toolbar strips (§7c) and the canvas/plot/schematic
figures, which were always out of scope.

The general recipe for a postprocessor figure, worth reusing: point the
recipe at a setup `.py` rather than a document. A positional script runs and
leaves the GUI open (unlike `-run`), so it can solve, load the solution and
set up whatever state the dialog needs — a contour for the X-Y plots, a model
with a circuit or conductor for the results dialogs. Then open the dialog by
invoking the toolbar action through UI Automation (`"@Plot X-Y"`,
`"@Circuit Props"` / `"@Conductor Props"`) instead of typing a menu path.

### 7d. Figure sizing: one scale, not one height each

The first rule was "match the old figure's height, falling back to its width
when the modern dialog is proportioned too differently". That was wrong, and
the manual showed it once enough figures had been replaced: **the 2004 figures
were not captured at a consistent scale themselves**, so matching them one at a
time inherited their inconsistency. The implied scale across the regenerated
dialogs ran from **0.31 to 1.51** — the same 9pt dialog font printing five
times larger in one figure than another.

Superseded by one factor for every dialog screenshot:

```latex
\newcommand{\dlgscale}{0.9}        % manual.tex preamble
\usepackage[export]{adjustbox}      % supplies the max width= key
\includegraphics[scale=\dlgscale,max width=\textwidth]{probdef}
```

One number governs 36 figures.

**Picking the factor.** Body text is 12pt. Captures are 96 dpi and a PNG prints
1 px = 1 pt, so the dialogs' Segoe UI 9pt font (12 px per em) lands at
`12 × \dlgscale` pt on paper. That makes the choice legible rather than
arbitrary:

| `\dlgscale` | dialog text | vs 12pt body |
|---|---|---|
| 0.6667 | 8.0 pt | 67% — *the first attempt; user reported it as "a lot smaller"* |
| 0.75 | 9.0 pt | 75% — true physical size |
| **0.9** | **10.8 pt** | **90% — current** |
| 1.0 | 12.0 pt | 100%, but five more figures overflow |

0.9 was free: the next-widest dialog after 576 px is 502 px, so raising the
scale from 0.85 all the way to 0.9 caps no additional figure and costs no
extra page. (For reference, the 2004 manual effectively used scale 1.0 — its
figures printed 1 px = 1 pt — but its dialogs were physically smaller.)

**The cap.** Five dialogs are too wide for the text block at 0.9 — the three
Material Library figures at 702 px, `bhcurve` at 642 px and `bdryprop` at
576 px. `max width=\textwidth` shrinks exactly those to fit and leaves every
other figure at the uniform scale, so changing `\dlgscale` later cannot leave a
stale exception list behind. Recapturing the Material Library narrower was
considered and rejected: its two panes need ~640 px before the longest entry
("Metals Handbook DC Magnetization Curves") starts clipping.

Two categories stay outside the rule, for reasons rather than convenience:

- the **11 toolbar strips** print 1:1. They are rows of 16px icons that any
  non-integer downscale turns to mush, and they are already mutually
  consistent.
- **`mass` and `mcontour1`** use `width=\textwidth`. They are pictures rather
  than windows — no dialog font to keep consistent — and both are far too
  large for 2/3 anyway.

Cost: 163 pages → **160**.

### 7b. Also captured but not yet used

`cc_matlib.png` — the **current-flow Material Library**, which the manual has
never had a figure for (see §2). Ready to drop in if a figure and caption are
added to `cflow.tex`.

### 7c. Out of the dialog scope (26; the 11 toolbars are now done)

**~~Toolbar strips (11)~~ — DONE.** `analmode`, `belaman3`, `belaman13`,
`belaman18`, `cd8`, `gridbtn`, `hplotbar`, `hresbar`, `intplot`, `modebtn`,
`spawnbtn`, regenerated by **`C:/temp/toolbargen.ps1`** (companion to
`figgen.ps1`; a toolbar group is not a window, so each figure is the union of
its actions' UI Automation bounding rectangles, cropped out of a full-window
grab).

Two things worth knowing before touching this again:

- femm-qt reuses the 2004 icon bitmaps **pixel for pixel**, in the same order
  and grouping, so the old figures were never *wrong* about which button does
  what. What dated them was the button chrome: Win95 drew a raised 3D bevel
  around every toolbar button and Qt draws them flat. The figures print at
  natural size (1 px = 1 pt) and the icons are unchanged, so every horizontal
  strip lands within a few points of the figure it replaces — no `[height=]`
  needed.
- **`gridbtn` and `belaman3` were genuinely wrong**, not merely old: both show
  horizontal strips, but femm-qt puts those groups in the *vertical* left nav.
  That cost the manual a page and made two prose fixes necessary — the grid
  paragraph identified buttons as "left-most"/"right-most" (now top/bottom).

Recipe gotchas: UIA strips a trailing ellipsis from an action's name
("Contour Plot...", not found; "Contour Plot", found), and several toolbar
actions share a name with a menu-bar item ("Mesh", "Analyze", "Results"), so
matches that are `ControlType.MenuItem` have to be skipped -- otherwise the
union silently grows to span the menu row. Filtering to `ControlType.Button`
instead does not work: the checkable actions (Point Values, Show Mesh, Show
Grid) are not Buttons.

**Canvas screenshots (5)** — old FEMM renderings of models, would need
rebuilding from the example files: `mass` (Triangulation of Massachusetts),
`mcontour1`, `ax1`, `open1`, `open1b`.

**Data plots (4)** — B-H and demagnetization curves; content is physics, not
UI, so they only need redoing if the underlying plots should look modern:
`bhcurve`, `manf`, `manfshft`, `recoil`.

**Line drawings (6)** — hand-drawn schematics that do not age and need
nothing: `eqmag`, `ezlam`, `hardlam`, `lamscheme`, `belaman20`, `thisside`
(the last two are the same drawing stored twice).
