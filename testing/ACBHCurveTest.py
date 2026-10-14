# ACBHCurveTest.py -- the effective AC B-H curve against an independent oracle.
# Runs inside the engine (femm_headless -run).
#
# For a harmonic problem a nonlinear material's static curve B(H) is replaced
# by an effective curve: drive H = Hi sin(t), and the effective B at Hi is the
# fundamental of the resulting B waveform,
#
#     b1(Hi) = (4/pi) * INT_0^{pi/2} B(Hi sin t) sin t dt
#
# GetSlopes(omega) evaluates that integral in closed form over the piecewise-
# linear curve, in two copies that must agree: the solver's (fkn/matprop.cpp)
# and the postprocessor's (libfemm/results_doc.cpp). Original FEMM 4.2's closed
# form was wrong -- 2.3% high on this very curve at 1 T -- so the AC rows of the
# OF golden comparisons cannot vouch for it (see of_ac_curve.py). This is the
# oracle that replaces them.
#
# THE FIXTURE. block.fem's 1" cube of M-19 Steel, with the material stripped of
# everything else GetSlopes does at frequency: conductivity 0 (no lamination
# eddy currents), fill 1 (no fill-factor mix), and its hysteresis lag is
# already 0. The right-hand boundary becomes a mixed condition with c0 = 0,
# which prescribes the tangential H there; with A = 0 on the left and no
# sources, H is then uniform through the block and equal to the drive. So:
#
#   * the SOLVER copy is checked by B: the solver has to find b1(Hi) itself;
#   * the POSTPROCESSOR copy is checked by H: mo_getpointvalues converts the
#     solved B back to H through its own copy of the curve.
#
# Each drive is a knot of the curve, where the spline through the effective
# points is exact, so no interpolation error enters. The DC rows check the
# drive itself (B must land on the static table value) before the AC rows rely
# on it.
#
# THE ORACLE integrates numerically, split at the curve's kinks (the segment
# ends, at t = asin(H_k/Hi)), with 8-point Gauss-Legendre on each piece. Within
# a piece the integrand (a + s*Hi*sin t)*sin t is analytic, so this converges
# to machine precision without sharing any algebra with the closed form.
#
# TOLERANCE 1e-5 relative in B, the nonlinear budget of FQReferenceCompare.py:
# the nonlinear iteration stops at a residual of 1e-6. Saturation amplifies a
# B error into H by the local dlnH/dlnB, so the H check is divided by that
# slope (taken from the same oracle curve) and judged in B too. Measured worst
# 4.4e-7. The old formula misses by 3e-3 to 2.3e-2.

import math

TOL = 1e-5

# M-19 B-H table, read from the fixture so the oracle and the model cannot
# drift. The mode argument matters: a bare open("x.fem") opens a document.
Bt, Ht = [], []
inblk = False
for ln in open("block.fem", "r").read().split("\n"):
    if "<BHPoints>" in ln:
        inblk = True
        continue
    if inblk:
        p = ln.split()
        if len(p) != 2:
            break
        Bt.append(float(p[0]))
        Ht.append(float(p[1]))

GX = [-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
      -0.1834346424956498, 0.1834346424956498, 0.5255324099163290,
      0.7966664774136267, 0.9602898564975363]
GW = [0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
      0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
      0.2223810344533745, 0.1012285362903763]

def fundamental(i):
    """b1 at Hi = Ht[i], by quadrature of B(Hi sin t) sin t over 0..pi/2."""
    Hi = Ht[i]
    tot = 0.0
    for k in range(1, i + 1):
        if Ht[k] == Ht[k - 1]:
            continue
        s = (Bt[k] - Bt[k - 1]) / (Ht[k] - Ht[k - 1])
        a = Bt[k - 1] - s * Ht[k - 1]
        t0 = math.asin(Ht[k - 1] / Hi)
        t1 = math.asin(min(Ht[k] / Hi, 1.0))
        c, r = (t0 + t1) / 2, (t1 - t0) / 2
        for x, w in zip(GX, GW):
            t = c + r * x
            tot += w * r * (a + s * Hi * math.sin(t)) * math.sin(t)
    return 4 / math.pi * tot

open("block.fem")
mi_saveas("temp.fem")
mi_modifymaterial("M-19 Steel", 5, 0)   # conductivity: no lamination eddy currents
mi_modifymaterial("M-19 Steel", 8, 1)   # fill factor: no fill mix
mi_modifyboundprop("Ahi", 1, 0)
mi_modifyboundprop("Ahi", 9, 2)         # mixed: prescribes tangential H
mi_modifyboundprop("Ahi", 7, 0)

# knots from the linear region through deep saturation
KNOTS = [i for i in range(len(Bt)) if any(abs(Bt[i] - b) < 1e-9
         for b in (0.5, 1.0, 1.3, 1.5, 1.7, 1.9, 2.1))]

fails = []
worst = 0.0
for freq in (0, 60):
    mi_probdef(freq, "inches", "planar")
    for i in KNOTS:
        mi_modifyboundprop("Ahi", 8, Ht[i])
        mi_analyze()
        mi_loadsolution()
        v = mo_getpointvalues(0, 0)
        mo_close()
        Bgot = math.sqrt(abs(v[1])**2 + abs(v[2])**2)
        Hgot = math.sqrt(abs(v[5])**2 + abs(v[6])**2)
        curve = (lambda j: Bt[j]) if freq == 0 else fundamental
        Bwant = curve(i)
        eb = abs(Bgot - Bwant) / Bwant
        # H inherits B's error times the curve's local log-slope dlnH/dlnB,
        # which reaches ~18 near 2.1 T; divide it back out so both checks
        # are held to the same budget in B.
        g = math.log(Ht[i + 1] / Ht[i - 1]) / math.log(curve(i + 1) / curve(i - 1))
        eh = abs(Hgot - Ht[i]) / Ht[i] / g
        worst = max(worst, eb, eh)
        if eb > TOL:
            fails.append("f=%d H=%g: B %.9g, want %.9g (%.2g)"
                         % (freq, Ht[i], Bgot, Bwant, eb))
        if eh > TOL:
            fails.append("f=%d H=%g: postprocessor H %.9g, driven %.9g "
                         "(%.2g, in B)" % (freq, Ht[i], Hgot, Ht[i], eh))

if fails:
    for m in fails:
        print("   " + m)
    print("ac b-h curve: %d FAILED" % len(fails))
else:
    print("ac b-h curve: solver and postprocessor both match the quadrature "
          "fundamental at %d knots from 0.5 T to 2.1 T (worst %.1e); "
          "DC lands on the table" % (len(KNOTS), worst))
