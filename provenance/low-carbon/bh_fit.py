# SPDX-License-Identifier: MIT
"""bh_fit.py -- unified phenomenological B-H model for low-carbon steel.
The current best fit, not a finished one.  Supersedes bh_fit_v10.py.
The paper describing it is bhcurves.tex; equation numbers below are its.

MODEL (MODEL=two; four processes as additive shares of one saturation):

  J(H; C, Mn, cond) = Js * [ fi P(H/hS)                        toe
                           + fS (1 - 1/sqrt(1+(H/hS)^2))       soft walls
                           + (1-fi-fS-fr) Burr(H; hW,kW,mW)    sharp walls
                           + fr Burr(H; hR,kR,mR) ]            rotation
  B = J + mu0*H,   Burr(H;a,k,m) = 1 - (1+(H/a)^k)^(-m)
  P(u) = u/sqrt(1+u^2)  (TOE=straight, default; TOE=rational gives u/(1+u))
  hS = hW exp(lqS[reg]): the toe and the soft walls share one scale.

  At small H, J/Js = fi u + (fS/2) u^2 with u = H/hS: Rayleigh's initial
  relation, the toe carrying mu_i and the soft walls nu.  At large H the
  soft walls leave Bozorth's a0/H with a0 = fS hS; the sharp walls' tail
  exponent kW*mW is held at 2 +/- 0.3 (TAIL0, TAIL_SIG) and the rotation's
  kR*mR at 2 +/- 0.05 (SIG_T), Akulov's H^-2, with
  hR = Ha sqrt((2/105)/fr) exp(lrs) and Ha = 2 K1/Js per grade.

  Js is Eq. (5), Js = 2.158 - 0.1736 C - 0.0228 Mn (T, wt%), from
  composition alone (bhlib.js_anchor); it is never fitted.  Each curve
  carries a level nuisance dJs and a field-scale nuisance dlh under
  priors; they correct that source's calibration, not the family.

REGIMES.  The four conditions (cond: as hot rolled, 1300F, 1400F, 1500F,
all anneals furnace cooled) fall into three regimes, split at A1
(727 C = 1341 F):

  reg 0  as hot rolled             dislocation / residual-stress pinning
  reg 1  annealed below A1         1300F: ferrite recovered, cementite
                                   spheroidised
  reg 2  annealed above A1         1400F, 1500F: lamellar pearlite re-forms
                                   on furnace cooling

CARBON enters through x = C/0.77 (Eqs. 8-9):

  ln hW = lhW[cond] + bW[reg] x        ln kW = lkW[reg] + bK[reg] x
  fS = sigmoid(lfS + bFS x)            fi = sigmoid(lfi + bFI x)

  global:         lfr, rot (ln kR, ln mR), lrs    rotation
                  lfi, bFI                        toe share
                  lfS, bFS                        soft-wall share
  per regime:     lqS, bW, lkW, bK, ltail         (ltail = ln kW*mW)
  per condition:  lhW
  27 shape parameters.  The form lives in bhmodel.py; every downstream
  script evaluates it through bhmodel.Family.

ANCHOR.  No source measures below about 30 A/m, so the toe and soft walls
are held to Bozorth's Rayleigh constants for annealed iron (Ferromagnetism
Table 2 p.494: mu_i = 200, nu = 2000/Oe) at ARMCO grade 2, 1400F, within
0.4 in the log (RAY_ANCHOR, RAY_CONDS).  The as-rolled end is not anchored.

DATA.  bhlib.load_curves: SLAC-TN-89-04 and the U.S. Steel datasheet
traces, with Fermilab TM-1197 held out.  Chart points below 0.3 kG are
dropped (BMIN_KG).  Cheney's annealed iron-carbon series is validation,
not fit data (CHENEY=1 adds it).  Residuals are in ln H at fixed B;
multi-start least squares with LM and trf/soft-L1 stages (NSTART, NOLM).

KNOBS.  Environment variables select variants: MODEL (two, splice, ...),
TOE, A1SPLIT=0 (the two-regime family), EXCLUDE, RAY_ANCHOR, TAIL0,
TAIL_SIG, SIG_T, BMIN_KG, CHENEY, P0 (warm start).  The recipe is written
to "variant" in the output JSON, which Family reads.

Outputs (TAG unset = the default fit):
  out/fit_params.json         fitted parameters, read by everything else
  femm_<grade>_<cond>.csv     9 grades x 4 conditions, B tesla / H A/m
  bh_model_fit.m              closed-form Mathematica 9 emitter
  out/fit_percurve.png, out/fit_conditions.png
With TAG=name everything goes under out/fit_name/ instead, so variants
can be compared without touching the default fit.
"""

import json
import os
import numpy as np
from scipy.optimize import least_squares
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import bhlib as L
import bhmodel as MDL

# ---------------------------------------------------------------- variant
TAIL_PER_REGIME = os.environ.get("TAIL_PER_REGIME", "1") == "1"
BW_PER_REGIME = os.environ.get("BW_PER_REGIME", "1") == "1"
AKULOV_SIG = float(os.environ.get("AKULOV_SIG", "0.1"))   # 1e3 = off
BW_SIG = float(os.environ.get("BW_SIG", "1.0"))
# One-sided floor on h_t/h_W over the EMITTED grades.  0 = off.  Below
# about 3 the toe saturates before the wall knee arrives and the
# permeability sags, which is what fails the single-hump gate.
TOE_RATIO = float(os.environ.get("TOE_RATIO", "0"))
# "fit"  = toe scale free per condition, curvature free (par lhi/bI/lc)
# "tied" = toe peak AT h_W, so h_t = h_W/toe_peak_u(cr); lhi/bI drop out
# "splice" = no additive toe; Rayleigh below H2 = h_W exp(lq), joined C1
# "splicemu" = the same toe parameterized by mu_1 = exp(lm + bM x); H2 solved
# "rb"   = one wall kernel, [1-(1+u^k)^(-1/k)]^(2/k): H^2 onset, 1/H tail
# "two"  = soft (k=2,m=1/2) and sharp wall populations, toe tied to the soft
# "splicew"  = the join a fraction sigmoid(lqw) into its own window, per regime
MODEL = os.environ.get("MODEL", "two")
# Optional prior on the knee width, ln kW ~ N(ln KW0[regime], KW_SIG).  0 = off.
# Decoupling the toe from h_W lets kW run to 14-89, a step knee that the
# Rayleigh parabola then climbs to; the plain and splice_t fits sat at 1.9-4.7.
KW_SIG = float(os.environ.get("KW_SIG", "0"))
KW0 = [float(v) for v in os.environ.get("KW0", "1.9,3.5").split(",")]
# Optional prior on the carbon slope of the knee width, bK ~ N(BK0, BK_SIG).
# The additive-toe fit put it at 0.33 / 0.24; the spliced fits want 0.8 for
# the anneals, which sharpens 1030's knee and moves its join window up.
BK_SIG = float(os.environ.get("BK_SIG", "0"))
BK0 = [float(v) for v in os.environ.get("BK0", "0.33,0.24").split(",")]
# Optional prior on the wall tail exponent, ln(kW*mW) ~ N(ln TAIL0, TAIL_SIG).
# Bozorth pp.486-488: the law of approach is I = Is - a0/H - b0/H^2, the
# b0 term from anisotropy (the rotation Burr, pinned at 2) and the a0/H
# term from inhomogeneity (Neel 48N4), which is the wall population's to
# carry.  Free, the fits put it at 0.85-1.15 on data that stop at 1000 Oe.
# Under MODEL=two the tail prior is on the SHARP population, which must
# die after the knee (2 +/- 0.3) so that the soft one alone carries a0/H.
# Under MODEL=splice it is on the single wall term, held at 1/H.
TAIL_SIG = float(os.environ.get("TAIL_SIG",
                                "0.3" if MODEL == "two" else "0.05"))
TAIL0 = float(os.environ.get("TAIL0", "2.0" if MODEL == "two" else "1.0"))
# One-sided floor on the as-rolled join ratio H2/h_W.  The as-rolled wall
# term (k_W ~ 2) has no slope-2 crossing, so its join window has no bottom
# and the fit can push the toe to nothing.  0 = off.
LQ_MIN = float(os.environ.get("LQ_MIN", "0.33"))
# MODEL=two: anchor the pure-iron end of the toe to Bozorth's Rayleigh
# constants for iron (Ferromagnetism, Table 2, p.494): annealed
# mu_i = 200, nu = 2000 per oersted; unannealed mu_i = 80, nu = 60.
# Applied to ARMCO grade 2 at 1400 F and as rolled.  The value is the
# sigma in the logarithm of each; 0 = off.  Default: the annealed anchor
# alone at 0.4; the as-rolled one is available through RAY_CONDS=2,4.
RAY_ANCHOR = float(os.environ.get("RAY_ANCHOR", "0.4"))
# A1SPLIT=1: three regimes -- as rolled, annealed below A1 (1300 F), and
# annealed above A1 (1400, 1500 F) -- instead of one shared anneal family.
NREG = 2 if os.environ.get("A1SPLIT") == "0" else 3   # default: split at A1
# MODEL=two toe form: "straight" u/sqrt(1+u^2) (no u^2 term, 1/H^2 tail)
# or "rational" u/(1+u), the form used before 2026-09-27.
TOE = os.environ.get("TOE", "straight")
SHAPE_FN = (MDL.shape_two_straight if MODEL == "two" and TOE == "straight"
            else MDL.SHAPES[MODEL])
# RAY_CONDS picks which anchors apply: "4" is the annealed one alone.
RAY_CONDS = [int(v) for v in os.environ.get("RAY_CONDS", "4").split(",")]
RAY_TARGETS = tuple(t for t in ((2, 80.0, 60.0), (4, 200.0, 2000.0))
                    if t[0] in RAY_CONDS)   # cond, mu_i, nu /Oe
LQ_SIG = float(os.environ.get("LQ_SIG", "0.05"))
# Require the emitted permeability to RISE below RISE_H A/m, which is
# the defect the single-hump gate catches, instead of steering the
# parameters that happen to cause it.  0 = off.
RISE_SIG = float(os.environ.get("RISE_SIG", "0"))
RISE_H = float(os.environ.get("RISE_H", "100"))
TOE_SIG = float(os.environ.get("TOE_SIG", "0.1"))
TAG = os.environ.get("TAG", "")
if TAG:
    OUT = f"out/fit_{TAG}"
    PARAMS, CSVDIR, MMA, PFX = f"{OUT}/fit_params.json", OUT, None, ""
else:
    OUT = "out"
    PARAMS, CSVDIR, MMA, PFX = "out/fit_params.json", ".", "bh_model_fit.m", "fit_"
os.makedirs(OUT, exist_ok=True)
# Ha is no longer a single number: each curve gets 2*K1/Js(C,Mn).  The
# old fit anchored the Akulov prior at 2*K1/2.11, a representative Js.
print(f"variant: TAIL_PER_REGIME={TAIL_PER_REGIME} BW_PER_REGIME="
      f"{BW_PER_REGIME} AKULOV_SIG={AKULOV_SIG} BW_SIG={BW_SIG} "
      f"TOE_RATIO={TOE_RATIO} MODEL={MODEL} -> {PARAMS}")

rng = np.random.default_rng(20260917)
curves = L.load_curves()
n = len(curves)

# Curves listed in EXCLUDE (semicolon-separated names) stay in the set
# for reporting but carry zero weight in the fit.
EXCLUDE = [s.strip() for s in
           os.environ.get("EXCLUDE", "").split(";")
           if s.strip()]
for _cu in curves:
    _cu.excluded = _cu.name in EXCLUDE
    if _cu.excluded:
        _cu.w = _cu.w * 0.0
        print(f"excluded from fit: {_cu.name}")
conds = sorted({c.cond for c in curves})
ci = {d: i for i, d in enumerate(conds)}
SIG_T = float(os.environ.get("SIG_T", "0.05"))   # Akulov tail prior, kR*mR = 2 +/- SIG_T
S_DLH_T, S_DLH_L = 0.03, 0.15

# ----------------------------------------------------------------- layout
IX, _p0 = {}, []


def _add(name, vals):
    IX[name] = (len(_p0), len(_p0) + len(vals))
    _p0.extend(vals)


_add("lfr", [np.log(0.18 / 0.82)])
_add("rot", [np.log(1.88), np.log(1.06)])
# ln multiplier on the Akulov hR; LRS_PER_REGIME=1 gives one per regime, the
# test of whether the level tracks residual stress (as rolled highest)
LRS_PER_REGIME = os.environ.get("LRS_PER_REGIME", "0") == "1"
_add("lrs", [0.0] * (NREG if LRS_PER_REGIME else 1))
# LRS_BC=1: carbon slope on that level (prior BR_SIG), the cementite test
LRS_BC = os.environ.get("LRS_BC", "0") == "1"
BR_SIG = float(os.environ.get("BR_SIG", "1.0"))
if LRS_BC:
    _add("bR", [0.0])
# LRS0: centre of the level prior (ln), for profiling it with a tight sigma
LRS0 = float(os.environ.get("LRS0", "0"))
SPLICED = MODEL in ("splice", "splicemu", "splicew")
if not SPLICED:
    _add("lfi", [np.log(0.06 / 0.94)])
    _add("bFI", [0.0])
if MODEL == "splicew":
    _add("lqw", [0.0, 0.0])     # join position in its window, per regime
elif MODEL == "splicemu":
    # the toe's initial permeability, log-linear in x per regime; the
    # join field is solved from the C1 condition
    _add("lm", [np.log(0.05), np.log(0.06)])   # ln(mu_1 mu0 h_W/Js)
    _add("bM", [0.0, 0.0])
elif MODEL == "splice":
    # the join, as a ratio to h_W per regime; a and b follow from C1
    _add("lq", [np.log(1.3)] * 2)
elif MODEL == "rb":
    # the toe scale as a ratio to the wall scale; the kernel has no m
    _add("lqt", [np.log(0.3)] * 2)
elif MODEL == "two":
    _add("lfS", [np.log(0.55 / 0.45)])     # soft-population share
    _add("bFS", [0.0])
    _add("lqS", [np.log(0.5)] * NREG)      # hS as a ratio to hW, per regime
elif MODEL == "tied":
    # h_t is h_W/toe_peak_u(cr), so there is no independent toe scale
    # and no independent carbon slope on it.  cr = 1 + exp(lcr) > 1,
    # which a peak requires.
    _add("lcr", [np.log(9.0)] * 2)
else:
    _add("lhi", list(np.log([200., 100., 60., 45.])))
    _add("bI", [0.0, 0.0])
    _add("lc", [np.log(3.0)] * 2)   # toe curvature; 0 = plain h/(h+hi)
_add("lhW", list(np.log([330., 190., 110., 110.])))
_add("bW", [1.5] * NREG if BW_PER_REGIME else [1.5] * 4)
_add("lkW", list(np.log([2.0] + [3.9] * (NREG - 1))))
_add("bK", [0.0] * NREG)
if MODEL != "rb":      # rayburr has no m: its tail is 1/H by construction
    _add("ltail", [np.log(1.2)] * (NREG if TAIL_PER_REGIME else 1))
_add("dJs", [0.0] * n)
_add("dlh", [0.0] * n)
p0 = np.array(_p0)
NSHAPE = IX["dJs"][0]
if os.environ.get("P0"):
    _w = json.load(open(os.environ["P0"]))["params"]
    for _k, (_a, _b) in IX.items():
        if _k in _w and len(_w[_k]) == _b - _a:
            p0[_a:_b] = _w[_k]
    print(f"warm start from {os.environ['P0']}")


def get(p, name):
    a, b = IX[name]
    return p[a:b]


def pdict(p):
    return {k: get(p, k) for k in IX}


def shape(p, c, d, h, js):
    return SHAPE_FN(pdict(p), ci, c, d, h, js)


def jmodel(p, cu, i, h):
    hh = h * np.exp(get(p, "dlh")[i])
    return (cu.js_a + get(p, "dJs")[i]) * shape(p, cu.c, cu.cond, hh,
                                                cu.js_a)


# The emitter's compositions, so the ratio floor is applied where the
# gate is actually tested rather than only where curves were fitted.
GATE_GRADES = [(0.010, 0.100), (0.040, 0.35), (0.060, 0.325),
               (0.080, 0.40), (0.100, 0.45), (0.175, 0.75),
               (0.180, 1.15), (0.200, 0.45), (0.300, 0.75)]
# The emitted grid up to RISE_H, origin included -- the origin secant is
# where the sag was worst and the old gate never looked.
_hg = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])
RISE_GRID = _hg[_hg <= RISE_H]


def resid(p):
    djs, dlh = get(p, "dJs"), get(p, "dlh")
    r = [np.sqrt(cu.w) * (cu.j - jmodel(p, cu, i, cu.h))
         for i, cu in enumerate(curves)]
    r.append(np.array([djs[i] / cu.sig_js for i, cu in enumerate(curves)]))
    r.append(np.array([dlh[i] / (S_DLH_T if cu.trust else S_DLH_L)
                       for i, cu in enumerate(curves)]))
    kR, mR = np.exp(get(p, "rot"))
    r.append(np.array([(kR * mR - 2.0) / SIG_T]))
    # Akulov consistency is in the parameterisation now: hR is built from
    # each curve's own Ha = 2*K1/Js, and only the overall level lrs is
    # free.  So the residual is just a prior keeping that level near 1.
    r.append((get(p, "lrs") - LRS0) / AKULOV_SIG)
    if LRS_BC:
        r.append(get(p, "bR") / BR_SIG)
    r.append((get(p, "bW") - 1.5) / BW_SIG)
    r.append(get(p, "bK") / 1.0)
    if not SPLICED:
        # The toe share was held loosely near 6% before it had a source.
        # With the Bozorth anchor on, mu_i fixes it (about 2% at C = 0),
        # and the old prior only pulls against that; it applies only
        # when the anchor is off.
        if RAY_ANCHOR == 0:
            r.append((get(p, "lfi") - np.log(0.06 / 0.94)) / 1.5)
        r.append(get(p, "bFI") / 3.0)
    if MODEL not in ("tied", "splice", "splicemu", "splicew", "rb", "two"):
        r.append(get(p, "bI") / 6.0)
    # The toe curvature is weakly identified, so this bounds it rather
    # than steering it: sigma 3 in ln cr is a factor of 20 either way.
    if MODEL not in ("tied", "splice", "splicemu", "splicew", "rb", "two"):
        r.append((get(p, "lc") - np.log(3.0)) / 3.0)
    if MODEL == "two":
        # the sharp population must keep a positive share, and its
        # soft-population slope gets the same weak prior as the others
        pd = pdict(p)
        fr0 = MDL._sig(get(p, "lfr")[0])
        q = []
        for cg, _mn in GATE_GRADES:
            xg = cg / 0.77
            fi0 = MDL._sig(get(p, "lfi")[0] + get(p, "bFI")[0] * xg)
            fS0 = MDL._sig(get(p, "lfS")[0] + get(p, "bFS")[0] * xg)
            q.append(max(0.0, 0.05 - (1.0 - fi0 - fS0 - fr0)) * 1e3)
        r.append(np.array(q))
        r.append(get(p, "bFS") / 3.0)
    if RAY_ANCHOR > 0 and MODEL == "two":
        pd = pdict(p)
        c0, mn0 = 0.010, 0.100
        js0, x0 = L.js_anchor(c0, mn0), c0 / 0.77
        fi0 = MDL._sig(get(p, "lfi")[0] + get(p, "bFI")[0] * x0)
        fS0 = MDL._sig(get(p, "lfS")[0] + get(p, "bFS")[0] * x0)
        for dg, mu_t, nu_t in RAY_TARGETS:
            hS = (MDL.wall_fit(pd, ci, c0, dg)[2]
                  * np.exp(get(p, "lqS")[MDL._reg(pd, dg)]))
            mui = fi0 * js0 / (L.MU0 * hS)
            # the straight toe has no u^2 term, so nu is the soft walls' alone
            nu = (js0 * (fS0 / 2 - (0.0 if TOE == "straight" else fi0))
                  / (L.MU0 * hS**2) * L.OE)
            r.append(np.array([(np.log(mui) - np.log(mu_t)) / RAY_ANCHOR,
                               (np.log(max(nu, 1e-3)) - np.log(nu_t))
                               / RAY_ANCHOR]))
    if KW_SIG > 0:
        r.append((get(p, "lkW") - np.log(KW0)) / KW_SIG)
    if BK_SIG > 0:
        r.append((get(p, "bK") - np.array(BK0)) / BK_SIG)
    if TAIL_SIG > 0 and "ltail" in IX:
        r.append((get(p, "ltail") - np.log(TAIL0)) / TAIL_SIG)
    if LQ_MIN > 0 and MODEL == "splice":
        r.append(np.array([max(0.0, np.log(LQ_MIN) - get(p, "lq")[0])]) / LQ_SIG)
    if MODEL == "splicemu":
        # a mu_1 the wall term cannot join to is a residual, and the
        # carbon slope is given the same weak prior as the other slopes
        pd = pdict(p)
        # shortfall converted to mu_1 units; sigma 50 in permeability
        q = [MDL.splice_join_mu(pd, ci, cg, dg, L.js_anchor(cg, mng))[3]
             * L.js_anchor(cg, mng) / L.MU0 / 50.0
             for cg, mng in GATE_GRADES for dg in conds]
        r.append(np.array(q))
        r.append(get(p, "bM") / 3.0)
    if MODEL == "splice":
        # the toe must have a positive initial slope, which holds only
        # where the join sits below local log-log slope 2; over the
        # emitted grades, since that is where it is emitted
        pd = pdict(p)
        q = []
        for cg, mng in GATE_GRADES:
            for dg in conds:
                a, _b, _H2 = MDL.splice_rayleigh(pd, ci, cg, dg,
                                                 L.js_anchor(cg, mng))
                q.append(max(0.0, -a) * 1e4)
        r.append(np.array(q))
    if TOE_RATIO > 0 and MODEL not in ("tied", "splice", "splicemu", "splicew"):
        q = []
        pd = pdict(p)
        for cg, _mn in GATE_GRADES:
            for dg in conds:
                _fi, hi, hW, _k, _m = MDL.wall_fit(pd, ci, cg, dg)
                q.append(max(0.0, np.log(TOE_RATIO) - np.log(hi / hW)))
        r.append(np.array(q) / TOE_SIG)
    if RISE_SIG > 0:
        q = []
        for cg, mng in GATE_GRADES:
            for dg in conds:
                bb = (L.js_anchor(cg, mng)
                      * shape(p, cg, dg, RISE_GRID, L.js_anchor(cg, mng))
                      + L.MU0 * RISE_GRID)
                dd = np.diff(bb) / np.diff(RISE_GRID) / L.MU0
                # any fall in permeability below RISE_H is the defect
                q.append(np.maximum(0.0, -np.diff(dd)) / dd.max())
        r.append(np.concatenate(q) / RISE_SIG)
    return np.concatenate(r)


def robust_cost(p):
    r = resid(p)
    z = (r / 0.08)**2
    return float(np.sum(0.08**2 * 2 * (np.sqrt(1 + z) - 1)))


if os.environ.get("COSTDBG"):
    print(f"robust cost at p0: {robust_cost(p0):.5f}")
    for _f in os.environ["COSTDBG"].split(","):
        _w = json.load(open(_f))["params"]; _q = p0.copy()
        for _k, (_a, _b) in IX.items():
            if _k in _w and len(_w[_k]) == _b - _a:
                _q[_a:_b] = _w[_k]
        print(f"robust cost at {_f}: {robust_cost(_q):.5f}")
    raise SystemExit(0)
if os.environ.get("JACDBG"):
    r0 = resid(p0)
    print(f"resid at p0: n={len(r0)} |r|={np.linalg.norm(r0):.4f}")
    for k, (a, b) in IX.items():
        if k in ("dJs", "dlh"):
            continue
        for j in range(a, b):
            for step in (1e-6, 1e-3):
                pp = p0.copy(); pp[j] += step
                dr = (resid(pp) - r0) / step
                print(f"  d/d{k}[{j-a}] step {step:g}: |J col| = {np.linalg.norm(dr):.4g}")
    raise SystemExit(0)

# ------------------------------------------------------------ multi-start
print(f"multi-start fit ({NSHAPE} shape parameters; v10 had 38) ...")
cands = []
NSTART = int(os.environ.get("NSTART", "6"))
NOLM = os.environ.get("NOLM") == "1"   # skip the LM stage, trf/soft_l1 only
for s in range(NSTART):
    pj = p0.copy()
    if s > 0:
        pj[:NSHAPE] += rng.normal(0, 0.15, NSHAPE)
    try:
        r1x = pj if NOLM else least_squares(resid, pj, method="lm",
                                            max_nfev=60000).x
        r2 = least_squares(resid, r1x, method="trf", loss="soft_l1",
                           f_scale=0.08, max_nfev=60000)
        cands.append((robust_cost(r2.x), s, r2.x))
        print(f"  start {s}: robust cost = {cands[-1][0]:.5f}")
    except Exception as e:
        print(f"  start {s}: failed ({e})")
cands.sort(key=lambda t: t[0])
p = cands[0][2]
print(f"selected start {cands[0][1]}")

# ------------------------------------------------------------- diagnostics
print("\n================ fitted global parameters ================")
fr = 1 / (1 + np.exp(-get(p, "lfr")[0]))
kR, mR = np.exp(get(p, "rot"))
lrs = get(p, "lrs")[0]
print(f"fr={fr:.4f}   kR={kR:.3f}  mR={mR:.3f}  tail kR*mR={kR*mR:.3f}")
print(f"hR = Ha(Js) sqrt((2/105)/fr) x {np.exp(lrs):.3f}   "
      f"(lrs={lrs:+.3f}, prior sigma {AKULOV_SIG})")
if LRS_PER_REGIME:
    print("   per regime: " + "  ".join(f"{np.exp(v):.3f}"
                                         for v in get(p, "lrs")))
if LRS_BC:
    _bR = get(p, "bR")[0]
    print(f"   carbon slope bR = {_bR:+.3f}: level "
          + "  ".join(f"C {c_:.2f} -> {np.exp(lrs + _bR * c_ / 0.77):.3f}"
                      for c_ in (0.0, 0.01, 0.10, 0.20, 0.30)))
for _g, _c, _mn in (("ARMCO gr.2", 0.010, 0.100), ("1010", 0.100, 0.45),
                    ("1030", 0.300, 0.75)):
    _js = L.js_anchor(_c, _mn)
    print(f"   {_g:11s} Js={_js:.4f} T  Ha={2*L.K1/_js:6.0f}  "
          f"hR={2*L.K1/_js*np.sqrt((2/105)/fr)*np.exp(lrs):6.0f} A/m")
_lt = np.exp(get(p, "ltail")) if "ltail" in IX else np.array([1.0, 1.0])
print("wall tail kW*mW = " + (f"{_lt[0]:.3f} (global)" if len(_lt) == 1 else
      f"as-rolled {_lt[0]:.3f}, annealed {_lt[1]:.3f}"))
print(f"knee width kW at C=0: as-rolled {np.exp(get(p, 'lkW')[0]):.3f}, "
      f"annealed {np.exp(get(p, 'lkW')[1]):.3f};  "
      f"bK = {get(p, 'bK')[0]:+.3f}, {get(p, 'bK')[1]:+.3f}")
if MODEL == "rb":
    print(f"toe scale hi/hW = as-rolled {np.exp(get(p, 'lqt')[0]):.3f}, "
          f"annealed {np.exp(get(p, 'lqt')[1]):.3f}")
elif MODEL == "two":
    _fS = 1 / (1 + np.exp(-get(p, "lfS")[0]))
    print(f"soft share fS at C=0 = {_fS:.3f}, bFS = {get(p, 'bFS')[0]:+.3f};"
          f"  hS/hW = {np.exp(get(p, 'lqS')[0]):.3f} / "
          f"{np.exp(get(p, 'lqS')[1]):.3f}")
elif MODEL == "splicew":
    print(f"join position in window q = {1/(1+np.exp(-get(p, 'lqw')))}")
elif MODEL == "splicemu":
    print(f"toe mu_1 mu0 h_W/Js at C=0: as-rolled {np.exp(get(p, 'lm')[0]):.4f}, "
          f"annealed {np.exp(get(p, 'lm')[1]):.4f};  bM = {get(p, 'bM')}")
elif MODEL == "splice":
    print(f"join H2/h_W at C=0: as-rolled {np.exp(get(p, 'lq')[0]):.3f}, "
          f"annealed {np.exp(get(p, 'lq')[1]):.3f}")
elif MODEL == "tied":
    _cr = 1.0 + np.exp(get(p, "lcr"))
    print(f"toe curvature cr = as-rolled {_cr[0]:.2f}, annealed "
          f"{_cr[1]:.2f};  h_t = h_W/{MDL.toe_peak_u(_cr[1]):.4f} "
          f"= {1/MDL.toe_peak_u(_cr[1]):.2f} h_W (annealed)")
else:
    print(f"carbon slopes: bI = {get(p, 'bI')}  bW = {get(p, 'bW')}")
    print(f"toe curvature cr = {np.exp(get(p, 'lc'))}")
condname = {2: "as hot rolled", 3: "annealed 1300F", 4: "annealed 1400F",
            5: "annealed 1500F"}
par = pdict(p)
for d in conds:
    print(f"cond {d} ({condname[d]}):")
    for c in (0.04, 0.10, 0.20, 0.30):
        fi, hi, hW, kW, mW = MDL.wall_fit(par, ci, c, d)
        if MODEL == "tied":
            hi = hW / MDL.toe_peak_u(1.0 + np.exp(get(p, "lcr")[
                MDL.regime(d)]))
        if MODEL in ("rb", "two"):
            _js = L.js_anchor(c, 0.45)
            _sh = SHAPE_FN(par, ci, c, d, np.array([1e-4, 10.0]),
                                    _js)
            _mu1 = _js * _sh[0] / 1e-4 / L.MU0
            print(f"   C={c:.2f}: fi={fi:.3f} hW={hW:6.1f} kW={kW:.3f} "
                  f"mu1={_mu1:7.0f}")
            continue
        if SPLICED:
            _js = L.js_anchor(c, 0.45)
            if MODEL == "splicemu":
                _a, _b, _H2, _s = MDL.splice_join_mu(par, ci, c, d, _js)
            elif MODEL == "splicew":
                _a, _b, _H2 = MDL.splice_join_window(par, ci, c, d, _js)
            else:
                _a, _b, _H2 = MDL.splice_rayleigh(par, ci, c, d, _js)
            _js = L.js_anchor(c, 0.45)
            print(f"   C={c:.2f}: H2={_H2:6.1f} mu1={_a*_js/L.MU0:7.0f} "
                  f"nu={_b*_js/L.MU0:8.3f}/(A/m)  hW={hW:6.1f} "
                  f"kW={kW:.3f} mW={mW:.3f}")
            continue
        print(f"   C={c:.2f}: fi={fi:.3f} hi={hi:6.1f}  hW={hW:6.1f} "
              f"kW={kW:.3f} mW={mW:.3f}")

print("\n================ per-curve fit quality ================")
tot = []
for i, cu in enumerate(curves):
    fn = lambda h, i=i, cu=cu: jmodel(p, cu, i, h)
    r = L.lnh_residuals(cu, fn)
    sel = (cu.j > 0.5) & (cu.j < 2.0)
    rknee = r[sel] if sel.any() else r
    if not cu.excluded:
        tot.extend(r)
    print(f"  {cu.name:34s} dJs={get(p,'dJs')[i]:+.4f} "
          f"dlnH={get(p,'dlh')[i]:+.4f}  RMS lnH={L.rms(r):.3f} "
          f"(0.5-2.0T: {L.rms(rknee):.3f})"
          + ("  [excluded from fit]" if cu.excluded else ""))
print(f"TOTAL RMS lnH = {L.rms(tot):.4f} over fitted curves  (v10: 0.1117)")

trusted = [i for i, cu in enumerate(curves) if cu.trust]
# The per-curve offsets are a fitting device: each scan needs its own
# level so that a level error does not contaminate the shape.  They are
# NOT averaged into the emitted curves.  Their mean used to be, and it
# averaged a two-cluster population -- the SLAC pair sits ~2% low, every
# trusted U.S. Steel curve within 3 mT of its anchor -- so a 0.42% shift
# rode on two curves from one laboratory.  The emitted level is Eq. (5).
_off = get(p, "dJs")[trusted]
_uss = [1000 * get(p, "dJs")[i] for i in trusted
        if not curves[i].name.startswith("SLAC")]
djsbar = 0.0
print(f"\nper-curve level offsets over the {len(trusted)} trusted curves:")
print(f"  mean {1000*float(np.mean(_off)):+.2f} mT   "
      f"median {1000*float(np.median(_off)):+.2f} mT   "
      f"range {1000*float(np.min(_off)):+.1f} to {1000*float(np.max(_off)):+.1f}")
print(f"  excluding the two SLAC curves: mean {float(np.mean(_uss)):+.2f} mT "
      f"over {len(_uss)} curves, i.e. Eq. (5) predicts the U.S. Steel levels "
      f"to about {float(np.max(np.abs(_uss))):.0f} mT")
print("  NOT applied to the emitted curves: the emitted level is Eq. (5).")

# ------------------------------------------------------------ save params
out = {"description": "bh_fit: two-regime shared-shape family",
       "model": MODEL,
       "variant": dict(TAIL_PER_REGIME=TAIL_PER_REGIME,
                       BW_PER_REGIME=BW_PER_REGIME, AKULOV_SIG=AKULOV_SIG,
                       BW_SIG=BW_SIG, EXCLUDE=EXCLUDE, MODEL=MODEL,
                       BMIN_KG=L.BMIN_KG, SIG_T=SIG_T, KW_SIG=KW_SIG,
                       KW0=KW0, BK_SIG=BK_SIG, BK0=BK0, TAIL_SIG=TAIL_SIG,
                       TAIL0=TAIL0, LQ_MIN=LQ_MIN, RAY_ANCHOR=RAY_ANCHOR,
                       RAY_CONDS=RAY_CONDS, TOE=TOE),
       "djsbar": djsbar,      # 0: the emitted level is Eq. (5) alone
       "level_offsets_mT": {"trusted_mean": 1000 * float(np.mean(_off)),
                            "trusted_median": 1000 * float(np.median(_off)),
                            "uss_only_mean": float(np.mean(_uss))},
       "conds": conds,
       "params": {k: list(map(float, get(p, k))) for k in IX}}
with open(PARAMS, "w") as f:
    json.dump(out, f, indent=1)
print(f"wrote {PARAMS}")

# ------------------------------------------------------------ family
F = MDL.Family(PARAMS)

grades = [("ArmcoFe2", 0.010, 0.100),  # AK Steel bulletin Gr.2 maxima
          ("1004", 0.040, 0.35), ("1006", 0.060, 0.325),
          ("1008", 0.080, 0.40), ("1010", 0.100, 0.45),
          ("1018", 0.175, 0.75),
          ("1117", 0.180, 1.15),       # resulfurized; Mn as level only
          ("1020", 0.200, 0.45), ("1030", 0.300, 0.75)]
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])


gate = L.gate      # shared with bh_fit_si.py; see bhlib.gate


print(f"\n================ family emitter -> {CSVDIR}/ ================")
nfail = 0
for gname, c, mn in grades:
    for d in conds:
        b = F.b(c, mn, d, HGRID)
        dmin, turns, murat = gate(b, HGRID)
        ok = (dmin >= 0.999 and turns <= 1 and murat < 1.0
              and (np.diff(b) > 0).all())
        nfail += not ok
        fn = f"{CSVDIR}/femm_{gname}_{condname[d].replace(' ', '_')}.csv"
        np.savetxt(fn, np.column_stack([b, HGRID]), fmt="%.9f",
                   delimiter=",")
        print(f"  {gname:9s} cond {d}: Js={L.js_anchor(c, mn)+djsbar:.4f}  "
              f"B(4000 Oe)={F.b(c, mn, d, 4000*L.OE):.4f}  "
              f"min dB/dH/mu0={dmin:.2f}  turns={turns}  "
              f"mu_i/mu_max={murat:.2f}"
              + ("" if ok else "  <== GATE FAIL"))
print(f"  {len(grades)*len(conds)} tables, {nfail} gate failures")

# ------------------------------------------------- hold-out validation
print("\n================ hold-out: Fermilab TM-1197 (as received) ========")
ho_res = []
for hv in L.load_holdout():
    pred = F.j(hv["c"], hv["mn"], 2, hv["h"])
    dj = (F.j(hv["c"], hv["mn"], 2, hv["h"] * 1.001) - pred) / 0.001
    r = (hv["j"] - pred) / dj
    ho_res.append((hv, r))
    knee = hv["j"] > 1.35
    print(f"  {hv['name']} vs cond-2 family: RMS lnH above knee (J>1.35 T) "
          f"= {L.rms(r[knee]):.3f}")


# ---------------------------------------- Mathematica 9 emitter (no fit)
def mma_list(v, fmt="%.6f"):
    return "{" + ", ".join(fmt % x for x in v) + "}"


if MMA and MODEL == "two":
    reg = [MDL.regime(d, NREG) for d in conds]
    bW4 = [get(p, "bW")[r if BW_PER_REGIME else j] for j, r in enumerate(reg)]
    lkW4 = [get(p, "lkW")[r] for r in reg]
    bK4 = [get(p, "bK")[r] for r in reg]
    lt = get(p, "ltail")
    lt4 = [lt[r] if len(lt) > 1 else lt[0] for r in reg]
    lqS4 = [get(p, "lqS")[r] for r in reg]
    mma = f"""(* ::Package:: *)

(* bh_model_fit.m -- GENERATED by bh_fit.py; Mathematica 9 compatible.
   Closed-form B-H model for low-carbon steel, no fitting needed.
   J = Js ( fi h/(h+hS) + fS (1 - 1/Sqrt[1+(h/hS)^2])
            + (1-fi-fS-fr) Burr[h,hW,kW,mW] + fr Burr[h,hR,kR,mR] )
   with hS = hW Exp[lqS] and hR = 2 K1/Js Sqrt[(2/105)/fr] Exp[lrs].
   B = J + mu0 H.  Conditions: 2 as-rolled | 3 ann 1300F | 4 ann 1400F |
   5 ann 1500F.  x = C/0.77 carbon coordinate.  Per-regime parameters of
   the Python model are expanded to one entry per condition;
   mW = Exp[ltail]/kW. *)

mu0 = 4 Pi 10.^-7; oe = 1000./(4 Pi); kG = 0.1;  K1 = {L.K1};
(* Eq. (5): one attenuation coefficient per alloying element. *)
kC = {L.K_C:.6f};  kMn = {L.K_MN};
JsAnchor[c_, mn_] := {L.JS_FE} - kC c - kMn mn;

condPos[d_] := d - 1;  (* conditions 2..5 -> positions 1..4 *)
lfi = {get(p, 'lfi')[0]:.6f};  bFI = {get(p, 'bFI')[0]:.6f};
lfS = {get(p, 'lfS')[0]:.6f};  bFS = {get(p, 'bFS')[0]:.6f};
lhWV = {mma_list(get(p, 'lhW'))};  bWV = {mma_list(bW4)};
lkWV = {mma_list(lkW4)};  bKV = {mma_list(bK4)};
ltailV = {mma_list(lt4)};  lqSV = {mma_list(lqS4)};
fr = {fr:.6f};  kR = {kR:.6f};  mR = {mR:.6f};  lrs = {lrs:.6f};

burr[h_, a_, k_, m_] := 1 - (1 + (h/a)^k)^-m;
soft[h_, a_] := 1 - 1/Sqrt[1 + (h/a)^2];
toe[h_, a_] := {'(h/a)/Sqrt[1 + (h/a)^2]' if TOE == 'straight' else 'h/(h + a)'};

shapeS[c_, mn_, d_, h_] := Module[{{x = c/0.77, j = condPos[d], fi, fS, hW,
    kW, mW, hS, hR}},
   hR = 2 K1/JsAnchor[c, mn] Sqrt[(2/105.)/fr] Exp[lrs];
   fi = 1/(1 + Exp[-(lfi + bFI x)]);
   fS = 1/(1 + Exp[-(lfS + bFS x)]);
   hW = Exp[lhWV[[j]] + bWV[[j]] x];
   kW = Exp[lkWV[[j]] + bKV[[j]] x];
   mW = Exp[ltailV[[j]]]/kW;
   hS = hW Exp[lqSV[[j]]];
   fi toe[h, hS] + fS soft[h, hS] + (1 - fi - fS - fr) burr[h, hW, kW, mW] +
     fr burr[h, hR, kR, mR]];

(* H in A/m -> B in tesla *)
bOfH[c_, mn_, d_, h_] := JsAnchor[c, mn] shapeS[c, mn, d, h] + mu0 h;

grades = {{{{"1004", 0.040, 0.35}}, {{"1006", 0.060, 0.325}},
   {{"1008", 0.080, 0.40}}, {{"1010", 0.100, 0.45}},
   {{"1018", 0.175, 0.75}}, {{"1020", 0.200, 0.45}},
   {{"1030", 0.300, 0.75}}}};

(* example: plot the annealed-1400F family, H in Oe, B in kG *)
LogLinearPlot[
 Evaluate[Table[bOfH[g[[2]], g[[3]], 4, oe hOe]/kG, {{g, grades}}]],
 {{hOe, 0.3, 5000}}, PlotRange -> {{0, 24}}, Frame -> True,
 FrameLabel -> {{"H [Oe]", "B [kG]"}}, PlotLegends -> grades[[All, 1]]]

(* example: emit a FEMM table (B tesla, H A/m) for 1010 annealed 1400F *)
femmTable[c_, mn_, d_, np_: 61] := Module[{{hs}},
  hs = Join[{{0.}}, 10^Range[1., Log[10., 4. 10^5], (Log[10., 4. 10^5] -
        1.)/(np - 2)]];
  Transpose[{{bOfH[c, mn, d, #] & /@ hs, hs}}]];
(* Export["femm_1010_annealed_1400F.csv", femmTable[0.10, 0.45, 4]] *)
"""
    with open(MMA, "w") as f:
        f.write(mma)
    print(f"wrote {MMA} (closed-form Mathematica 9 emitter)")

if MMA and MODEL == "splice":
    reg = [MDL.regime(d, NREG) for d in conds]
    bW4 = [get(p, "bW")[r if BW_PER_REGIME else j] for j, r in enumerate(reg)]
    lkW4 = [get(p, "lkW")[r] for r in reg]
    bK4 = [get(p, "bK")[r] for r in reg]
    lt = get(p, "ltail")
    lt4 = [lt[r] if len(lt) > 1 else lt[0] for r in reg]
    lq4 = [get(p, "lq")[r] for r in reg]
    mma = f"""(* ::Package:: *)

(* bh_model_fit.m -- GENERATED by bh_fit.py; Mathematica 9 compatible.
   Closed-form B-H model for low-carbon steel, no fitting needed.
   Below the join H2 the polarization is Rayleigh's, J = Js (a h + b h^2);
   above it J = Js ((1-fr) Burr[h,hW,kW,mW] + fr Burr[h,hR,kR,mR]), with
   hR = 2 K1/Js Sqrt[(2/105)/fr] Exp[lrs].  a and b follow from
   continuity of J and dJ/dh at H2 = hW Exp[lq].  B = J + mu0 H.
   Conditions: 2 as-rolled | 3 ann 1300F | 4 ann 1400F | 5 ann 1500F.
   x = C/0.77 carbon coordinate.  Per-regime parameters of the Python
   model are expanded to one entry per condition; mW = Exp[ltail]/kW. *)

mu0 = 4 Pi 10.^-7; oe = 1000./(4 Pi); kG = 0.1;  K1 = {L.K1};
(* Eq. (5): one attenuation coefficient per alloying element. *)
kC = {L.K_C:.6f};  kMn = {L.K_MN};
JsAnchor[c_, mn_] := {L.JS_FE} - kC c - kMn mn;

condPos[d_] := d - 1;  (* conditions 2..5 -> positions 1..4 *)
lhWV = {mma_list(get(p, 'lhW'))};  bWV = {mma_list(bW4)};
lkWV = {mma_list(lkW4)};  bKV = {mma_list(bK4)};
ltailV = {mma_list(lt4)};  lqV = {mma_list(lq4)};
fr = {fr:.6f};  kR = {kR:.6f};  mR = {mR:.6f};  lrs = {lrs:.6f};

burr[h_, a_, k_, m_] := 1 - (1 + (h/a)^k)^-m;
burrD[h_, a_, k_, m_] := m k (h/a)^k/h (1 + (h/a)^k)^(-m - 1);

(* the wall + rotation core and its derivative, then the C1 join *)
shapeS[c_, mn_, d_, h_] := Module[{{x = c/0.77, j = condPos[d], hW, kW, mW,
    hR, h2, core, coreD, f2, s2, a, b}},
   hR = 2 K1/JsAnchor[c, mn] Sqrt[(2/105.)/fr] Exp[lrs];
   hW = Exp[lhWV[[j]] + bWV[[j]] x];
   kW = Exp[lkWV[[j]] + bKV[[j]] x];
   mW = Exp[ltailV[[j]]]/kW;
   h2 = hW Exp[lqV[[j]]];
   core[hh_] := (1 - fr) burr[hh, hW, kW, mW] + fr burr[hh, hR, kR, mR];
   coreD[hh_] := (1 - fr) burrD[hh, hW, kW, mW] + fr burrD[hh, hR, kR, mR];
   f2 = core[h2]; s2 = coreD[h2];
   a = 2 f2/h2 - s2;  b = (s2 h2 - f2)/h2^2;
   If[h < h2, a h + b h^2, core[h]]];

(* H in A/m -> B in tesla *)
bOfH[c_, mn_, d_, h_] := JsAnchor[c, mn] shapeS[c, mn, d, h] + mu0 h;

grades = {{{{"1004", 0.040, 0.35}}, {{"1006", 0.060, 0.325}},
   {{"1008", 0.080, 0.40}}, {{"1010", 0.100, 0.45}},
   {{"1018", 0.175, 0.75}}, {{"1020", 0.200, 0.45}},
   {{"1030", 0.300, 0.75}}}};

(* example: plot the annealed-1400F family, H in Oe, B in kG *)
LogLinearPlot[
 Evaluate[Table[bOfH[g[[2]], g[[3]], 4, oe hOe]/kG, {{g, grades}}]],
 {{hOe, 0.3, 5000}}, PlotRange -> {{0, 24}}, Frame -> True,
 FrameLabel -> {{"H [Oe]", "B [kG]"}}, PlotLegends -> grades[[All, 1]]]

(* example: emit a FEMM table (B tesla, H A/m) for 1010 annealed 1400F *)
femmTable[c_, mn_, d_, np_: 61] := Module[{{hs}},
  hs = Join[{{0.}}, 10^Range[1., Log[10., 4. 10^5], (Log[10., 4. 10^5] -
        1.)/(np - 2)]];
  Transpose[{{bOfH[c, mn, d, #] & /@ hs, hs}}]];
(* Export["femm_1010_annealed_1400F.csv", femmTable[0.10, 0.45, 4]] *)
"""
    with open(MMA, "w") as f:
        f.write(mma)
    print(f"wrote {MMA} (closed-form Mathematica 9 emitter)")

if MMA and MODEL == "fit":
    # expand the per-regime vectors to per-condition, so the .m stays
    # a flat table lookup with no regime logic
    reg = [MDL.regime(d, NREG) for d in conds]
    bI4 = [get(p, "bI")[r] for r in reg]
    bW4 = [get(p, "bW")[r if BW_PER_REGIME else j] for j, r in enumerate(reg)]
    lkW4 = [get(p, "lkW")[r] for r in reg]
    bK4 = [get(p, "bK")[r] for r in reg]
    lt = get(p, "ltail")
    lt4 = [lt[r] if len(lt) > 1 else lt[0] for r in reg]
    mma = f"""(* ::Package:: *)

(* bh_model_fit.m -- GENERATED by bh_fit.py; Mathematica 9 compatible.
   Closed-form unified B-H model for low-carbon steel, no fitting needed.
   J(H) = Js*(fi*H/(H+hi) + (1-fi-fr)*Burr[H,hW,kW,mW] + fr*Burr[H,hR,kR,mR])
   with hR = 2 K1/Js Sqrt[(2/105)/fr] Exp[lrs], so the rotation scale
   follows the composition rather than being a single fitted number.
   B = J + mu0 H.  Conditions: 2 as-rolled | 3 ann 1300F | 4 ann 1400F |
   5 ann 1500F.  x = C/0.77 carbon coordinate.  The per-regime parameters
   of the Python model are expanded here to one entry per condition;
   mW is not free but ltail - ln kW (tail exponent kW*mW per regime). *)

mu0 = 4 Pi 10.^-7; oe = 1000./(4 Pi); kG = 0.1;  K1 = {L.K1};
(* Eq. (5): one attenuation coefficient per alloying element. *)
kC = {L.K_C:.6f};  kMn = {L.K_MN};
JsAnchor[c_, mn_] := {L.JS_FE} - kC c - kMn mn;
dJsBar = {djsbar:.6f};

condPos[d_] := d - 1;  (* conditions 2..5 -> positions 1..4 *)
lfi = {get(p, 'lfi')[0]:.6f};  bFI = {get(p, 'bFI')[0]:.6f};
lhiV = {mma_list(get(p, 'lhi'))};  bIV = {mma_list(bI4)};
lhWV = {mma_list(get(p, 'lhW'))};  bWV = {mma_list(bW4)};
lkWV = {mma_list(lkW4)};  bKV = {mma_list(bK4)};
ltailV = {mma_list(lt4)};
fr = {fr:.6f};  kR = {kR:.6f};  mR = {mR:.6f};  lrs = {lrs:.6f};

burr[h_, a_, k_, m_] := 1 - (1 + (h/a)^k)^-m;
(* hR is not a constant: it follows each composition's own anisotropy
   field Ha = 2 K1 / Js, with only the level Exp[lrs] fitted. *)
shapeS[c_, mn_, d_, h_] := Module[{{x = c/0.77, j = condPos[d], fi, hi, hW,
    kW, mW, hR}},
   hR = 2 K1/JsAnchor[c, mn] Sqrt[(2/105.)/fr] Exp[lrs];
   fi = 1/(1 + Exp[-(lfi + bFI x)]);
   hi = Exp[lhiV[[j]] + bIV[[j]] x];
   hW = Exp[lhWV[[j]] + bWV[[j]] x];
   kW = Exp[lkWV[[j]] + bKV[[j]] x];
   mW = Exp[ltailV[[j]]]/kW;
   fi h/(h + hi) + (1 - fi - fr) burr[h, hW, kW, mW] +
     fr burr[h, hR, kR, mR]];

(* H in A/m -> B in tesla *)
bOfH[c_, mn_, d_, h_] := (JsAnchor[c, mn] + dJsBar) shapeS[c, mn, d, h] + mu0 h;

grades = {{{{"1004", 0.040, 0.35}}, {{"1006", 0.060, 0.325}},
   {{"1008", 0.080, 0.40}}, {{"1010", 0.100, 0.45}},
   {{"1018", 0.175, 0.75}}, {{"1020", 0.200, 0.45}},
   {{"1030", 0.300, 0.75}}}};

(* example: plot the annealed-1400F family, H in Oe, B in kG *)
LogLinearPlot[
 Evaluate[Table[bOfH[g[[2]], g[[3]], 4, oe hOe]/kG, {{g, grades}}]],
 {{hOe, 0.3, 5000}}, PlotRange -> {{0, 24}}, Frame -> True,
 FrameLabel -> {{"H [Oe]", "B [kG]"}}, PlotLegends -> grades[[All, 1]]]

(* example: emit a FEMM table (B tesla, H A/m) for 1010 annealed 1400F *)
femmTable[c_, mn_, d_, np_: 61] := Module[{{hs}},
  hs = Join[{{0.}}, 10^Range[1., Log[10., 4. 10^5], (Log[10., 4. 10^5] -
        1.)/(np - 2)]];
  Transpose[{{bOfH[c, mn, d, #] & /@ hs, hs}}]];
(* Export["femm_1010_annealed_1400F.csv", femmTable[0.10, 0.45, 4]] *)
"""
    with open(MMA, "w") as f:
        f.write(mma)
    print(f"wrote {MMA} (closed-form Mathematica 9 emitter)")

# ------------------------------------------------------------ plots
hgrid = np.logspace(1, np.log10(4.5e5), 500)
_nr = int(np.ceil(len(curves) / 4))    # 14 curves, or 18 with Cheney
fig, axes = plt.subplots(_nr, 4, figsize=(20, 4 * _nr), sharex=True)
for i, cu in enumerate(curves):
    ax = axes.flat[i]
    ax.semilogx(cu.h, cu.j, "o", ms=4)
    ax.semilogx(hgrid, jmodel(p, cu, i, hgrid), "-")
    ax.set_title(f"{cu.name} (cond {cu.cond})", fontsize=9)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_ylim(0, 2.3)
for ax in axes.flat[n:]:
    ax.axis("off")
fig.suptitle("bh_fit: per-curve fits (with per-curve level and "
             "field-scale nuisances)", fontsize=14)
fig.supxlabel("H [A/m]"); fig.supylabel("J [T]")
fig.tight_layout()
fig.savefig(f"{OUT}/{PFX}percurve.png", dpi=110)

fig, axes = plt.subplots(1, 2, figsize=(16, 6.5))
CC = {2: "#7f7f7f", 3: "#1f77b4", 4: "#d62728", 5: "#2ca02c"}
hh = np.logspace(np.log10(0.2 * L.OE), np.log10(5e3 * L.OE), 400)
for ax, (g, c, mn) in zip(axes, [("1010", 0.10, 0.45), ("1030", 0.30, 0.75)]):
    for d in conds:
        ax.semilogx(hh / L.OE, F.b(c, mn, d, hh) / L.KG, "-", color=CC[d],
                    lw=1.8, label=condname[d])
    for cu in curves:
        if abs(cu.c - c) < 1e-6 and "toe" not in cu.name:
            ax.semilogx(cu.h / L.OE, (cu.j + L.MU0 * cu.h) / L.KG,
                        "o" if cu.name.startswith("SLAC") else "s",
                        ms=3.5, color=CC[cu.cond], alpha=0.7,
                        label="_nolegend_")
    ax.set_title(f"{g}: four conditions (markers: scans)")
    ax.set_xlabel("H [Oe]"); ax.set_ylabel("B [kG]")
    ax.set_xlim(0.2, 5000); ax.set_ylim(0, 24)
    ax.grid(True, which="both", alpha=0.3); ax.legend(fontsize=8)
fig.tight_layout()
fig.savefig(f"{OUT}/{PFX}conditions.png", dpi=110)
print(f"\nwrote {OUT}/{PFX}percurve.png, {OUT}/{PFX}conditions.png")
