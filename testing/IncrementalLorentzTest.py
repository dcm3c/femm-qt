# IncrementalLorentzTest.py -- the 1x incremental Lorentz force/torque block
# integrals (mo_blockintegral types 28/29/30). CPython; drives femm_headless.
#
# These three types are deliberately NOT compared against Original FEMM.
# OF's postprocessor reads the previous-solution current density Jp from the
# fifth field of the .ans element record, but the solver writes it eighth
# (after the three edge-boundary indices), so OF reads e0 -- a boundary index,
# -1 on every interior element. Its Jp term is therefore garbage of negligible
# size, which leaves OF computing only the j~ x B_bias half of the force and
# silently dropping the Jp x b~ half. femm-qt reads the field the solver
# writes and converts MA/m^2 -> A/m^2, so it disagrees with OF by design and
# needs an oracle of its own. This is that oracle.
#
# THE ORACLE. The 1x incremental Lorentz force is, by construction, the
# first-order term of the force in the perturbation amplitude:
#
#     F(bias + d*pert) = F(bias) + d * INT( Jp x b~ + j~ x Bp ) + O(d^2)
#
# so types 28/29 must equal the DERIVATIVE of the ordinary nonlinear DC
# Lorentz force (types 11/12) with respect to the perturbation amplitude.
# That derivative is available without any reference implementation: solve the
# nonlinear DC problem at bias +/- d and central-difference it. The incremental
# solve linearizes with the incremental permeability, which is exactly the
# tangent the DC finite difference sees as d -> 0, so the two agree in the
# limit -- to solver precision, not merely to a few digits.
#
# Run at a LOW frequency. The incremental solve is an AC solve, so at 60 Hz it
# carries genuine eddy-current phase that the DC finite difference cannot see
# (verified: the imaginary part is linear in frequency). Driving it at 0.01 Hz
# removes that term and leaves the quasi-static force the derivative measures.
#
# Checks:
#   1. x-force (type 28) vs the Richardson-extrapolated finite difference
#   2. y-force (type 29) under a y-direction perturbation, likewise
#   3. no current, no Lorentz force: the steel rotor carries neither Jp nor j~,
#      so all three integrals over it must be IDENTICALLY zero. This is the
#      check that fails loudly if Jp ever picks up a boundary index again --
#      it was -1.97e-06 N before the fix, and is exactly 0 after.

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FEM = os.path.join(HERE, "RadialMagneticBearing.fem").replace("\\", "/")

SCRIPT = r'''
FEM = "%(fem)s"
D   = "%(dir)s/"
def cur(i1, i2, i3, i4):
    mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)

def dc_force(bias, pert, d):
    """Nonlinear DC Lorentz force on the coils at bias + d*pert."""
    open(FEM); mi_saveas(D + "il_dc.fem")
    cur(*[b + d * p for b, p in zip(bias, pert)])
    mi_probdef(0, "inches", "planar")
    mi_analyze(); mi_loadsolution()
    mo_clearblock(); mo_groupselectblock(2)
    return mo_blockintegral(11), mo_blockintegral(12)

def incremental(bias, pert, freq):
    """1x incremental Lorentz integrals for a unit-amplitude perturbation."""
    open(FEM); mi_saveas(D + "il_bias.fem")
    cur(*bias); mi_probdef(0, "inches", "planar"); mi_analyze()
    mi_saveas(D + "il_inc.fem"); mi_setprevious(D + "il_bias.ans", 1)
    cur(*pert); mi_probdef(freq, "inches", "planar")
    mi_analyze(); mi_loadsolution()
    out = []
    for grp in (2, 1):                      # 2 = coils, 1 = steel rotor
        mo_clearblock(); mo_groupselectblock(grp)
        out.append((mo_blockintegral(28), mo_blockintegral(29),
                    mo_blockintegral(30)))
    return out

BIAS = (6, 6, 6, 6)
for name, pert in (("x", (1, 0, -1, 0)), ("y", (0, 1, 0, -1))):
    (c28, c29, c30), (r28, r29, r30) = incremental(BIAS, pert, 0.01)
    print("INC", name, repr(c28.real), repr(c29.real),
          repr(abs(r28)), repr(abs(r29)), repr(abs(r30)))
    for d in (0.25, 0.125):
        fxp, fyp = dc_force(BIAS, pert,  d)
        fxm, fym = dc_force(BIAS, pert, -d)
        print("FD", name, d, repr((fxp - fxm) / (2 * d)),
              repr((fyp - fym) / (2 * d)))
print("DONE")
'''


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "femm_headless"
    tmp = tempfile.mkdtemp(prefix="inclorentz")
    path = os.path.join(tmp, "run.py")
    with open(path, "w") as f:
        f.write(SCRIPT % {"fem": FEM, "dir": tmp.replace("\\", "/")})
    out = subprocess.run([exe, "-run", path], capture_output=True, text=True).stdout
    if "DONE" not in out:
        print("incremental-Lorentz check: solver run FAILED\n" + out[-500:])
        return 1

    inc, fd = {}, {}
    for line in out.splitlines():
        t = line.split()
        if t[:1] == ["INC"]:
            inc[t[1]] = tuple(float(x) for x in t[2:])
        elif t[:1] == ["FD"]:
            fd.setdefault(t[1], {})[float(t[2])] = (float(t[3]), float(t[4]))

    problems = []
    for axis, comp in (("x", 0), ("y", 1)):
        # Richardson: the central difference is second order, so
        # F' = FD(h/2) + (FD(h/2) - FD(h))/3 kills the leading h^2 term.
        coarse = fd[axis][0.25][comp]
        fine = fd[axis][0.125][comp]
        exact = fine + (fine - coarse) / 3.0
        got = inc[axis][comp]
        rel = abs(got - exact) / max(abs(exact), 1e-30)
        if rel > 1e-5:
            problems.append("%s-force: incremental %.9g vs finite difference "
                            "%.9g (%.2g relative)" % (axis, got, exact, rel))
        # the rotor carries no current: every 1x Lorentz integral must vanish
        for k, name in ((2, "28"), (3, "29"), (4, "30")):
            if inc[axis][k] != 0.0:
                problems.append("%s: rotor type %s = %g, must be identically 0 "
                                "(Jp reading a boundary index again?)"
                                % (axis, name, inc[axis][k]))

    if problems:
        print("incremental-Lorentz check FAILED: " + "; ".join(problems))
        return 1
    print("incremental-Lorentz check: types 28/29 match the finite-difference "
          "of the nonlinear DC force to <1e-5; current-free rotor gives "
          "identically zero")
    return 0


if __name__ == "__main__":
    sys.exit(main())
