# femm-qt Postprocessing Verification — Test Report

**What this is:** femm-qt (a Qt6/Python rebuild of FEMM 4.2)
**Goal:** show that, for the same model, femm-qt reports the same numbers as the
original FEMM — checked value-by-value across all four physics.
**Compared against:** Original FEMM 4.2 (D. Meeker), Lua 4 scripting.
**Report date:** 2026-07-19
**Location of artifacts:** `femm-qt/testing/`

---

## 1. What This Covers

femm-qt is a Qt6 rebuild of FEMM 4.2, with a Python scripting layer in place of
FEMM's Lua. Since so much work relies on FEMM's numbers, a core goal for femm-qt
is a simple one: **for the same model, it should give the same answers as the
original.**

This report is about how we check that. Every value FEMM can report to a script —
each field integral, terminal quantity, and point value — has a matching test
that runs the same query in both femm-qt and the original FEMM (referred to here
as "OF") and compares the two results. If a number is off, the test flags it.

The queries covered span all four physics — magnetics (`mo_*`), electrostatics
(`eo_*`), current flow (`co_*`), and heat flow (`ho_*`):

| What's reported | Query family |
|---|---|
| Integrals over an area (energy, force, losses, …) | `*_blockintegral` |
| Integrals along a line or contour (flux, MMF drop, …) | `*_lineintegral` |
| Air-gap forces, torques, and field harmonics (motors) | `mo_gapintegral`, `mo_getgapharmonics`, `mo_getgapb`, `mo_getgapa` |
| Circuit / conductor terminals (current, voltage, flux, charge, …) | `mo_getcircuitproperties`, `eo/co/ho_getconductorproperties` |
| The field at a single point | `*_getpointvalues` |
| The underlying B–H material curve | `mo_getpointvalues` on a prescribed-field fixture |

A few things are **not** covered here: the GUI itself, the internals of mesh
generation, and the field solve on its own. The solve is still checked, just
indirectly — if femm-qt solved the field incorrectly, its reported values would
disagree with OF and these tests would catch it.

---

## 2. Verification Method

### 2.1 Golden-value comparison against an independent oracle

For each postprocessing surface and physics, a matched pair of test drivers was
written:

- `*Test.py` — runs on femm-qt (headless MicroPython).
- `*Test.lua` — runs the identical test logic on Original FEMM (Lua 4).

The OF driver's output is captured once and checked in as the **golden reference**
(`OF_*_result.txt`). The femm-qt driver's output is regenerated on every run and
compared to the golden by a physics-specific **comparator** (`*Compare.py`), which
reports the number of values agreeing within tolerance and itemizes any that do
not. Because the two implementations share no code in the reporting path, an
agreement is strong evidence of correctness, not of a shared defect.

Both drivers emit each returned value as a `re im` pair, so complex (AC) and real
(DC/static) results are compared field-for-field in one format.

### 2.2 Tolerance rationale (mesh-dependent vs. mesh-independent)

Tolerances are set by the physics of each comparison, not chosen uniformly:

- **Mesh-dependent quantities** (integrals and point values on real geometry).
  femm-qt and OF use different mesh generators (femm-qt's *tangle* vs. Triangle),
  so identical algorithms produce slightly different meshes and therefore
  slightly different discrete results. Integrals average this out (typ. <0.3%);
  single-point values do not and are more sensitive, particularly in saturated
  iron where a small B difference is amplified through the nonlinear permeability.
  Tolerances: **2%** for integrals and terminal properties, **3%** for point
  values on real geometry.

- **Mesh-independent quantities.** The B–H constitutive check (`block.fem`) uses a
  prescribed-potential boundary that makes the field exactly uniform, removing all
  mesh dependence; femm-qt and OF must then agree to solver precision. Tolerance:
  **1e-5** (measured agreement ~4e-7).

### 2.3 Comparator normalization

Naive relative error mis-ranks near-zero components (a transverse field riding on
a large dominant one, an unexcited circuit, a symmetric machine's even harmonics).
Each comparator therefore normalizes a component against the appropriate physical
scale: vector components against the local field magnitude; force/torque against
the dominant force/torque of the run; harmonics/gap-B against the section peak;
scalars by relative error. This floors legitimately-near-zero values while keeping
the meaningful ones under a tight bound.

### 2.4 Bug-isolation technique

Where a discrepancy could be mesh or a genuine defect, two techniques
disambiguate: (a) load OF's own `.ans` (its exact mesh and solution) into femm-qt
and recompute — identical input isolates the algorithm; (b) a prescribed-field
fixture (`block.fem`) that removes mesh from the comparison entirely. Both were
used; (b) exposed the lamination-fill-factor defect (§5).

---

## 3. Test Environment

| Item | Value |
|---|---|
| Component | femm-qt, branch `master` (local) |
| Build | CMake + Ninja, Qt 6.10.3 / MinGW 13.1.0 (GCC), Release |
| Solvers | `fkn` (magnetics), `belasolv` (electrostatics), `csolv` (current flow), `hsolv` (heat) |
| Postprocessor | libfemm + MicroPython scripting |
| Host OS | Windows 11 |
| Reference | Original FEMM 4.2 (`femm42src`), Lua 4 |
| Invocation | `femm_headless -run <driver>.py` (batch mode) |

---

## 4. Test Inventory and Results

Result column: **values compared / values in agreement** at the stated tolerance.
Full inventory of drivers, comparators, and goldens is in the Appendix.

> **Results at a glance (full re-verification, 2026-07-19):** 18 suites, **2,020
> values compared, 2,020 in agreement, 0 flagged.** Every suite regenerated its
> femm-qt result from source and re-diffed against its checked-in OF golden in a
> single automated pass (`_verify_all.sh`).

### 4.1 Block integrals

| Suite | Fixtures | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics | RadialMagneticBearing, roters1b, torque, PMTest, 3× Lorentz | types 0–30 (energy, area/volume, losses, Lorentz + Henrotte force/torque, incremental) | 2% | 437 / 437 |
| Electrostatics | electrostatic1/2 | types 0–6 (energy, area/volume, avg D/E, WST force/torque) | 0.1% | 14 / 14 |
| Heat flow | heatFlow1/2 | types 0–4 (avg T, area/volume, avg F/G) | 0.1% | 10 / 10 |
| Current flow | currentFlow1–6 | types 0–11 (real/reactive/apparent power, energy, area/volume, WST force/torque) | 2% | 48 / 48 |

### 4.2 Line / contour integrals

| Suite | Fixtures | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics | tutorial, tutorial_planar, RMB, roters1b, torque | types 0–5 (B.n, H.t, length/area, force, torque, (B.n)²) | 2% | 30 / 30 |
| Electrostatics | electrostatic1/2 | types 0–4 (E.t, D.n, length/area, force, torque) | 2% | 18 / 18 |
| Current flow | cf1, cf3, cf4, cf5 | types 0–3 (E.t, J.n, length/area, avg V) | 2% | 30 / 30 |
| Heat flow | heatFlow1/2 | types 0–3 (G.t, F.n, length/area, avg T) | 2% | 15 / 15 |

Physical cross-checks embedded in the line suites: Ampère (∮H·dl = enclosed
amp-turns), Gauss (∮D·n = enclosed charge), current continuity (∮J·n = injected
current), heat-flow conservation (∮F·n = 100 W); each stress-tensor line
force/torque was additionally cross-checked against the block WST value on the
same body.

### 4.3 Air-gap-element integrals (planar magnetics)

| Suite | Fixtures | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics AGE | RadialMagneticBearingAGE (force), rolled (torque) | `mo_gapintegral` types 0–6 (DC/2×/incremental torque & force, stored energy), `mo_getgapharmonics`, `mo_getgapb`, `mo_getgapa`; DC, AC 60 Hz, incremental | 2% | 232 / 232 |

### 4.4 Circuit / conductor terminal properties

| Suite | Fixtures | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics circuits | RMB, roters1b, torque, 3× Lorentz | current, voltage drop, flux linkage; DC/AC/incremental | 2% | 105 / 105 |
| Electrostatics cond. | electrostatic1/2 | voltage, charge | 2% | 4 / 4 |
| Current-flow cond. | currentFlow1–6 | voltage, current | 2% | 20 / 20 |
| Heat cond. | heatFlow1/2 | temperature, heat flow | 2% | 4 / 4 |

### 4.5 Field point values

| Suite | Fixtures | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics | RMB, roters1b, torque, PMTest | 14 values (A, B, σ, energy, H, Je, Js, μ, Pe, Ph, ff) at air/iron/coil/magnet points, DC & AC | 3% | 434 / 434 |
| Electrostatics | electrostatic1/2 | 8 values (V, D, E, ε, energy) | 2% | 64 / 64 |
| Current flow | cf1, cf3, cf5, cf6 | 15 values (V, J, K, E, ε, Jd, σ, Jc) | 2% | 135 / 135 |
| Heat flow | heatFlow1/2 | 7 values (T, F, G, κ) | 2% | 42 / 42 |

### 4.6 Constitutive (B–H) evaluation — mesh-independent

| Suite | Fixture | Quantities | Tol | Result |
|---|---|---|---|---|
| Magnetics B–H | block.fem (prescribed-A M-19 cube, B = Bdes) | 14 point values swept over Bdes ∈ [0.1, 2.3] T × {DC, 60 Hz, 400 Hz} | 1e-5 | 378 / 378 |

Measured agreement across the B–H sweep: max relative error **7.1e-7**.

---

## 5. Defects Found and Dispositioned

The verification uncovered nine defects in femm-qt, each confirmed against OF,
fixed, and re-verified. All are closed on branch `master`.

| # | Defect | Surface | Root cause | Fix (commit) |
|---|---|---|---|---|
| 1 | AC hysteresis-loss sign flip | mo_blockintegral (3) | sign error in the AC loss term | block-integral work |
| 2 | B–H energy integrated by trapezoid | mo_blockintegral (2/17) | used trapezoid instead of OF's cubic-Hermite spline; few-% error at the B–H knee | block-integral work |
| 3 | Axisymmetric WST mask on the r=0 axis | mo_blockintegral (IsKosher) | mask not pinned to the axis for on-axis bodies | block-integral work |
| 4 | Reactive/apparent power from `E·E` | co_blockintegral (1/2) | un-conjugated phasor product (spurious 2θ phase); should be `S = ∫½ J·conj(E)` | 4ca28c3 |
| 5 | Planar swept-area returned 0 | mo_lineintegral (2) | only the axisymmetric branch implemented; planar `length×Depth` missing | da81e57 |
| 6 | avg V / avg T degenerate on axis | co/ho_lineintegral (3) | 2πr-weighted 0/0 on r=0 → NaN in OF; femm-qt now falls back to the plain arc-length average (the r→0 limit) | 505a8d9 |
| 7 | Air-gap-element name unquoted in AC `.ans` | solver (prob2big) | name written without quotes/newline; postproc loader dropped the element → all AC gap results 0/empty | b67c09a |
| 8 | AGE name re-quoted on incremental reload | solver (femmedoccore) | reloaded name kept its quotes and was re-quoted → all incremental gap results 0/empty | 76f7557 |
| 9 | Lamination fill factor double-counted in DC point μ | mo_getpointvalues (real GetMu) | GetH already bakes in LamFill; GetMu re-applied it → systematic ~2% μ/H error in laminated nonlinear iron at DC | 6314d92 |

**Notes.**
- Defect 9 was invisible to every integral-level test (which route through the
  correct complex GetMu or sample air) and appeared only in the DC point μ/H
  report. It was indistinguishable from mesh scatter until `block.fem` removed
  mesh from the comparison, at which point the error resolved to an exact factor
  of `LamFill` (0.98). Its AC counterpart was verified correct.
- Defects 7 and 8 silently zeroed every air-gap result at nonzero frequency; the
  purpose-built `rolled.fem` (a torque-producing machine) and the AC/incremental
  sweeps were required to exercise them.

---

## 6. Deliberate Deviations From the Reference

Not every difference from OF is a defect. The following is an intentional
improvement, documented for traceability:

- **Axisymmetric block-integral quadrature.** femm-qt uses quadrature consistent
  with the axisymmetric solver's element model (`v = 2πrA` linear in (r², z)),
  including closed-form radial moments, where OF uses a centroid rule that makes
  O(1)-relative errors on axis-adjacent elements. femm-qt therefore differs from
  OF by up to ~0.3% on axisymmetric WST/energy values at practical mesh densities;
  both converge to the same limit under refinement, and femm-qt converges
  monotonically with markedly less scatter. This is within the 2% suite tolerance.

---

## 7. Reproduction Procedure

From `femm-qt/testing/`, for any suite (example: magnetics point values):

```
femm_headless -run MOPointValueTest.py > fq_pv_result.txt
python3 MOPointValueCompare.py OF_pv_result.txt fq_pv_result.txt
```

The comparator prints `compared N ... : M agree ... , K flagged` and exits
nonzero if `K > 0`, so it can gate a build. To regenerate a golden, run the
corresponding `*Test.lua` on Original FEMM from this directory and save its output
as the `OF_*` file. `fq_*` captures are regenerated on every run and are not
checked in; the `OF_*` goldens are.

A full re-verification of all suites is scripted (see `_verify_all.sh`, generated
for this report).

---

## 8. Summary

Every result femm-qt can report — across magnetics, electrostatics, current flow,
and heat, and across every kind of query (integrals over areas and contours,
air-gap forces and torques, circuit and conductor terminals, point-by-point field
values, and the underlying B–H material curve) — is checked value-for-value
against the original FEMM, and all of it agrees within the expected tolerances.

Getting there also turned up **nine real bugs**, each now fixed — several of which
only showed up under fixtures built specifically to expose them. And because every
reference value is checked in and every test regenerates its own numbers from
scratch, anyone can reproduce these results for themselves (§7).

In short: if the original FEMM reports it, femm-qt is tested against it.

---

## Appendix A — Suite / Comparator / Golden Cross-Reference

| femm-qt driver | OF driver | Comparator | Golden |
|---|---|---|---|
| MOBlockIntegralTest.py | MOBlockIntegralTest.lua | MOBlockIntegralCompare.py | OF_result.txt |
| ElecBlockIntegralTest.py | ElecBlockIntegralTest.lua | EOBlockIntegralCompare.py | OF_elec_result.txt |
| HeatBlockIntegralTest.py | HeatBlockIntegralTest.lua | HeatBlockIntegralCompare.py | OF_heat_result.txt |
| CurrBlockIntegralTest.py | CurrBlockIntegralTest.lua | CurrBlockIntegralCompare.py | OF_curr_result.txt |
| MOLineIntegralTest.py | MOLineIntegralTest.lua | MOLineIntegralCompare.py | OF_line_result.txt |
| EOLineIntegralTest.py | EOLineIntegralTest.lua | EOLineIntegralCompare.py | OF_eline_result.txt |
| CILineIntegralTest.py | CILineIntegralTest.lua | CILineIntegralCompare.py | OF_iline_result.txt |
| HILineIntegralTest.py | HILineIntegralTest.lua | HILineIntegralCompare.py | OF_hline_result.txt |
| MOGapIntegralTest.py | MOGapIntegralTest.lua | MOGapIntegralCompare.py | OF_gap_result.txt |
| MOCircuitPropTest.py | MOCircuitPropTest.lua | MOCircuitPropCompare.py | OF_circ_result.txt |
| EOCondPropTest.py | EOCondPropTest.lua | CondPropCompare.py | OF_eic_result.txt |
| COCondPropTest.py | COCondPropTest.lua | CondPropCompare.py | OF_cic_result.txt |
| HOCondPropTest.py | HOCondPropTest.lua | CondPropCompare.py | OF_hic_result.txt |
| MOPointValueTest.py | MOPointValueTest.lua | MOPointValueCompare.py | OF_pv_result.txt |
| MOBHCurveTest.py | MOBHCurveTest.lua | MOBHCurveCompare.py | OF_bh_result.txt |
| EOPointValueTest.py | EOPointValueTest.lua | ScalarPointValueCompare.py | OF_eopv_result.txt |
| COPointValueTest.py | COPointValueTest.lua | ScalarPointValueCompare.py | OF_copv_result.txt |
| HOPointValueTest.py | HOPointValueTest.lua | ScalarPointValueCompare.py | OF_hopv_result.txt |

## Appendix B — Defect Fix Commit Record

```
6314d92  solver: fix lamination fill factor double-counted in DC point-value permeability
76f7557  solver: strip quotes/newline when reloading an air-gap-element name (fkn incremental)
b67c09a  solver: write air-gap-element name quoted + newline in the AC .ans (prob2big)
505a8d9  solver: average voltage/temperature line integral falls back to plain average on the axis
da81e57  solver: fix planar swept-area in mo_lineintegral; add MO line-integral suite
4ca28c3  solver: fix current-flow reactive/apparent power; add block-integral suite
```
(Defects 1–3 were fixed during the magnetics block-integral suite development; see
that suite's commit history.)
