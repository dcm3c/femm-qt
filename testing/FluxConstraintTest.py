# FluxConstraintTest.py -- flux- and current-constrained circuit invariant
# test (CPython).
#
# Exercises the `<basename>.fem.tmp` sidecar mechanism on a planar DC
# magnetics problem whose circuit "icoil" carries constraints:
#
#   flux constraint      fluxlinkage + M * i = Lambda
#       ([CircuitMatrix] M, [CircuitFlux] Lambda -- the interface the
#       transient drivers use; M carries the external end-turn/end-ring
#       coupling, a full matrix in general)
#
#   current constraint   sum_j C[m][j] * i_j = I_m
#       ([Constraints]/[ConstraintMatrix]/[ConstraintCurrent] -- KCL rows
#       for cage bars, Y-winding star points, general interconnections.
#       The state paired with each row is a Lagrange multiplier that is
#       physically the voltage of the node where the KCL holds; an absent
#       [ConstraintCurrent] section means the homogeneous constraint I=0)
#
# This is the ONLY path that routes the static solver into QMRSolve
# (prob1big.cpp: NumFluxConstraints or NumCurrentConstraints > 0), so it
# also exercises the signed-D indefinite branch of the incomplete-LDL^T
# preconditioner -- the current-constraint rows have zero diagonals, the
# genuinely saddle-point case.
#
# Checks:
#   1. Flux-constraint invariant, exact to solver precision (~1e-9
#      observed), at 1e-6 relative on every flux case.
#   2. An Original-FEMM cross-check on one flux case, at 1% (bounded by
#      the tangle-vs-Triangle mesh difference; observed 0.012%).
#   3. Current-constraint invariant: i pinned exactly to the prescribed
#      value, including the homogeneous (absent-RHS) form.
#   4. Consistency: constraining the current to the UNCONSTRAINED case's
#      own solution current must reproduce that solution (the multiplier
#      -- the node voltage -- goes to zero, the constraint is inactive at
#      the operating point).
#
# HISTORY: the OF cross-check is what gives this test teeth, and it earned
# them immediately. The first version of this test measured the implemented
# behavior as lambda + 2*M*i = Lambda and codified it -- but OF solves the
# same problem to lambda + M*i = Lambda (i = 20.5799, flux = 0.17942 for
# M = 0.001, Lambda = 0.2). The factor of 2 was a femm-qt split-assembly
# regression (the [CircuitMatrix] block assembled unguarded in the sweep
# loop, doubling into the Iter==1 baseline); an invariant checked only
# against the implementation can enshrine a bug, so the reference values
# below are the actual oracle. The current-constraint machinery has no OF
# oracle -- OF never implemented it (parsed but unassembled) -- so its
# oracle is the exact-pin and consistency checks instead.
#
# Usage:  python3 FluxConstraintTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes fqflux_tmp.* scratch files.
# Exits nonzero on any violation.

import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    HERE, "..", "build", "bin", "femm_headless.exe")

FIXTURE = os.path.join(HERE, "myTestProblem.fem")
BASE = os.path.join(HERE, "fqflux_tmp")
CIRCUIT = "icoil"

# flux-only cases: no coupling (exactness), then M and Lambda variations
CASES = [(0.0, 0.1), (0.001, 0.2), (0.002, 0.2), (0.001, 0.1)]
RELTOL = 1e-6

# Original FEMM 4.2 on the M = 0.001, Lambda = 0.2 case (user-run reference,
# 2026-08-05). Tolerance bounded by the mesh difference, not the solvers.
OF_CASE = (0.001, 0.2)
OF_I, OF_FLUX = 20.5799, 0.17942
OF_RELTOL = 1e-2

SCRIPT = (
    'open(r"%s.fem")\n'
    "mi_analyze()\n"
    "mi_loadsolution()\n"
    'i, v, f = mo_getcircuitproperties("%s")\n'
    'print("RESULT", i, f)\n'
    "mo_close()\n"
    "mi_close()\n" % (BASE.replace("\\", "/"), CIRCUIT)
)

def write_tmp(M, Lam, C=None, I=None):
    with open(BASE + ".tmp", "w") as f:
        f.write('[Circuits] = 1\n"%s"\n[CircuitMatrix]\n%.17g\n'
                "[CircuitFlux]\n%.17g\n" % (CIRCUIT, M, Lam))
        if C is not None:
            f.write("[Constraints] = 1\n[ConstraintMatrix]\n%.17g\n" % C)
            if I is not None:
                f.write("[ConstraintCurrent]\n%.17g\n" % I)

def solve():
    out = subprocess.run([BIN, "-run", BASE + ".py"],
                         capture_output=True, text=True).stdout
    for ln in out.splitlines():
        if ln.startswith("RESULT"):
            v = [float(x) for x in ln.split()[1:]]
            return v[0], v[1]
    return None

def main():
    with open(FIXTURE) as f:
        fem = f.read()
    with open(BASE + ".fem", "w") as f:
        f.write(fem)
    with open(BASE + ".py", "w") as f:
        f.write(SCRIPT)

    failures = 0
    unconstrained = {}   # (M, Lam) -> (i, flux), for the consistency check
    try:
        for M, Lam in CASES:
            write_tmp(M, Lam)
            r = solve()
            if r is None:
                print("  M=%-8g Lam=%-6g NO RESULT (solver or script failed)"
                      % (M, Lam))
                failures += 1
                continue
            i, flux = r
            unconstrained[(M, Lam)] = (i, flux)
            resid = abs(flux + M * i - Lam) / abs(Lam)
            ok = resid <= RELTOL
            print("  M=%-8g Lam=%-6g i=%-12.6g flux=%-12.6g  "
                  "|flux+Mi-Lam|/Lam = %.3g  %s"
                  % (M, Lam, i, flux, resid, "ok" if ok else "VIOLATION"))
            if not ok:
                failures += 1
            if (M, Lam) == OF_CASE:
                di = abs(i - OF_I) / abs(OF_I)
                df = abs(flux - OF_FLUX) / abs(OF_FLUX)
                ok = di <= OF_RELTOL and df <= OF_RELTOL
                print("  %-17s vs OF: di=%.3g dflux=%.3g  %s"
                      % ("", di, df, "ok" if ok else "VIOLATION"))
                if not ok:
                    failures += 1

        # --- current-constraint cases ---
        M, Lam = OF_CASE
        iA, fluxA = unconstrained.get(OF_CASE, (None, None))

        # exact pin: i forced to a value the flux constraint alone would
        # not have chosen
        write_tmp(M, Lam, C=1.0, I=10.0)
        r = solve()
        if r is None:
            print("  constraint i=10           NO RESULT")
            failures += 1
        else:
            i, flux = r
            resid = abs(i - 10.0) / 10.0
            ok = resid <= RELTOL
            print("  constraint i=10           i=%-12.6g flux=%-12.6g  "
                  "|i-10|/10 = %.3g  %s"
                  % (i, flux, resid, "ok" if ok else "VIOLATION"))
            if not ok:
                failures += 1

        # homogeneous form: [ConstraintCurrent] absent means I = 0
        write_tmp(M, Lam, C=1.0, I=None)
        r = solve()
        if r is None:
            print("  constraint i=0 (no RHS)   NO RESULT")
            failures += 1
        else:
            i, flux = r
            scale = abs(iA) if iA else 1.0
            resid = abs(i) / scale
            ok = resid <= RELTOL
            print("  constraint i=0 (no RHS)   i=%-12.6g flux=%-12.6g  "
                  "|i|/iA = %.3g  %s"
                  % (i, flux, resid, "ok" if ok else "VIOLATION"))
            if not ok:
                failures += 1

        # consistency: pin i to the unconstrained solution's own current;
        # the multiplier (node voltage) must go to zero and the solution
        # must be reproduced
        if iA is not None:
            write_tmp(M, Lam, C=1.0, I=iA)
            r = solve()
            if r is None:
                print("  constraint i=i_uncon      NO RESULT")
                failures += 1
            else:
                i, flux = r
                df = abs(flux - fluxA) / abs(fluxA)
                ok = abs(i - iA) / abs(iA) <= RELTOL and df <= 1e-5
                print("  constraint i=i_uncon      i=%-12.6g flux=%-12.6g  "
                      "dflux/flux = %.3g  %s"
                      % (i, flux, df, "ok" if ok else "VIOLATION"))
                if not ok:
                    failures += 1
    finally:
        for ext in (".fem", ".tmp", ".py", ".ans"):
            try:
                os.remove(BASE + ext)
            except OSError:
                pass

    if failures:
        print("FLUX CONSTRAINT CHECK FAILED: %d case(s)" % failures)
        sys.exit(1)
    print("flux-constraint check: %d flux + 3 current-constraint cases ok "
          "(lambda + M*i = Lambda, C*i = I; OF cross-check ok)"
          % len(CASES))

if __name__ == "__main__":
    main()
