# The `.tmp` sidecar: coupling an external network to the magnetics solver

**Status:** developmental interface, deliberately absent from the user
manual. It is stable enough to drive from a script today, and it is the
intended route for time-stepped simulation with an external circuit. The
section names and conventions below are what `fkn` reads as of October 2026
(`solvers/fkn/femmedoccore.cpp` parses the file, `solvers/fkn/prob1big.cpp`
assembles it). Expect the transport to change (an in-process call will
eventually replace the file); the quantities will not.

## 1. What it does

An ordinary FEMM circuit property prescribes a current. The sidecar lets a
circuit's current be an **unknown**, determined together with the field by
one extra equation per circuit of the form

    lambda_k  +  sum_j M[k][j] * i_j  (+ h * sum v_solid)  =  Lambda_k

where

| symbol | meaning | unit |
|---|---|---|
| `lambda_k` | flux linkage of circuit *k* computed from the field solution (turn-weighted, same quantity `mo_getcircuitproperties` reports) | Wb |
| `i_j` | current of circuit *j* (the real circuit current, not ampere-turns) | A |
| `M[k][j]` | coefficient supplied by the caller: external inductance, or a time-step times an external resistance, or any affine coupling | H |
| `v_solid` | axial voltage drop of a solid conductor in the circuit, an unknown the solver adds in a transient (section 3.1); the term appears only when `[DeltaT]` is set | V |
| `Lambda_k` | right-hand side supplied by the caller | Wb |

plus optional **Kirchhoff current-law (KCL) rows** over those currents,

    sum_j C[m][j] * i_j  =  I_m

whose multiplier is physically the voltage of the network node where the
constraint holds (a star point, an end ring).

The point of the arrangement is that any *linear* external network,
discretised in time by whatever rule you choose, is exactly a set of such
rows (section 5). The field and the network are then solved **together, in
one linear system per step**. There is no iteration between a field solve
and a separate circuit solver, which is where the time goes in a loosely
coupled scheme.

## 2. File location and when it is read

* The solver is invoked as `fkn <base>` and reads `<base>.fem`. The sidecar
  is `<base>.tmp`, in the same directory. `<base>` is the `.fem` path with
  its extension removed, so for `motor.fem` the sidecar is `motor.tmp`
  (not `motor.fem.tmp`). `mi_analyze()` and the GUI's Analyze pass exactly
  this base, so the sidecar works from a script, from the GUI, and from a
  direct `fkn` call alike.
* It is read **only for DC magnetics problems** (`[Frequency] = 0`). AC
  problems ignore it entirely.
* `[DeltaT]` is honoured for planar and axisymmetric problems. The circuit
  sections are honoured for **planar problems only**, and only if the
  `.fem` defines at least one circuit property.
* The file is optional. If it is missing, the solve is an ordinary static
  one.
* **It is never deleted or renamed by the application.** A sidecar left
  beside a model is picked up by every later Analyze of that model,
  including an interactive one from the GUI. Remove it when a run is
  finished.

## 3. Sections

Keys are matched case-insensitively at the start of a line. Scalar sections
use the `[Key] = value` form. Matrix sections are followed by one row per
line, whitespace-separated numbers. Circuit names are quoted strings and
must match a circuit property name in the `.fem` exactly (case-sensitive).
Sections may appear in any order **except** that `[Circuits]` must precede
`[CircuitMatrix]` and `[CircuitFlux]`, and `[Constraints]` must precede
`[ConstraintMatrix]` and `[ConstraintCurrent]`, because the count is needed
to size the rows that follow.

### 3.1 `[DeltaT] = h`

Time step in seconds. Any positive value marks the solve as one
**backward-Euler step** of a transient:

* Every region with nonzero conductivity gets the `sigma/h` mass term, with
  the previous solution's nodal `A` taken from the `.ans` named by
  `[PrevSoln]` in the `.fem`. That file must come from the same geometry;
  the solver verifies the geometry echo and otherwise remeshes and treats
  the previous solution as absent.
* Every circuit gains a row for the **axial voltage gradient** of any solid
  conductor in it. A solid conductor is a block with conductivity > 0 that
  is in a circuit and is not a stranded (wound) region. If the circuit's
  current is prescribed, the voltage gradient is solved for so that the
  region carries that current with a non-uniform (eddy) distribution. If
  the circuit is a flux-type circuit (section 3.2) the region's voltage
  enters the circuit row instead, as the `h * sum v_solid` term of
  section 1, with `v_solid` the voltage drop along the full planar depth
  (volts), one term per solid block in the circuit.
* The previous solution's mesh is reused when it can be, so node
  correspondence between steps is structural.

`[DeltaT]` absent or zero means a static solve. A solid conductor in no
circuit at all, with `[DeltaT]` set, is a legitimate pure-diffusion
problem.

### 3.2 `[Circuits] = N` followed by N quoted names

Each named circuit becomes a **flux-type circuit**: its current is an
unknown and gets one row of the section 1 form. Consequences:

* The current given for it in the `.fem` is ignored, except as the initial
  guess for the iterative solver when a `[PrevSoln]` is present.
* The circuit is treated as **series** regardless of how it is declared in
  the `.fem`. Each block label in it becomes its own entry, and the label's
  `Turns` gives that block's ampere-turns per ampere of circuit current.
  The sign of `Turns` is the winding direction. A circuit declared
  *parallel* in the `.fem` is silently promoted to series when named here.
* A circuit may mix wound blocks and solid blocks. All of them feed the
  same row: wound blocks through their flux linkage, solid blocks through
  the voltage term (transient only).

Row order in the following sections is the order of the names here.

### 3.3 `[CircuitMatrix]`

`N` rows of `N` numbers, henries. The matrix is **assembled symmetrically:
only the upper triangle, diagonal included, is read**, and entry `[k][j]`
is mirrored into `[j][k]`. Supply a symmetric matrix; anything below the
diagonal is ignored. Diagonal entries are what a single external inductance
or an `h * R` term looks like; off-diagonal entries are mutual couplings
(end-turn leakage between phases, end-ring segments between bars). A
single circuit with no external coupling has a `1 x 1` matrix containing
`0`.

### 3.4 `[CircuitFlux]`

`N` lines, one number each, `Lambda_k` in webers, in the same turn-weighted
convention as the flux linkage the postprocessor reports. Always supply
this section when `[Circuits]` is present. A static "find the current that
produces this flux linkage" solve is just `[CircuitMatrix]` of zeros and
the target linkage here.

### 3.5 `[Constraints] = P`, `[ConstraintMatrix]`, `[ConstraintCurrent]`

`[ConstraintMatrix]` is `P` rows of `N` dimensionless numbers (row *m* lists
the coefficient of each flux-circuit current). `[ConstraintCurrent]` is `P`
lines of `I_m` in amperes; if the section is absent every constraint is
homogeneous, `sum C * i = 0`. The rows are KCL at network nodes you want to
keep explicit rather than eliminate: a Y connection's star point
(`i_a + i_b + i_c = 0`), the junction of cage bars with an end ring, or a
plain pin of one current to a value (`C = [1]`, `I = i_0`).

The unknown paired with each row is the node voltage, scaled internally
and **not written to the `.ans`**. A tiny negative diagonal is added to each
constraint row for the benefit of the incomplete factorisation; its effect
on the enforced constraint is below solver tolerance.

## 4. What comes back

* In the `.ans` circuit block, `<TotalAmps_re>` of a flux-type circuit is
  overwritten with the **solved current**. Everything downstream (the
  Circuit Properties dialog, `mo_getcircuitproperties`) therefore sees it.
* `mo_getcircuitproperties(name)` returns `(current, voltage drop, flux
  linkage)`:
  * `current` is the solved `i_k`.
  * `flux linkage` is `lambda_k` computed from the field. It satisfies the
    circuit row to solver precision, which is the first thing a driver
    should check.
  * `voltage drop` is the winding's own resistive drop, `rho * J`
    integrated over the stranded blocks times turns and depth. It is
    **not** the network terminal voltage; the driver knows that from its
    own equations.
* For a solid block in a transient, the block label's record in the
  `.ans` carries its solved axial voltage gradient (as a "voltage-driven
  region", the same form the AC solver uses), and the postprocessor
  reconstructs the eddy current density from `A`, `A_prev` and `h`.
* KCL multipliers (node voltages) are not reported. If you need them,
  recover them from your own network equations once the currents are
  known.
* `[DeltaT]` is echoed into the `.ans` header so the postprocessor can
  reconstruct `J = sigma * (v - (A - A_prev)/h)`.

## 5. Writing the rows for an external network

Take the winding's terminal relation, with `R_w` the winding resistance
(the solver does **not** include it: a stranded region imposes no
resistance on the field equation, so it belongs in the network):

    v_terminal  =  R_w * i  +  d(lambda)/dt

Any linear network attached to the terminals, once its R, L, C and sources
are discretised in time, becomes a resistive network with history sources.
Reduce it to the winding ports (eliminate the internal nodes) to get an
affine multiport relation `v_terminal = e - Z * i`, with `Z` symmetric for a
reciprocal network. Then the rows are, for a step of length `h`:

| rule | `M` | `Lambda` |
|---|---|---|
| backward Euler | `h * (Z + R_w)` | `lambda_prev + h * e_n` |
| trapezoidal (Crank-Nicolson) | `(h/2) * (Z + R_w)` | `lambda_prev + (h/2) * (e_n + e_prev) - (h/2) * (Z + R_w) * i_prev` |

`lambda_prev` and `i_prev` are read from the previous step's `.ans` with
`mo_getcircuitproperties`; `e_prev` is the driver's own. Network states that
are not winding currents (capacitor voltages, currents in unmapped
inductors) are advanced by the driver after each step from the returned
currents, and feed the next step's `e`. Choosing the integrator is choosing
the table row; the solver is indifferent.

Worked single-phase RL example, source `e(t)`, external resistance `R`,
external inductance `L`, winding resistance `R_w`, backward Euler:

    e = (R + R_w) i + L di/dt + d(lambda)/dt
    => lambda_n + [h (R + R_w) + L] i_n = lambda_prev + L i_prev + h e_n

    [Circuits] = 1
    "phaseA"
    [CircuitMatrix]
    <h*(R+R_w) + L>
    [CircuitFlux]
    <lambda_prev + L*i_prev + h*e_n>

Three-phase Y, `R` the total series resistance per phase including the
winding, end-turn leakage `L_e` per phase with an illustrative mutual
`-L_e/2`, backward Euler, star point kept explicit:

    [Circuits] = 3
    "A"
    "B"
    "C"
    [CircuitMatrix]
    <h R + L_e>   <-L_e/2>      <-L_e/2>
    0             <h R + L_e>   <-L_e/2>
    0             0             <h R + L_e>
    [CircuitFlux]
    <Lambda_A>
    <Lambda_B>
    <Lambda_C>
    [Constraints] = 1
    [ConstraintMatrix]
    1 1 1

(the lower triangle is shown as `0` to make the point that it is not read;
writing the mirrored values is equally fine). The Y connection's line-to-
star voltages enter through `Lambda`, the star-point voltage is the
constraint multiplier, and the homogeneous `[ConstraintCurrent]` is omitted.

Squirrel cage, `n` bars each modelled as a **one-turn stranded** circuit:
each bar is a flux-type circuit whose current is a bar current. The end
rings are a resistive-inductive ladder whose segment currents are
cumulative sums of the bar currents around the ring; eliminating them
turns the ring impedances into a full symmetric `M` over the bar currents
(the KCL rows cannot carry ring-segment currents, since `C` ranges over
flux-circuit currents only). One homogeneous KCL row `sum i_b = 0`
expresses charge conservation in the rings. Modelling the bars as **solid**
blocks in a flux-type circuit instead puts each bar's eddy distribution
into the field equation (section 3.1) and its total voltage into the row
through the `h * v_solid` term, with the same `M` and KCL rows. The two
can be mixed bar by bar.

A pulse-width-modulated inverter is not nonlinear from the field's point
of view: the switch states are a known schedule, so `Z` and `e` are
piecewise constant in time and recomputed each step, still one solve.
State-dependent elements (diode conduction) are the one case that needs
either a one-step lag of the state or a single corrector pass using the
previous step's incremental inductance; neither puts the field solve
inside an iteration.

### Worked case: inverter-fed motor with normal-mode and common-mode filters

Three winding ports `A`, `B`, `C`, Y-connected. Between the inverter and
the motor sit a normal-mode inductor `L_dm` with damping resistor `R_dm`
per phase, a common-mode choke, and common-mode capacitors `C_cm` from
each phase to ground. The star point reaches ground through the winding-
to-frame capacitance `C_f`. Every element lands in one place:

| element | where it goes |
|---|---|
| inverter legs | the switch schedule gives each leg's voltage to the DC-bus midpoint, `u_A(t)`, `u_B(t)`, `u_C(t)`, known per step: part of `e` |
| `L_dm`, `R_dm` | in series with the winding, so they carry the winding current: `h R_dm + L_dm` on the diagonal of `M` |
| common-mode choke | three windings on one core, inductance matrix `L_cm * [all ones] + L_leak * I`: `L_cm` on every off-diagonal of `M`, `L_cm + L_leak` added to the diagonal |
| `C_cm` to ground, `C_f` from star point to ground | internal nodes with states: companion-discretised, eliminated into `Z` and `e` before the step, advanced from the returned currents after it |
| star point | an internal node like the others once `C_f` is present; a KCL row `i_A + i_B + i_C = 0` instead if it floats |
| winding resistance `R_w` | `h R_w` on the diagonal of `M`; the field does not impose it |

Backward Euler, with the shunt capacitors eliminated, gives a full
symmetric `3 x 3` resistive matrix `Z` (the series resistances
`R_dm + R_w` on the diagonal, plus the companion conductances of the
shunt paths, which couple the phases to each other through ground), with
the inductors kept separate as the external inductance matrix `L`, and
`e` carries the leg voltages plus the capacitor history terms. The rows
are then

    [Circuits] = 3
    "A"
    "B"
    "C"
    [CircuitMatrix]
    <h Z_AA + L_AA>   <h Z_AB + L_cm>   <h Z_AC + L_cm>
    0                 <h Z_BB + L_BB>   <h Z_BC + L_cm>
    0                 0                 <h Z_CC + L_CC>
    [CircuitFlux]
    <lambda_A,prev + sum_j L_Aj i_j,prev + h e_A>
    <lambda_B,prev + sum_j L_Bj i_j,prev + h e_B>
    <lambda_C,prev + sum_j L_Cj i_j,prev + h e_C>

with `L_AA = L_dm + L_cm + L_leak` and the `L_Aj` the full external
inductance matrix (the `L i_prev` term is the history of every external
inductor, exactly as in the single-phase example). After the solve the
driver updates each capacitor voltage from the currents that flowed into
it, and those voltages are the next step's history terms. With `C_f`
present there is no KCL row: the star-point voltage is a network state
like the capacitor voltages, recovered by back-substitution.

Three things are specific to this case:

* **Common mode needs a return path.** With a floating star point and no
  capacitance to ground the KCL row forces `i_A + i_B + i_C = 0`, no
  common-mode current can exist, and the common-mode filter does nothing.
  The capacitive path to the frame is what makes the common-mode problem
  a problem, and it is a network state, not a field quantity.
* **The step is set by the switching edges, not by the machine.** A
  10 kHz carrier resolved at a microsecond or so turns a few electrical
  cycles into hundreds of thousands of steps. This is the workload for
  which an in-process driver, rather than a solver spawn per step, is
  essential.
* **Dead time is the one nonlinearity.** While both switches in a leg are
  off, the leg voltage depends on the sign of that phase's current through
  the freewheeling diodes. Taking the sign from the previous step is the
  standard one-step lag, accurate because dead time is short compared with
  the steps across it. Ideal switches with no dead time have no
  nonlinearity at all.

The field supplies the zero-sequence response for free: common-mode
current in all three phases produces whatever net MMF the winding layout
gives it, which a lumped model would have to guess.

## 6. Hazards and limits

* **`h` lives in two places**: `[DeltaT]` and baked into `M` and `Lambda`.
  The solver cannot check that they agree, and a mismatch is a subtly
  wrong transient, not an error. Derive both from one variable.
* **A stale sidecar is silently applied** to the next Analyze of that
  model (section 2).
* **Planar only** for the circuit sections; DC only for the whole file.
* **Names must match exactly** (case-sensitive). An unmatched name is a
  fatal input error: the solver prints which entry failed to match and
  exits with status 7 ("Problem loading .fem file"), the same code as any
  other unreadable input. Before this guard existed the slot was left at
  -1 and the right-hand-side assembly indexed the circuit list with it,
  undefined behaviour with no diagnostic.
* **`[CircuitMatrix]` must be symmetric**; the lower triangle is ignored.
* **Solid-conductor eddy currents are backward Euler** regardless of what
  rule the rows use. Mixing trapezoidal rows with backward-Euler solid
  regions is legitimate (each equation carries its own rule, both are
  unconditionally stable, the coupled scheme is consistent); the solid
  regions' own dynamics are then first order while the stranded circuits
  are second order. A solver-side trapezoidal option for the field term is
  not implemented.
* With any flux or current constraint present, the linear system is
  indefinite and is solved by QMR with a signed incomplete-LDL
  preconditioner, the only path in the static solver that takes that
  route. It is exercised by `testing/FluxConstraintTest.py`.
* The sidecar circuit rows are not available to the axisymmetric solver,
  and nothing in the GUI edits the file.

## 7. Minimal verified example

`testing/myTestProblem.tmp`, used by `testing/FluxConstraintTest.py` on
`testing/myTestProblem.fem`:

    [Circuits] = 1
    "icoil"
    [CircuitMatrix]
    0.001
    [CircuitFlux]
    0.2

The solver returns `i = 20.5799 A` and `lambda = 0.17942 Wb`, so
`0.17942 + 0.001 * 20.5799 = 0.2` as required. FEMM 4.2 on the same model
agrees to the mesh difference (about 0.01 percent). `testing/TransientTest.py`
exercises `[DeltaT]` against a closed-form 1-D diffusion, and its last
check is the `h * v_solid` term of a solid block in a flux-type circuit:
with `M = 0` and no wound block the row collapses to `h * Depth * v = Lambda`.

## 8. Where in the code

| what | where |
|---|---|
| parser for every section, `[DeltaT]` gate, planar-only gate | `solvers/fkn/femmedoccore.cpp`, the block after the `.fem` is closed |
| series expansion of flux-type circuits, `Turns` per block | `femmedoccore.cpp`, series-circuit expansion |
| flux row coupling from the element sweep, `[CircuitMatrix]` and constraint assembly, right-hand sides | `solvers/fkn/prob1big.cpp`, after the element loop |
| solid-conductor voltage rows and their mirror into the flux row | `prob1big.cpp`, "Close the total-current equation of each Case-2 region" |
| `.ans` output of the solved current and the solid-block voltage | `prob1big.cpp`, the `.ans` rewrite of `<TotalAmps_re>` and the per-label block |
| postprocessor's current, voltage drop, flux linkage | `libfemm/results_doc.cpp`, `GetStrandedVoltageDrop` / `GetFluxLinkage`; `libfemm/py_mag_postproc.cpp`, `mo_getcircuitproperties` |
