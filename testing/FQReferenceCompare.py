# FQReferenceCompare.py -- femm-qt self-consistency acceptance test.
#
# The OF_* goldens answer "does femm-qt agree with Original FEMM?" -- but their
# 2-3% tolerances exist to absorb the tangle-vs-Triangle MESH difference, so
# they cannot see a small solver regression. This test answers the sharper
# question: "did a change move femm-qt's OWN answers?" It compares a fresh
# suite run (the fq_*.txt that _verify_all.sh leaves in testing/) against the
# committed reference snapshot in fq_reference/ -- same mesher, same mesh,
# same postprocessing, so the expected agreement is set by the solvers alone:
#
#   * the nonlinear outer iteration stops at res < 100*Precision = 1e-6, with
#     the final pass's inner solve enforced at full Precision, so two correct
#     solver versions land in a fixed-point basin ~1e-6 relative wide;
#   * the solvers are sequential and bit-deterministic, so they add no
#     run-to-run floor of their own (OpenMP's ~5e-7 reduction-order noise
#     retired with it);
#   * saturation (dlnH/dlnB ~ mu/mu_inc) amplifies that on nonlinear-magnetics
#     quantities.
#
# NORMALIZATION: each movement is judged against its LABEL GROUP's largest
# magnitude -- all lines sharing the text left of '=' (the same quantity, in
# the same units, across fixtures/points). This keeps zero-valued quantities
# (symmetry force components, lossless powers, transverse fields) from
# producing meaningless self-relative percentages: they are measured against
# the scale of their real counterparts.
#
# BUDGETS (fraction of label-group scale):
#     nonlinear magnetics suites .... 1e-5
#     everything else ............... 5e-6
# Calibrated 2026-08-04 against three known solver states (pre-overhaul,
# post-overhaul, post-enforcement): all agree within ~1e-6 of scale, so these
# budgets carry ~10x headroom over the observed floor while sitting 200-2000x
# below the OF-comparison tolerances.
#
# THE BUDGETS ASSUME THE SAME FLOATING-POINT ARITHMETIC AS THE REFERENCE, and
# the reason is NOT the solvers -- it is THE MESH. Everything above assumes
# "same mesher, same mesh"; a build that contracts a*b+c into a fused
# multiply-add does not produce the same mesh, so none of it applies.
#
# Measured 2026-08-19, same model, two builds differing only in FMA:
#
#     RadialMagneticBearingAC   16277 nodes  vs  16357 nodes
#     my_SPM_motor_AGB         156238 nodes  vs 156277 nodes
#     ~64% of vertices exist in only one of the two meshes
#
# The same build run twice is bit-identical, so this is not nondeterminism.
# tangle's PREDICATES are immune to contraction (filter plus exact float256
# fallback, so a sign decision cannot change) -- but meshing is not only
# decisions. Circumcentres, off-centre Steiner placement and the quality
# metrics compared against thresholds are ordinary arithmetic with no exact
# fallback, and one ULP decides which triangle is worst. Refinement then
# inserts a point somewhere marginally different and the cascade diverges.
# "The predicates are safe" is true and does NOT imply "the mesh is stable".
#
# So a contracting build solves a DIFFERENT DISCRETE PROBLEM, and ~1e-3 in
# derived quantities is ordinary mesh-to-mesh discretisation scatter -- the
# OF suites carry 2-3% tolerances for exactly this reason, and they stay green
# throughout. It is this test's premise that is violated, not the physics.
#
# Which builds contract: clang on arm64 by default, and gcc wherever FMA
# exists, which -march=native turns on for any Haswell-or-later x86. Measured
# 14 of 18 suites over budget, worst ~3e-3, with several movements
# bit-identical between an arm64 mac and an x86 build compiled -mfma
# -ffp-contract=fast -- which is what identified the cause. Reproduce with:
#
#     cmake .. -DCMAKE_CXX_FLAGS="-mfma -ffp-contract=fast" ...
#     FEMM_BUILD=../build-fma bash _verify_all.sh
#
# DECIDED 2026-08-19: FMA STAYS ON. Turning it off on macOS was considered and
# rejected -- the contracting build is not WRONG, it is a different mesh solved
# correctly, and the measured price was 10-14% on AC harmonic solves (complex
# multiply is exactly the shape FMA fuses). Transients and DC magnetostatics
# were unaffected either way, so the cost bought nothing for the workloads that
# matter.
#
# The standing consequence: THIS TEST IS EXPECTED TO FAIL ON macOS, and that
# row should be read as "not applicable on a contracting build", not as red.
# It remains the sharpest regression detector on Windows, Linux and FreeBSD,
# which is where it is relied on.
#
# If it is ever wanted back on macOS, the way is a SECOND reference snapshot
# generated there -- the test asks "did a change move our own answers", which
# is a per-platform question and works fine against a per-platform baseline.
# That costs another snapshot to regenerate on every intentional solver change,
# which is why it was not done.
#
# Regenerating the SHARED reference on a contracting build would make this test
# green and useless everywhere else; do not.
#
# HONEST DETECTION FLOOR: this test cannot see defects whose physical impact
# is below ~1e-5 of quantity scale (e.g. the final-pass tolerance-enforcement
# gap fixed in b8d9c76 moved results by only ~1e-7 of scale; it was found by
# reasoning about invariants, not by output comparison). Green here means "no
# physically visible movement", not "no defect of any kind".
#
# Usage:
#     python3 FQReferenceCompare.py [run_dir]     (run_dir defaults to ".")
# Exits nonzero on any violation.
#
# REGENERATING THE REFERENCE: only after an INTENTIONAL solver-behavior
# change, with the OF suites green and the movement understood. Run
# _verify_all.sh, copy the fresh fq_*.txt over fq_reference/, and commit both
# together with an explanation -- exactly like regenerating a golden.

import os, re, sys

NUM = re.compile(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?")

# Suites whose fixtures involve nonlinear (B-H) magnetics.
NONLINEAR_MAG = {
    "fq_result.txt", "fq_pv_result.txt", "fq_gap_result.txt",
    "fq_circ_result.txt", "fq_line_result.txt", "fq_bh_result.txt",
}
BUDGET_NONLINEAR = 1e-5
BUDGET_LINEAR    = 5e-6

def values(line):
    return [float(x) for x in NUM.findall(line)]

def label(line):
    return line.split("=", 1)[0].strip() if "=" in line else None

def main():
    run_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    ref_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fq_reference")

    violations = []
    compared = 0
    worst_overall = 0.0

    for fn in sorted(os.listdir(ref_dir)):
        if not fn.startswith("fq_"):
            continue
        run_path = os.path.join(run_dir, fn)
        if not os.path.exists(run_path):
            violations.append((fn, None, "missing from run directory"))
            continue
        R = open(os.path.join(ref_dir, fn), errors="replace").read().splitlines()
        T = open(run_path, errors="replace").read().splitlines()
        if len(R) != len(T):
            violations.append((fn, None,
                f"line count {len(T)} vs reference {len(R)} -- test scripts "
                "changed? regenerate the reference"))
            continue
        compared += 1
        budget = BUDGET_NONLINEAR if fn in NONLINEAR_MAG else BUDGET_LINEAR

        gscale = {}
        for ln in R:
            lab = label(ln)
            if lab is None:
                continue
            v = values(ln)
            if v:
                gscale[lab] = max(gscale.get(lab, 0.0), max(abs(x) for x in v))

        worst = 0.0
        winfo = ""
        for lr, lt in zip(R, T):
            lab = label(lr)
            if lab is None:
                continue
            vr, vt = values(lr), values(lt)
            if len(vr) != len(vt) or not vr:
                continue
            sc = max(gscale.get(lab, 0.0), 1e-300)
            for xr, xt in zip(vr, vt):
                d = abs(xr - xt) / sc
                if d > worst:
                    worst, winfo = d, lr.strip()[:70]
        worst_overall = max(worst_overall, worst)
        status = "ok" if worst <= budget else "VIOLATION"
        print(f"  {fn:<24} worst {worst:9.3g}  budget {budget:.0e}  {status}")
        if worst > budget:
            violations.append((fn, worst, winfo))

    print()
    if violations:
        print(f"REFERENCE CHECK FAILED: {len(violations)} suite(s) exceed the budget")
        for fn, w, info in violations:
            print(f"  {fn}: {w if w is None else format(w, '.3g')}  [{info}]")
        sys.exit(1)
    print(f"reference check: {compared} suites within budget "
          f"(worst movement {worst_overall:.3g} of label scale)")

if __name__ == "__main__":
    main()
