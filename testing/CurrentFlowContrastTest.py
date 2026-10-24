# CurrentFlowContrastTest.py
# An AC current-flow solve must converge in the low-conductivity regions, not
# only in the high-conductivity ones.
#
#     build/bin/femm_headless -run testing/CurrentFlowContrastTest.py
#
# Reported from the field (2026-10): a 50 Hz model with copper turns (sigma
# 5.8e7) in epoxy (sigma 1e-9, omega*eps ~ 1e-8) solved to voltages hundreds of
# volts BELOW the lowest conductor, where FEMM 4.2 gave a sensible field. The
# complex BiCG in solvers/common/cspars.cpp stopped on |R| / |b|, which the
# copper rows dominate: an error of hundreds of volts in the epoxy is a
# residual ~1e-17 of |b|, so the solve "converged" with the epoxy unsolved. It
# now stops on the preconditioned residual |M^-1 R| / |M^-1 b|, a relative
# error in the voltages whatever the scale of each row.
#
# The model has the reported one's structure, not its geometry: four copper
# turns at 28.0-28.3 kV, each bounded entirely by its conductor, in an epoxy
# slab over a weakly conducting core (sigma 0.1) whose far face is grounded.
# The answer at Precision 1e-8 is checked against the same model solved to
# 1e-14, and every voltage must lie between ground and the highest turn.
#
# Measured on the old stopping test: points off by 300-980 V (up to 3.5% of
# the drive) and 1226 of 5479 nodes below -1 V. On the new one: within 2e-8.
#
# WELL-POSEDNESS MATTERS HERE. A turn whose copper faces the epoxy WITHOUT a
# conductor on that face, or a floating copper body, puts a coefficient ratio
# of ~6e15 into one row -- past 1/eps -- and the epoxy's contribution is lost
# in round-off when the row is formed. No stopping test converges that model;
# solved to 1e-14 it never terminates. Keep every copper boundary a conductor.

import os

fails = []

VTOP = 28300.0
TURNS = [(10, 28000.0), (20, 28100.0), (30, 28200.0), (40, 28300.0)]
PTS = [("epoxy, top left", 2, 27), ("epoxy, top middle", 25, 27),
       ("epoxy, top right", 48, 27), ("epoxy, left edge", 2, 15),
       ("epoxy, between turns", 15, 15), ("epoxy, under turns", 25, 5),
       ("epoxy, right edge", 47, 5), ("core", 25, -5)]


def label(x, y, mat, size):
    ci_addblocklabel(x, y)
    ci_selectlabel(x, y)
    ci_setblockprop(mat, 0, size, 0)
    ci_clearselected()


def solve(precision):
    newdocument(3)
    ci_probdef("millimeters", "planar", 50, precision, 20, 30)
    ci_addmaterial("epoxy", 1e-9, 1e-9, 3.5, 3.5, 0, 0)
    ci_addmaterial("copper", 5.8e7, 5.8e7, 1, 1, 0, 0)
    ci_addmaterial("core", 0.1, 0.1, 10, 10, 0, 0)
    ci_drawrectangle(0, 0, 50, 30)        # epoxy slab
    ci_drawrectangle(0, -10, 50, 0)       # weakly conducting core below it
    label(2, 28, "epoxy", 1)
    label(25, -5, "core", 1)
    ci_addconductorprop("gnd", 0, 0, 1)
    ci_selectsegment(25, -10)
    ci_setsegmentprop("<None>", 0, 1, 0, 0, "gnd")
    ci_clearselected()
    for k, (x, v) in enumerate(TURNS):
        c = "turn%d" % k
        ci_addconductorprop(c, v, 0, 1)
        ci_drawrectangle(x - 3, 10, x + 3, 20)
        label(x, 15, "copper", 1)
        for (sx, sy) in ((x - 3, 15), (x + 3, 15), (x, 10), (x, 20)):
            ci_selectsegment(sx, sy)
        ci_setsegmentprop("<None>", 0, 1, 0, 0, c)
        ci_clearselected()
    ci_saveas("_cf_contrast.fec")
    ci_analyze()
    ci_loadsolution()
    out = [co_getpointvalues(x, y)[0] for (_, x, y) in PTS]
    ci_close()
    return out


ref = solve(1e-14)
got = solve(1e-8)

for (name, _, _), vr, vg in zip(PTS, ref, got):
    vr = complex(vr)
    vg = complex(vg)
    err = abs(vg - vr) / VTOP
    inside = -1e-6 * VTOP < vg.real < VTOP * (1 + 1e-6)
    good = err < 1e-6 and inside
    print("  %-21s V = %11.3f   ref %11.3f   err %.1e  %s"
          % (name, vg.real, vr.real, err, "ok" if good else "FAIL"))
    if err >= 1e-6:
        fails.append("%s: |V - Vref| / %g = %.3g" % (name, VTOP, err))
    if not inside:
        fails.append("%s: V = %g, outside [0, %g]" % (name, vg.real, VTOP))

for ext in (".fec", ".anc"):
    try:
        os.remove("_cf_contrast" + ext)
    except OSError:
        pass

if fails:
    for m in fails:
        print("   " + m)
    print("current-flow contrast: %d FAILED" % len(fails))
else:
    print("current-flow contrast: %d points within 1e-6 of the 1e-14 solve "
          "and between ground and the drive" % len(PTS))
