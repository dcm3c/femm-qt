# Eccentric sliding band

Design document. Status: **proposed** (fleshed out 8 Aug 2026, not implemented).

## Motivation

The sliding band (the boundary condition called "AGE" in the code; see
*Sliding band motion model for electric machines*, Meeker 2018) models rotation
without remeshing: the rotor and stator meshes never move, and the whole motion
lives in the band's interpolation mapping. It currently requires the two
circular boundaries to be **concentric**. This shuts out a class of problems
that matter for machine noise and bearing work:

- **static eccentricity** — the rotor axis is displaced but fixed in the stator
  frame (mounting/tolerance error);
- **dynamic eccentricity** — the displacement is fixed in the *rotor* frame, so
  it rotates with the rotor (bent shaft, whirl);
- **mixed** — both at once, the generic real machine;
- the resulting **unbalanced magnetic pull** (UMP) and its sidebands, which are
  first-class inputs to vibration and acoustic-noise prediction.

The goal is to admit per-side translation with the same character the band
gives rotation: no remeshing, no new nodes, no change of mesh connectivity —
so the transient mesh-reuse machinery (`[AgeRings]`, `BuildAgeRing`,
`LoadPrev`) carries over unchanged, and `A_prev` correspondence is preserved
trivially in a transient march.

## Background: what the band is today

Anchors into the current implementation:

- The band is a stationary ring of `totalArcElements` rectangular elements in
  mid-gap, aspect ratio `K = (ro-ri)/(R*dtta)` with `R = (ri+ro)/2`
  (`prob1big.cpp:276-278`). The rectangles own no nodes: each element's corner
  values are cubic-Hermite interpolated from 5 inner + 5 outer mesh-edge nodes
  (the `nn[10]`/`ww[10]` stencil, `prob1big.cpp:352-400`).
- The element matrix is the closed-form 10×10 `MG` — the symbolic expansion of
  `T_c^T M_quad T_c` — a polynomial function of exactly three scalars: the
  fractional misalignments `ci`, `co` and the aspect ratio `K`
  (`prob1big.cpp:295-349`). Because the shift is uniform around the band, `MG`
  is computed **once per AGE** and added for every element.
- Rotation is carried entirely by the node pairing (`BuildAgeRing`: sort ring
  nodes by angle after applying `InnerAngle`/`OuterAngle`) plus the fractional
  shifts `InnerShift`/`OuterShift` that become `ci`/`co`.
- Postprocessing computes per-element `Br`, `Bt` at element centers, takes
  their DFT, and evaluates force/torque as harmonic sums of the Maxwell stress
  along the fixed circular band centerline; the same coefficients drive the
  continuous gap-field reconstruction (`mo_getgapb`-family) and the gap
  integrals.

Everything below is a perturbation of this structure, not a replacement.

## Parameterization: per-side displacement in the side's own frame

Each air gap boundary property gains two pairs:

```
<InnerDx> <InnerDy>     displacement of the inner-side member
<OuterDx> <OuterDy>     displacement of the outer-side member
```

**Frame convention (the load-bearing decision):** each pair is expressed in
the reference frame *fixed to that side*, i.e. rotated by that side's current
angle. Mapping to the static (band) frame:

```
d_s = Rot(angle_s) * (dx_s, dy_s),    s in {inner, outer}
```

where `angle_s` is `InnerAngle` or `OuterAngle`. This makes the two canonical
cases trivial for a driver:

- constant `(OuterDx, OuterDy)` on the stationary side = **static
  eccentricity** (fixed direction in the stator frame);
- constant `(InnerDx, InnerDy)` on the rotating side = **dynamic
  eccentricity** — the static-frame displacement direction rotates with
  `InnerAngle` automatically, with no per-step bookkeeping in the driver;
- both = mixed eccentricity; arbitrary whirl orbits by varying the values
  per step like the angles.

(For an outer-rotor machine the roles simply swap; nothing in the formulation
distinguishes which side spins.)

The displacement is a **boundary-condition parameter, not a coordinate
transform** — exactly like the angles. No mesh node moves; the rotor mesh
stays where it was meshed, and the entire effect enters through the band's
per-element geometry and interpolation weights.

## Geometry: per-element thickness and tangential shift

Let `theta_j` be the center angle of band element `j` in the static frame,
`rhat = (cos theta_j, sin theta_j)`, `that = (-sin theta_j, cos theta_j)`,
`g = ro - ri`, and `d_i`, `d_o` the static-frame displacement vectors from
above. Evaluate both shifts **at each element center** (the deliberate
first-order simplification):

**Radial projection → local thickness.** A side displaced along `+rhat` at
`theta_j` moves its surface toward larger radius there:

```
t_j = g - (d_i - d_o) . rhat(theta_j)
```

Only the *relative* displacement changes thickness, as it must.

**Tangential projection → per-element misalignment.** The side's surface
slides tangentially by `d_s . that(theta_j)`, which adds to that side's
fractional shift:

```
dshift_s,j = ( d_s . that(theta_j) ) / ( R * dtta_rad )      [in units of dtta]

ci_j = frac( ci0 + dshift_i,j )        oi_j = floor( ci0 + dshift_i,j )
co_j = frac( co0 + dshift_o,j )        oo_j = floor( co0 + dshift_o,j )
```

where `ci0`, `co0` are today's uniform shifts. The **integer parts `o*_j`
offset the interpolation stencil** for that element (index arithmetic on the
`node[k-1 .. k+2]` window, modulo the ring). They are not exotic: with
`eps ~ g/2` and edge spacing `R*dtta ~ g`, `dshift` reaches O(0.5), so carries
across element boundaries will happen in practice and must be handled from day
one, per element, with the same wrap logic the stencil already uses at the
ring seam.

The existing `ci`/`co` relative-normalization step (`prob1big.cpp:283-291`)
applies per element after the above.

**What is neglected by center evaluation.** Within one element the thickness
actually varies by `~ eps * dtta` and the edges tilt; treating the element as
a rectangle of thickness `t_j` drops terms of relative size `O(dtta)` compared
to the retained `O(eps/g)` effect — the same order of approximation as the
piecewise-constant-B element itself. Consistency limits that must hold and
should be asserted by tests:

- `d_i = d_o = 0`: reduces **bit-for-bit** to the current band (fast path:
  keep the compute-`MG`-once branch when both displacements are zero).
- `d_i = d_o != 0` (common-mode translation): `t_j = g` everywhere, both
  shifts move identically — the solution is the rigidly translated field to
  first order in `|d|/R`, and forces/torque are unchanged.

## Element matrices

`MG` is already a closed-form function of `(ci, co, K)`. The eccentric band
evaluates the **same expressions per element**:

```
K_j = t_j / ( R * dtta_rad ),   MG_j = MG(ci_j, co_j, K_j)
```

i.e. the `MG` block moves inside the `k` loop with per-element inputs. Cost:
`n` evaluations of ~55 polynomial entries instead of 1 — microseconds at
`n ~ 10^2..10^3`, and only on the displaced path. No new element derivation,
and no change to the `c = 2/3` quad blend — see the appendix: `c = 2/3` is
the truncation-optimal blend at **every** aspect ratio, so the per-element
`K_j` needs no per-element `c`.

The matrix stays symmetric positive semi-definite element-by-element (it is
`T^T M T` with a PSD `M`), so nothing changes for the solver: same PCG/ILDL
path, same structure stability under the split-assembly scheme (the AGE block
already lives in the `sweepSel<=1` baseline).

## Hard constraint: full-circle models only

Eccentricity breaks sector periodicity: the displaced field carries `p +/- 1`
sidebands that are not periodic under sector rotation, and a displacement
vector cannot be replicated into `n1 > 1` periodic copies consistently.

**Rule: nonzero displacement requires `totalArcLength == 360`.** The solver
must reject any AGE with displacement parameters on a sub-360 sector with a
clear error, not a wrong answer. (This also sidesteps any question of how
`BuildAgeRing`'s periodic replication would interact with displacement — it
never sees one.)

`BuildAgeRing` itself is untouched in all cases: the node pairing depends on
angles only. Displacement enters strictly downstream, in the per-element
weights and thicknesses at assembly time.

## File formats and plumbing

- **`.fem`**: four new boundary-property keys `<InnerDx>`, `<InnerDy>`,
  `<OuterDx>`, `<OuterDy>`, defaulting to 0 when absent — same pattern as
  `<InnerAngle>`/`<OuterAngle>`. Units: problem length units, static
  magnitudes in the side-fixed frame as defined above.
- **`.ans` AGE parameter line**: append the four static-frame-mapped values
  (or the raw side-frame values plus angles — decide once; raw side-frame is
  more self-describing) after `OuterShift`. All existing readers parse this
  line positionally with `sscanf` and ignore trailing fields, so the extension
  is invisible to them; new readers default the fields to 0 when missing.
  This is the same additive-compatibility trick as `[DeltaT]` and
  `[AgeRings]`.
- **Transient mesh reuse**: `LoadPrev` already re-reads the *current* `.fem`'s
  boundary props for the angles; the displacement keys ride the same re-read.
  Ring lists, pairing, geometry guard: unchanged.
- **`mesh.h`**: `CAirGapElement` gains the four doubles (and `CBoundaryProp`
  gains the parsed keys), initialized to 0 everywhere an AGE is built.
- **Bindings/GUI**: `mi_addboundprop`/`mi_modifyboundprop` gain four indexed
  parameters; the boundary dialog gains four fields. Deferrable — the solver
  and file format are the contract; a driver can write the keys directly
  before the GUI catches up. (Note the standing rule: check octavefemm `*.m`
  for wire-format authority when the binding work happens.)

## Postprocessing

The band field evaluation is where the thickness genuinely matters:

- **Per-element field.** `Br` (tangential difference over `R*dtta`) is
  unchanged. `Bt` divides the radial difference by the **local** thickness:
  `Bt_j = (Aa + Ab - Ac - Ad) / (2 * t_j)`. Equivalently: tangential `H` in
  the gap (`H.t = Bt/mu0`) is what the constant thickness used to give for
  free and now needs `t_j`. The postprocessor must reconstruct `t_j` from the
  stored displacement fields exactly as the solver did — one shared helper,
  not two implementations.
- **DFT and the harmonic sums.** The DFT machinery is unchanged in form; it
  just transforms thickness-aware `Br`, `Bt` samples. The Maxwell-stress
  force/torque sums along the band centerline remain valid because the
  centerline is a fixed circle lying in air, and any closed path in air is a
  legitimate stress-tensor path. UMP appears automatically as the
  `p, p±1` harmonic products in the existing `Fx`/`Fy` sums — **no new force
  formula**. The torque sum likewise picks up its eccentric corrections
  through the coefficients.
- **Which numbers move.** Every gap quantity built from `Bt` shifts subtly at
  nonzero displacement (torque, forces, gap energy-type integrals, the
  reconstructed gap-field curves); `Br`-only quantities shift only through the
  solution itself. At zero displacement all of it is bit-identical to today —
  which is the regression gate.
- **Path validity bound.** The centerline stays inside air only while each
  side's radial approach is less than half the gap:
  `|(d_i - d_o) . rhat| < g/2` pointwise (element inversion happens later, at
  `t_j <= 0`). Enforce the tighter bound with a clear solver-side error;
  it is the honest limit of the formulation, and machines run at eccentricity
  ratios well inside it.

## Transient composition

Nothing new is needed — this is the point of the design:

- No mesh node moves for rotation *or* translation; the band owns no nodes and
  no state. `A_prev` correspondence in a transient march is preserved by
  construction, and the mesh-reuse path applies verbatim: reuse mesh, re-pair
  rings at the current angles, evaluate per-element band geometry from the
  current displacements.
- A whirl-orbit transient is then per step: driver writes
  `InnerAngle(t)` and, for dynamic eccentricity, *constant* `(InnerDx,
  InnerDy)` (the frame convention does the rotation); solver reuses the mesh
  and reassembles `n` band matrices plus the usual transient terms.
- Rotor-frame sources (PM magnetization, bar currents) are untouched for the
  same reason they are untouched under rotation today: the member's mesh and
  frame never move.

## Validation ladder

1. **Zero-displacement bit-compat.** Full suite, plus direct `.ans` compare on
   AGE fixtures (`my_SPM_motor_AGB`, the torque benchmark model): displaced
   code path disabled ⇒ byte-identical output.
2. **Common-mode invariance.** `d_i = d_o`: forces and torque equal to the
   concentric result to first order in `|d|/R`; field equals the translated
   concentric field.
3. **Exact eccentric benchmark (the strong oracle).** Extend the concentric
   torque benchmark (femm.info/wiki/TorqueBenchmark) to eccentric circles: the
   eccentric annulus maps conformally (Möbius/bipolar) to a concentric one,
   so a problem with prescribed harmonic `A` on both displaced circles has a
   closed-form field, force, and torque. This nails the thickness law, the
   shift law, and the force path in one fixture, in exactly the way the
   `c = 2/3` calibration was nailed for the concentric band.
4. **Classical UMP cross-check.** Small-eccentricity UMP vs the standard
   relative-permeance perturbation result for a uniform-gap machine
   (proportional to eccentricity ratio, direction along minimum gap for the
   static case); dynamic case: force vector rotates at rotor frequency in a
   transient march.
5. **Meshed-gap referee.** The same eccentric geometry solved with a
   conventionally meshed (triangulated) air gap at the displaced position —
   agreement to discretization tolerance, on both field and force.
6. **Smoothness.** Force vs displacement and force vs angle sweeps must be
   smooth (no element-transition artifacts): the eccentric analogue of the
   BEMF-smoothness property that motivated cubic interpolation.

## Explicit non-goals / later

- **AC solver (`prob2big`)**: same mechanical changes apply (its AGE block
  parallels `prob1big`'s); defer until a workload needs eccentric AC.
- **Axisymmetric**: not applicable (no planar band); axial motion in
  axisymmetric models is the mask-morph idea's territory (see the
  translational-motion design discussion).
- **Thickness-graded band elements** (shape functions linear in the *local*
  thickness rather than center-sampled): only if benchmark 3 shows the
  center-evaluation error matters at realistic eccentricity.
- **Aspect-ratio-corrected blend `c(k)`**: ruled out — see the appendix; the
  optimal `c` is `2/3` independent of `k`, so there is nothing to correct.
- **GUI dialog + full binding surface**: after the solver/file contract
  settles.

## Appendix: the `c = 2/3` blend is aspect-ratio-optimal (derived)

The quad element matrix family is `M(c) = c*M_tri + (1-c)*M_lin` (paper
eq. 21); FEMM uses `c = 2/3`, originally selected empirically against the
analytic torque benchmark. The truncation analysis below shows that value is
the unique optimum, at every aspect ratio, and explains what it is.

Setup: unrolled band, tangential node spacing `h_x = R*dtta`, radial
thickness `h_y = ro - ri`, aspect `k = h_y/h_x`. Assemble `M(c)` over a
uniform grid of such rectangles and read off the interior 9-point stencil
(verified symbolically, 8 Aug 2026):

```
center      C0 = (1+c)(k + 1/k)
theta-edge  Ct = -[c(k^2+1) + k^2 - 1] / (2k)
r-edge      Cr = -[c(k^2+1) - k^2 + 1] / (2k)
diagonal    Cd = -(1-c)(k + 1/k)/4
```

Second order: the stencil is a consistent, isotropic discrete Laplacian
(`-2 h_x h_y * Lap A`) for **all** `c` — consistency does not select `c`.

Fourth order, using harmonicity (`Lap A = 0` holds in the gap — air, no
sources): the leading truncation on the harmonic quartic `Re(z^4)` is

```
T4 = -2 h_x h_y (3c - 2)(h_x^2 + h_y^2)
```

(and vanishes identically on `Im(z^4)`). The aspect ratio factors out
completely: `c = 2/3` cancels the leading error at **every** `k`. This is
why the empirical calibration landed on a clean rational number — at `k = 1`
the `c = 2/3` stencil is exactly the classical fourth-order "Mehrstellen"
nine-point Laplacian `[10/3, -2/3, -1/6]`, and the family's optimality
condition turns out not to depend on `k` at all.

Sixth order: the residual at `c = 2/3` on the harmonic sextic is

```
T6 = 3 h_x h_y (h_x - h_y)(h_x + h_y)(h_x^2 + h_y^2)
```

which vanishes at `h_x = h_y` (`k = 1`) — the known superconvergence of the
Mehrstellen operator on Laplace solutions. Practical consequences:

- The **mesher**, not the solver, owns the remaining aspect-ratio lever:
  gap-edge spacing equal to the gap thickness (`k = 1`) buys two extra
  orders on the band's own contribution. No `c(k)` table exists to tune.
  **tangle already does this**: the AGE spacing clamp
  `altMaxSideLength = (360/pi)*(ro-ri)/(ro+ri)` (tangle.cpp, "Aspect ratio
  limit: don't want skinny gap elements") is arc spacing of exactly
  `ro - ri` at the mean radius — the heuristic is the superconvergence
  condition. Note `T6` is *zero* at `k = 1` but nonzero for modest
  over-refinement (`h_x` somewhat below `h_y`), so letting the clamp bind
  is a local optimum for the band's own error, not merely "good enough";
  drawn densities finer than the gap only pay off through the interpolation
  and the triangle regions, not the band stencil.
- The **eccentric band inherits fourth order for free**: `c = 2/3` stays
  optimal at each element's own `K_j`, whatever the local thickness. A band
  meshed at `k = 1` drifts off the sixth-order sweet spot only quadratically
  in eccentricity, through `T6 ~ (t_j^2 - h_x^2)`.

Caveats: this is the interior-stencil (periodic-extension) analysis. The real
band is one element tall with cubic-Hermite-interpolated corner values
between two triangle meshes, so realized global accuracy is also gated by
the interpolation and the triangle regions; `c = 2/3` is the condition that
the band's own contribution does not pollute the order — which is what the
torque benchmark confirmed end-to-end. Curvature (unrolling) error is
`O((g/R)^2)` relative and independent of `c`; at typical geometry
(`g/R ~ 0.03`) it is ~1e-3 relative and not worth chasing.

## Appendix B: a C2 band via quintic interpolation (ADOPTED as default)

Status update (8 Aug 2026): implemented behind the `QUINTIC` switch in
`common/band_interp.h` (the single shared define; solver and postprocessor
must agree — see the header) and made the default after A/B measurement on
the sliding-band paper's own BLDC (201-point sweep, 0.015 deg steps):
element-transition kinks in torque down 1.7x rms / 3.5x max, in flux
linkages down 1.9-2.6x rms / 3.7-5.6x max, values moved ~0.05%; the
spikes verified to sit at the 0.6 deg element-passing pitch and to be
flattened to the background ripple level. The cubic branch is retained
under `#ifndef QUINTIC` until more field experience accumulates. The
postprocessor's gap-field reconstruction (`ComputeAGEHarmonics`) carries
a matching branch — a mismatched solver/postproc pair produces
scheme-mismatch torque noise ~15x worse than either consistent scheme.

Idea: replace the cubic Hermite interpolation (paper eq. 15) with a quintic
whose endpoint second derivatives are rolled up from second central
differences (`f''_j = A_{j-1} - 2A_j + A_{j+1}`), giving flux linkage,
BEMF slope, and torque **C2** dependence on rotor position instead of C1.
Findings (sympy-verified, 8 Aug 2026):

- **Structurally free.** The FD data for interval [1,2] lives in the same
  `{A0..A3}` window Catmull-Rom uses, so the per-side stencil stays 5 nodes
  and the element matrix stays 10x10 — only the `MG` weight polynomials
  regenerate. Weights (eq-15 analogs, point `a` at `1-eps` from node 2):

  ```
  w0 = -e^3 (e-1)(2e-3)/2          w1 = e (6e^4-15e^3+9e^2+e+1)/2
  w2 = -(e-1)(6e^4-9e^3+2e+2)/2    w3 = e (e-1)^3 (2e+1)/2
  ```

  Partition of unity; identity at aligned positions; C2 across transitions
  (verified; the cubic is C1).
- **Does not disturb `c = 2/3`**: the optimality condition lives in the quad
  blend; the interpolation enters as `T^T M(c) T` and couples to the
  c-dependent part only beyond leading order. (Empirical confirmation
  already exists: the 2018 calibration ran with the cubic `T` active and
  landed exactly on the clean-grid optimum.) PSD is preserved for any `T`.
- **Does not raise the accuracy order** (exactness still stops at
  quadratics — the FD slope is inexact on cubics, a data limitation shared
  with Catmull-Rom), and the in-interval error constant is ~45% larger
  (6.4e-4 vs 4.4e-4 max interpolation error at `omega*h = 0.3`).

So: one more continuity order, in exchange for a modestly larger pointwise
error constant, at essentially zero structural cost. **Decision criterion:**
rerun the paper's BLDC BEMF experiment cubic-vs-quintic and compare spectra.
The C1 kinks sit at element-passing frequency (acoustic band); adopt the
quintic only if those lines are visible above the time-step/tolerance noise
floor. Escalation if both C2 *and* the better constant are ever needed:
fourth-order FD data — but that widens the window to 6 nodes/side (12x12
matrix), a real structural change held in reserve.
