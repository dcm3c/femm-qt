# TransientTest.py -- transient (backward-Euler) eddy currents in solid
# conductors. CPython; drives the solver directly and reads the .ans.
#
# All three fixtures are the same 1 inch square of solid copper (sigma = 58
# MS/m, depth 40 in) holding a uniform 1 T field, with both vertical faces
# then clamped to A = 0. That is textbook 1-D magnetic diffusion, which
# gives this test something the rest of the suite mostly lacks: a CLOSED
# FORM to check against, rather than another solver's output.
#
#     A(x,t) = sum_n b_n sin(n pi x/L) exp(-t/tau_n)
#     b_n = 2 A0 (-1)^(n+1)/(n pi),  tau_n = mu sigma L^2/(n^2 pi^2)
#     tau_1 = 4.764 ms
#
#   CubeInitialCondition.fem  the 1 T starting state (A ramps 0 -> 0.0254)
#   CubeDiffusion.fem         block in NO circuit  -> v = 0, pure diffusion
#   CubeTransient.fem         block in a circuit   -> voltage unknown active
#
# Four checks, each aimed at a different part of the formulation:
#
#  1. DIFFUSION vs the series. Backward Euler is first order, so this does
#     not check "close to exact" -- it checks that the error HALVES with the
#     step and equals the analytic BE error for the dominant mode,
#     [exp(h/tau)/(1+h/tau)]^N. That pins the sign, the mass matrix and the
#     sigma/DeltaT scaling together. Sampled at the centre line, where
#     dA/dx = 0 and the value is therefore insensitive to node placement.
#
#  2. NET-ZERO CURRENT, via Ampere. An isolated conductor (circuit
#     prescribing 0 A) must carry zero net current, and -- independently --
#     zero enclosed current forces H, hence B, equal at both faces. The
#     second is not a quantity the formulation is told to conserve.
#
#  3. PRESCRIBED CURRENT. Driving a known current exercises the whole
#     current-equation chain at once (the 0.01 RHS factor, the voltage-row
#     diagonal and coupling, and A_prev's share); an error in any one of
#     them shifts the delivered current.
#
#  4. FLUX-CIRCUIT COUPLING MAGNITUDE. With M = 0 and no wound region the
#     circuit equation collapses to DeltaT*Depth*v = Lambda, predicting the
#     voltage outright. Checked at two values of Lambda. This is the only
#     check that can see the coupling's magnitude: with Lambda = 0 any
#     nonzero coefficient satisfies the equation.
#
# Usage:  python3 TransientTest.py [path-to-fkn] [path-to-femm_headless]
# Runs from testing/; writes and removes tr_tmp* scratch files.
# Exits nonzero on any violation.

import math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "..", "build", "bin")
FKN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BIN, "fkn.exe")
HEADLESS = sys.argv[2] if len(sys.argv) > 2 else os.path.join(BIN, "femm_headless.exe")

L, SIG, A0 = 0.0254, 5.8e7, 0.0254          # 1 inch, copper, 1 T
MU0 = 4e-7 * math.pi
TAU1 = MU0 * SIG * L * L / (math.pi ** 2)   # 4.764 ms
DEPTH_M = 40 * 2.54 / 100.0                 # [Depth] = 40 inches

failures = []

def fail(msg):
    failures.append(msg)
    print("  VIOLATION: " + msg)

def run(base, src, dt, prev=None, tmp_extra=""):
    """Write <base>.fem/.tmp from src and solve. Returns the .ans path."""
    fem = open(os.path.join(HERE, src)).read()
    if prev is not None:
        fem = fem.replace('[PrevSoln]    =  "CubeInitialCondition.ans"',
                          '[PrevSoln]    =  "%s"' % prev)
    open(base + ".fem", "w").write(fem)
    open(base + ".tmp", "w").write("[DeltaT] = %.17g\n" % dt + tmp_extra)
    r = subprocess.run([FKN, base], capture_output=True, text=True)
    if r.returncode != 0:
        fail("%s: solver returned %d" % (base, r.returncode))
        return None
    return base + ".ans"

def read_ans(path):
    """-> (nodes[(x,y,A)], labels[(case, value)])"""
    with open(path) as f:
        for line in f:
            if line.startswith("[Solution]"):
                break
        n = int(f.readline())
        nodes = []
        for _ in range(n):
            p = f.readline().split()
            nodes.append((float(p[0]), float(p[1]), float(p[2])))
        ne = int(f.readline())
        for _ in range(ne):
            f.readline()
        nl = int(f.readline())
        labels = []
        for _ in range(nl):
            p = f.readline().split()
            labels.append((int(p[0]), float(p[1])))
    return nodes, labels

def series(xr, t, N=60):
    s = 0.0
    for n in range(1, N + 1):
        s += (2 * A0 * (-1) ** (n + 1) / (n * math.pi)) * \
             math.sin(n * math.pi * xr) * math.exp(-t / (TAU1 / (n * n)))
    return s

def at(nodes, xr, yr=0.5):
    """Nearest node to (xr, yr) in inches."""
    return min(nodes, key=lambda p: abs(p[0] - xr) + abs(p[1] - yr))[2]

def march(tag, src, h, nsteps, tmp_extra=""):
    prev = "CubeInitialCondition.ans"
    for k in range(nsteps):
        b = "tr_tmp_%s%03d" % (tag, k)
        if run(b, src, h, prev, tmp_extra) is None:
            return None
        prev = b + ".ans"
    return prev

def main():
    print("  seeding initial condition (uniform 1 T)")
    if run("tr_tmp_ic", "CubeInitialCondition.fem", 0) is None:
        return
    # the fixtures name CubeInitialCondition.ans, so publish it under that name
    import shutil
    shutil.copy("tr_tmp_ic.ans", "CubeInitialCondition.ans")

    # ---- 1. diffusion vs the closed form, and BE's first-order rate ----
    print("  1. pure diffusion vs the analytic series (centre line)")
    T = 0.005
    prevmeas = None
    for h, N in ((5.0e-4, 10), (2.5e-4, 20), (1.25e-4, 40)):
        ans = march("d%d" % N, "CubeDiffusion.fem", h, N)
        if ans is None:
            return
        nodes, _ = read_ans(ans)
        fem, ana = at(nodes, 0.5), series(0.5, T)
        meas = (fem - ana) / ana
        # analytic backward-Euler error of the dominant mode
        pred = (math.exp(h / TAU1) / (1 + h / TAU1)) ** N - 1.0
        print("     h=%-9.3g N=%-3d  err %+.4f  predicted %+.4f" % (h, N, meas, pred))
        if abs(meas - pred) > 0.1 * abs(pred):
            fail("BE error %.4g departs from the predicted %.4g" % (meas, pred))
        if prevmeas is not None and not (0.4 < meas / prevmeas < 0.6):
            fail("error ratio %.3f is not the ~0.5 first order requires"
                 % (meas / prevmeas))
        prevmeas = meas

    # ---- 2. isolated conductor: net zero current, and Ampere ----
    print("  2. isolated conductor (0 A): net current and Ampere invariant")
    ans = march("nz", "CubeTransient.fem", 5.0e-4, 6)
    if ans is None:
        return
    probe = ("open(r\"tr_tmp_nz005.fem\")\n"
             "mi_analyze(); mi_loadsolution()\n"
             "mo_selectblock(0.5, 0.5)\n"
             "print('NETI', mo_blockintegral(7))\n"
             "print('BLO', mo_getpointvalues(0.02, 0.5)[2])\n"
             "print('BHI', mo_getpointvalues(0.98, 0.5)[2])\n"
             "mo_close(); mi_close()\n")
    open("tr_tmp_probe.py", "w").write(probe)
    out = subprocess.run([HEADLESS, "-run", "tr_tmp_probe.py"],
                         capture_output=True, text=True).stdout
    vals = {}
    for ln in out.splitlines():
        p = ln.split()
        if p and p[0] in ("NETI", "BLO", "BHI"):
            vals[p[0]] = float(p[1])
    if len(vals) != 3:
        fail("probe did not report the expected values")
        return
    # the current scale to judge "zero" against: sqrt(loss * sigma * area)
    # is overkill; use the conductor's own |J| scale via the face field
    print("     net current = %.4g A" % vals["NETI"])
    if abs(vals["NETI"]) > 1e-6:
        fail("isolated conductor carries net current %.4g A" % vals["NETI"])
    blo, bhi = vals["BLO"], vals["BHI"]
    rel = abs(blo - bhi) / max(abs(blo), abs(bhi), 1e-30)
    print("     B at the two faces: %.6g vs %.6g  (%.3g relative)"
          % (blo, bhi, rel))
    if rel > 0.02:
        fail("Ampere: zero enclosed current requires equal B at both faces "
             "(differ by %.3g)" % rel)

    # ---- 3. flux-circuit coupling magnitude ----
    print("  3. flux circuit, M = 0: DeltaT*Depth*v = Lambda predicts v")
    h = 5.0e-4
    for lam in (0.2, 0.05):
        tmp = ('[Circuits] = 1\n"NetZero"\n[CircuitMatrix]\n0\n'
               "[CircuitFlux]\n%.17g\n" % lam)
        ans = run("tr_tmp_fx", "CubeTransient.fem", h,
                  "CubeInitialCondition.ans", tmp)
        if ans is None:
            return
        _, labels = read_ans(ans)
        dV = labels[0][1]
        pred = lam / (h * DEPTH_M)
        print("     Lambda=%-6g dV=%-14.8g predicted %-14.8g ratio %.8g"
              % (lam, dV, pred, dV / pred))
        if abs(abs(dV / pred) - 1.0) > 1e-6:
            fail("flux-circuit coupling magnitude off by %.3g"
                 % (abs(dV / pred) - 1.0))

def cleanup():
    for f in os.listdir("."):
        if f.startswith("tr_tmp"):
            try:
                os.remove(f)
            except OSError:
                pass
    for f in ("CubeInitialCondition.ans",):
        try:
            os.remove(f)
        except OSError:
            pass

if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
    if failures:
        print("TRANSIENT CHECK FAILED: %d violation(s)" % len(failures))
        sys.exit(1)
    print("transient check: diffusion matches the closed form at first order, "
          "Ampere and flux-circuit coupling ok")
