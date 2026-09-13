# Why the ILDL preconditioner breaks down, and a structural cure

Status: **investigation complete, proposal NOT implemented.** Nothing here
fixes an outstanding defect. The shift ladder in `common/spars.cpp` already
handles every case found so far, and ends in a guarantee. This note exists so
the reasoning is not re-derived, and so the successor idea has its measurements
attached.

Written 2026-09-02, after the second batch of "Couldn't solve the problem"
reports from a motor designer. See `project_ildl_breakdown` for the fix that
shipped.

---

## 1. What was observed

Four independent saturated PM-motor models, all solving in FEMM 4.2, all
failing in femm-qt with `Couldn't solve the problem`. Three of the four have
**no previous solution and no air gap element** — ordinary DC magnetostatics.
The failure is the incomplete LDL^T factorization: a pivot goes nonpositive,
and (before the fix) the in-place patch that followed compounded into overflow.

The obvious reading — "saturation is hard for the solver" — is not quite
right, and the useful version is sharper.

## 2. What the matrices actually look like

`FactorPC` was instrumented to report, per Newton pass, the worst row's
dominance ratio `sum_j |a_ij| / |a_ii|` and the share of off-diagonal entries
that are **positive**:

| model | pass | dominance ratio | positive off-diagonals |
|---|---|---:|---:|
| `PlanarLorentzForce` (linear, 86,613 nodes) | 0 | **1.0000** | **0.0% count, 0.0% mass** |
| `block` (mildly nonlinear) | 0 | 1.0000 | 0.0% |
| `block` | 2 | 1.2638 | 5.7% count, 1.0% mass |
| `MotorB_FSCW1` (saturated, no AGE) | 0 | 3.78 | 4.1% count, 1.4% mass |
| `MotorB_FSCW1` | 2 | **5.20** | **10.1% count, 1.9% mass** |
| `MotorB_VIPM` (saturated, has AGE) | 1 | 5.14 | 7.3% count |

## 3. Why linear problems never broke

A linear magnetostatic assembly is `integral of nu * grad Ni . grad Nj`. On a
Delaunay mesh that is an **M-matrix**: positive diagonal, all off-diagonals
<= 0, weakly diagonally dominant. `PlanarLorentzForce` measures this exactly —
ratio 1.0000, 0.0% positive, over 86k nodes.

For an M-matrix, **incomplete Cholesky provably cannot break down** (Meijerink
& van der Vorst 1977). That is a theorem, not luck, and it is why years of
linear use — and the whole regression corpus telemetry that justified removing
the SSOR fallback — never saw a single rescued pivot.

## 4. Why the Newton case breaks it

The Newton tangent adds, per element,

    2 * (dnu/dB^2) * (B . grad Ni)(B . grad Nj)   =   c * b b^T,  c >= 0

a rank-one, positive-semidefinite update. The matrix stays SPD — that is not
the problem. The problem is that `b_i b_j` is **positive** wherever the two
gradients lie on the same side of **B**, which destroys the M-matrix sign
structure. And `dnu/dB^2` is exactly the quantity saturation inflates: past the
knee, reluctivity climbs steeply, so the term that breaks the sign structure is
scaled by how saturated the iron is.

    saturation -> large dnu/dB^2 -> positive off-diagonals -> not an M-matrix
                -> Kershaw's case -> dropped fill can make a pivot nonpositive

`block` shows the transition cleanly: a perfect M-matrix at pass 0, 5.7%
positive by pass 2, and it still solves because it is only mildly nonlinear.
The motors run 10-11% positive at 4-5x dominance, and they did not.

**So "normal problems" is the correct description.** Nothing exotic is
required — just iron driven past the knee, which is the entire point of a
motor. Every saturated design was a candidate. What made it look rare is that
the failure needs the dropping rule to discard the particular entries that flip
a pivot, so it depends on mesh, materials and Newton path together; which is
why one rotor angle failed and its neighbour did not.

FEMM 4.2 is immune because SSOR factors nothing and so has no pivots to lose.
It is a weaker preconditioner — replacing it bought 2-3x — and the shift ladder
is what buys back the robustness without giving up the speed.

## 5. The proposal: compensate rather than discard

**The entries that cause the breakdown carry almost none of the operator: 10%
of the count, 2% of the mass.** That asymmetry is what makes a structural cure
cheap, and it is the whole reason this idea is worth keeping.

Rather than choosing what to KEEP, change what happens to what is DROPPED. For
every positive off-diagonal (and every threshold-dropped entry), move it onto
both diagonals instead of discarding it:

    d_i += a_ij ;  d_j += a_ij ;  a_ij := 0

The resulting `A-hat` has non-positive off-diagonals and a positive diagonal.
If its row sums stay >= 0 it is a **Stieltjes matrix**, incomplete Cholesky on
it is breakdown-free by the same 1977 theorem, and any FURTHER dropping is also
safe because the H-matrix property survives arbitrary dropping.

This is essentially **MILU** (modified ILU) with an M-matrix-restoring twist.

### 5.1 Why the row-sum condition plausibly holds

The Newton term's row sum is

    sum_j b_i b_j  =  b_i * (B . sum_j grad Nj)  =  0

because shape-function gradients sum to zero on every element. **The Newton
term contributes exactly zero row sum** — it redistributes, never adds. So the
row sums of `A` are the row sums of the LINEAR part, which are >= 0 for the FE
Laplacian with Dirichlet conditions.

That is a sketch, not a proof, and section 7 is how to settle it.

### 5.2 What it would buy

The guarantee becomes **proactive instead of reactive**: one factorization,
breakdown impossible by construction, and the shift ladder becomes dead code.
It is also better targeted — only rows that actually lost mass are perturbed,
where `alpha*diag(M)` perturbs every row in the matrix.

### 5.3 What it might cost

MILU's reputation is genuinely mixed: often excellent on Poisson-like problems,
sometimes **worse** than plain ILU on strongly anisotropic ones. A saturated
Newton matrix is strongly anisotropic almost by definition — the rank-one term
is huge along **B** and zero across it — so this could cost iterations exactly
where they are most needed. That is the risk, and it is not resolvable by
argument.

## 6. Alternatives considered

**Precondition with the Picard matrix.** Factor `nu * K0` (the linear part at
the current nu) and use it for the Newton system: guaranteed M-matrix, no
compensation needed. **The measurements undercut this.** `MotorB_FSCW1`
already reads 3.78 / 4.1% positive at **pass 0**, before the Newton term
contributes anything — so something else is placing positive off-diagonals
there (PM coercivity, or non-Delaunay constrained edges from the mesher). The
linear part may not be an M-matrix on these meshes, and that must be checked
before the idea is worth anything.

**Keep more fill.** Does not help. Dropping is not the root cause; the matrix
itself is not an H-matrix in saturation, and no choice of what to keep can
restore a property the operator does not have.

**Leave it alone.** The shift ladder works, costs nothing measurable, took the
SMALLEST rung (alpha = 1e-3) on all four real models, and terminates in a
theorem. This is the status quo and it is defensible.

## 7. The decisive experiment, if this is ever picked up

Cheap, and it settles the whole question before any code is written. Over the
regression corpus plus the four motor models, instrument the assembled matrix
and report:

1. **Are row sums >= 0 after assembly?** Specifically including the AGE dense
   block, periodic/antiperiodic merges, and circuit-constraint rows — all
   assembled outside the argument in 5.1. If any of those produce negative row
   sums, the Stieltjes guarantee fails and the idea is dead.
2. **What fraction of rows would need compensation**, and how much diagonal
   mass would move. Section 2 says ~2% of off-diagonal mass; confirm per model.
3. Then, if 1 holds: implement behind a flag, and compare **Krylov iteration
   counts** against the current factor across the corpus. That is the number
   that decides it, not the breakdown guarantee, because the guarantee is
   already in hand.

If step 1 fails the answer is no, and an hour has been spent.

## 8. How to reproduce the measurements

The instrumentation was temporary and is not in the tree. It was a block at the
top of `CBigLinProb::FactorPC` computing, per call, `max_i (sum_j |a_ij| /
|a_ii|)` over the flat CSR, plus counts and absolute-value sums of positive vs
all off-diagonal entries, gated on an environment variable. Roughly fifteen
lines; re-adding it takes minutes, which is why it was not kept.

Related: `project_ildl_breakdown` (the shipped fix), `project_solver_
preconditioner` (why ILDL replaced SSOR, and the measurements behind it),
`tests/test_spars_pc.cpp` and `solvers/tests/test_cspars_pc.cpp`.
