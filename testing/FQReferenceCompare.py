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
