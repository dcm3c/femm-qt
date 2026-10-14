# SPDX-License-Identifier: MIT
"""bh_fit_si.py -- the silicon-steel family, regressed on the low-carbon machinery.

Ten grades from the US Steel 1978 catalog (silib.GRADES), one digitized DC
magnetization curve each, one shared FOUR-process form (simodel.py): a
reversible toe (the low-carbon straight toe, its scale a fitted ratio to
the wall pinning field), a Burr-XII knee, a hard wall population carrying
the shoulder, and an Akulov-constrained rotation term.
simodel.py records why the fourth process was necessary and what was tried
before adding it.

WHAT THE FIT IS FOR.  si_measured.py already turns each page into a table.
The point of regressing them together is consistency: one rotation term,
one knee shape per product form, one tail exponent, and a composition law
for the pinning scales.  The family then says something the ten separate
pages cannot -- what a silicon content it was never shown should look like
-- and it says it with the extrapolation above the chart's 989 Oe edge
governed by the same rotation term for all ten, instead of ten independent
Akulov splices.

LEVEL comes from the book's resistivity (silib.js), with a per-grade dJs
nuisance at sigma 0.02 T.  It is NOT read off the chart: the pages stop at
1.8-2.4 Ha and si_composition.py shows them running 0.4-2.7% low there.

FIELD SCALE has no nuisance parameter, unlike the low-carbon fit.  One book, one set of
axes, calibrated to about 2 px rms; there is nothing for it to absorb but
model error.

PRIORS, all of them stated:
  lrs      ~ N(0, AKULOV_SIG)  the one free scale on the rotation term,
                               around Ha(Si) sqrt((2/105)/fr)
  kR mR    ~ N(2, SIG_T)       Akulov tail exponent, as low-carbon (0.05)
  kW mW    ~ N(2, TAIL_SIG)    wall tail exponent, as low-carbon (0.3)
  eS, eW   ~ N(0, SIG_E)       per-grade shoulder and wall pinning
                               residuals -- the processing axis the book
                               does not describe
  dJs      ~ N(0, 0.02)        per-grade level of the PAGE (not emitted;
                               see simodel.SiFamily.js)
  bK       ~ N(0, 0.5)         silicon slope of the knee width
  bFT, bFS ~ N(0, 0.5)         silicon slopes of the toe and shoulder shares

Variant knobs are environment variables, as in the low-carbon fit; TAG keeps a variant's
outputs out of the default paths.  HMIN, SIG_EI and SIG_EK exist only to
reproduce the experiments recorded in simodel.py and default to off.

  TAG=   AKULOV_SIG=0.10  SIG_E=0.30  EXCLUDE=  DECIM=15
  TOE=straight  TOE_SCALE=1  SHOULDER=rational  TAIL_SIG=0.3  (the model
  form; see the block above the knob definitions)
  HMIN=0 (A/m floor)  SIG_EI=0 (0 = share SIG_E)  SIG_EK=0 (0 = no per-
  grade knee width)

Outputs (TAG unset):
  out/si_params.json          fitted parameters, read by everything else
  femm_si_<grade>.csv         10 tables, B tesla / H A/m
  out/si_percurve.png, out/si_law.png
"""
import json
import os
import numpy as np
from scipy.optimize import least_squares
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
import silib as S
import simodel as M
import femm_table as T


class _HJ:
    """Minimal stand-in for bhlib.Curve, which is what lnh_residuals wants."""

    def __init__(self, h, j):
        self.h, self.j = h, j

# ---------------------------------------------------------------- variant
AKULOV_SIG = float(os.environ.get("AKULOV_SIG", "0.10"))
SIG_E = float(os.environ.get("SIG_E", "0.30"))
DECIM = int(os.environ.get("DECIM", "15"))
HMIN = float(os.environ.get("HMIN", "0"))     # A/m; drop the chart's bottom
SIG_EI = float(os.environ.get("SIG_EI", "0"))  # 0 = use SIG_E for eS too
SIG_EK = float(os.environ.get("SIG_EK", "0"))  # 0 = no per-grade knee width
TAG = os.environ.get("TAG", "")
# Model form (simodel.shape_si), as adopted 2026-09-27:
#   TOE=straight     toe u/sqrt(1+u^2), as in the low-carbon fit
#   TOE_SCALE=1      its scale a fitted global ratio to hW, ht = hW exp(lqT),
#                    as the low-carbon toe's scale hS is a fitted ratio to hW
#   SHOULDER=rational  H/(H+hs): the pages want its linear onset and 1/H
#                    tail; the soft-wall and straight kernels fit worse
#   TAIL_SIG=0.3     the low-carbon prior on the wall tail, kW*mW ~ 2
#   SI_SPLIT=1       everything wall-related per test condition (as sheared,
#                    decarb annealed); rotation and composition laws shared
#   SOFT=1           the low-carbon soft-wall population, H^2 onset, per
#                    test condition
# Fourteen pages (2026-09-27): RMS lnH 0.073, six starts agreeing, all 14
# tables through the shared gate, rotation 1.006 x the anisotropy value.
# 2026-10-03, Js base less the example C and Mn (silib.JS_BASE): RMS 0.0729,
# rotation 1.031, tail 2.25; with AKULOV_SIG=1 it drifts to 1.165 and 2.51 for
# RMS 0.0723, so the level is held by the prior, not measured by the pages.
# Then SIG_T 0.2 -> 0.05 (the low-carbon value): RMS 0.0722, 5/6 starts,
# rotation 0.999, tail 2.003, 14/14 gates.
# Fitted alone the five annealed pages pull the rotation 26% off, and with
# one set of wall parameters for both conditions RMS is 0.088.  The annealed
# toe share falls to 0.001: annealed material takes the low-carbon structure
# (soft population, no toe), and lft/bFT/lqT there are unconstrained.
# A per-grade knee width (SIG_EK) cannot be combined with SOFT stably and
# does not reach the gauge effect (the .024 in pages' rounder knees).
# The ten-page fit that preceded this (RMS 0.061) is master at 7e30f9f; the
# 2026-09-22 fit, whose rational toe failed 5 of 10 tables under the shared
# gate, is at 8011b87.
TOE = os.environ.get("TOE", "straight")
TOE_SCALE = os.environ.get("TOE_SCALE", "1") == "1"
TAIL_SIG = float(os.environ.get("TAIL_SIG", "0.3"))   # 0 = wall tail free
SHOULDER = os.environ.get("SHOULDER", "rational")
# SI_GROUPS=form: shape parameters per test condition (as sheared, decarb
# annealed).  SI_GROUPS=section: as-sheared lamination sheet (Stator B fully
# processed) gets its own set, apart from the M-grades (silib.regime).
GROUPS = os.environ.get("SI_GROUPS", "form")   # not GROUPS: bash reserves it
NREG = 3 if GROUPS == "section" else 2
# SI_SPLIT=1: everything wall-related -- toe share, slope and scale ratio,
# shoulder share and slope, and the silicon slopes of the pinning scales and
# knee width -- per regime; only the rotation term and the composition laws
# stay shared.  Tests whether as-sheared and decarb-annealed material are
# one family differing in pinning, or two.
SPLIT = os.environ.get("SI_SPLIT", "1") == "1"
SOFT = os.environ.get("SOFT", "1") == "1"
NW = NREG if SPLIT else 1
OUT = f"out/si_{TAG}" if TAG else "out"
PARAMS = f"{OUT}/si_params.json" if TAG else "out/si_params.json"
CSVDIR = OUT if TAG else "."
PFX = "" if TAG else "si_"
os.makedirs(OUT, exist_ok=True)
EXCLUDE = [s.strip() for s in os.environ.get("EXCLUDE", "").split(";")
           if s.strip()]
print(f"variant: AKULOV_SIG={AKULOV_SIG} SIG_E={SIG_E} "
      f"SIG_EI={SIG_EI or SIG_E} HMIN={HMIN} DECIM={DECIM} "
      f"EXCLUDE={EXCLUDE} TOE={TOE} TOE_SCALE={TOE_SCALE} "
      f"SHOULDER={SHOULDER} TAIL_SIG={TAIL_SIG} -> {PARAMS}")

SIG_BK, SIG_BFI = 0.5, 0.5
SIG_JS = float(os.environ.get("LEVEL_SIG", "0.02"))   # per-page level prior, T
# Akulov tail, kR*mR = 2 +/- SIG_T.  0.05 since 2026-10-03, the low-carbon
# fit's value: at 0.2 the level and the exponent slid along a ridge together
# (freed level 1.17, exponent 2.51); held at 0.05 both families treat the
# rotation term identically.
SIG_T = float(os.environ.get("SIG_T", "0.05"))
LRS0 = float(os.environ.get("LRS0", "0"))       # centre of the level prior (ln)

# ------------------------------------------------------------------- data
rows = S.table()
curves = []
for i, r in enumerate(rows):
    h, b, _ = S.load_raw(r["key"])
    h, b = h[::DECIM], b[::DECIM]
    if HMIN > 0:
        k = h >= HMIN
        h, b = h[k], b[k]
    j = b - L.MU0 * h
    slope = np.gradient(j, np.log(h))
    cu = dict(r)
    cu["regime"] = S.regime(r, GROUPS)
    cu.update(i=i, h=h, j=j, slope=slope,
              w=1.0 / (slope ** 2 + (0.05 * slope.max()) ** 2),
              excluded=r["name"] in EXCLUDE)
    if cu["excluded"]:
        cu["w"] = cu["w"] * 0.0
        print(f"excluded from fit: {r['name']}")
    curves.append(cu)
n = len(curves)
print(f"{n} curves, {len(curves[0]['h'])} points each after decimation "
      f"by {DECIM} ({curves[0]['h'][0]:.1f} to {curves[0]['h'][-1]:.0f} A/m)")

# ----------------------------------------------------------------- layout
IX, _p0 = {}, []


def _add(name, vals):
    IX[name] = (len(_p0), len(_p0) + len(vals))
    _p0.extend(vals)


_add("lfr", [np.log(0.18 / 0.82)])
_add("rot", [np.log(1.88), np.log(1.06)])       # kR, mR;  hR is not free
_add("lrs", [0.0])                              # ln multiplier on Akulov hR
_add("lft", [np.log(0.05 / 0.95)] * NW)       # reversible toe share; its scale
_add("bFT", [0.0] * NW)                      # is hW times one global ratio,
if TOE_SCALE:                           # ht = hW exp(lqT) (else ht = hW)
    _add("lqT", [0.0] * (NW if os.environ.get("SPLIT_Q", "1") == "1" else 1))
if TOE in ("curved", "scurved"):        # toe curvature, v = u(1 + c u)
    _add("lct", [np.log(2.0)])
_add("lfs", [np.log(0.16 / 0.84)] * NW)
if SOFT:                              # soft-wall population, per regime
    _add("lfo", [np.log(0.10 / 0.90)] * NREG)
    _add("lqo", [np.log(0.7)] * NREG)      # hard wall population = the shoulder
_add("bFS", [0.0] * NW)
_add("lhs0", list(np.log([2500., 3000., 2500.]))[:NREG])
_add("bS", [-0.38] * NW)
_add("lhW0", list(np.log([300., 90., 300.]))[:NREG])
_add("bW", [-0.28] * NW)
_add("lkW", list(np.log([3.0, 3.5, 3.0]))[:NREG])
_add("bK", [0.0] * NW)
_add("ltail", [np.log(1.2)] * NREG)
_add("eS", [0.0] * n)
if SIG_EK:
    _add("eK", [0.0] * n)
_add("eW", [0.0] * n)
_add("dJs", [0.0] * n)
p0 = np.array(_p0)
NSHAPE = IX["dJs"][0]


def get(p, name):
    a, b = IX[name]
    return p[a:b]


def pdict(p):
    return {k: get(p, k) for k in IX}


def jmodel(p, cu, h):
    par = pdict(p)
    js = cu["js_rho"] + get(p, "dJs")[cu["i"]]
    return js * M.shape_si(par, cu["i"], cu["sieq"], cu["regime"], h,
                           toe=TOE, shoulder=SHOULDER)


def resid(p):
    r = [np.sqrt(cu["w"]) * (cu["j"] - jmodel(p, cu, cu["h"]))
         for cu in curves]
    r.append(get(p, "dJs") / SIG_JS)
    r.append(get(p, "eS") / (SIG_EI or SIG_E))
    if SIG_EK:
        r.append(get(p, "eK") / SIG_EK)
    r.append(get(p, "eW") / SIG_E)
    kR, mR = np.exp(get(p, "rot"))
    r.append(np.array([(kR * mR - 2.0) / SIG_T]))
    r.append((get(p, "lrs") - LRS0) / AKULOV_SIG)
    r.append(get(p, "bK") / SIG_BK)
    r.append(get(p, "bFT") / SIG_BFI)
    r.append(get(p, "bFS") / SIG_BFI)
    if TAIL_SIG:   # the low-carbon fit's prior on the wall tail, kW*mW ~ 2
        r.append((np.exp(get(p, "ltail")) - 2.0) / TAIL_SIG)
    return np.concatenate(r)


def robust_cost(p):
    z = (resid(p) / 0.08) ** 2
    return float(np.sum(0.08 ** 2 * 2 * (np.sqrt(1 + z) - 1)))


# ------------------------------------------------------------ multi-start
rng = np.random.default_rng(20260922)
print(f"multi-start fit ({NSHAPE} shape parameters + {3*n} per-grade) ...")
cands = []
for s in range(int(os.environ.get("NSTART", "6"))):
    pj = p0.copy()
    if s > 0:
        pj[:NSHAPE] += rng.normal(0, 0.15, NSHAPE)
    try:
        r1 = least_squares(resid, pj, method="lm", max_nfev=60000)
        r2 = least_squares(resid, r1.x, method="trf", loss="soft_l1",
                           f_scale=0.08, max_nfev=60000)
        cands.append((robust_cost(r2.x), s, r2.x))
        print(f"  start {s}: robust cost = {cands[-1][0]:.5f}")
    except Exception as e:
        print(f"  start {s}: failed ({e})")
cands.sort(key=lambda t: t[0])
p = cands[0][2]
par = pdict(p)
print(f"selected start {cands[0][1]}")

# ------------------------------------------------------------- diagnostics
print("\n================ fitted global parameters ================")
fr = 1 / (1 + np.exp(-get(p, "lfr")[0]))
kR, mR = np.exp(get(p, "rot"))
lrs = get(p, "lrs")[0]
print(f"rotation: fr={fr:.4f}  kR={kR:.3f}  mR={mR:.3f}  "
      f"tail kR*mR={kR*mR:.3f}")
print(f"          hR = Ha(Si) sqrt((2/105)/fr) x {np.exp(lrs):.3f}   "
      f"(lrs={lrs:+.3f}, prior sigma {AKULOV_SIG})")
print(f"          -> hR {S.ha(3.49)*np.sqrt((2/105)/fr)*np.exp(lrs):.0f} A/m "
      f"at 3.49% Si-eq, "
      f"{S.ha(0.39)*np.sqrt((2/105)/fr)*np.exp(lrs):.0f} at 0.39%")
sg = lambda z: 1 / (1 + np.exp(-z))
print(f"toe:      ft={sg(get(p,'lft')[0]):.4f} at Si=0, "
      f"bFT={get(p,'bFT')[0]:+.4f} /wt%  (scale "
      f"{np.exp(get(p,'lqT')[0]) if TOE_SCALE else 1.0:.3f} x hW)")
print(f"shoulder: fs={sg(get(p,'lfs')[0]):.4f} at Si=0, "
      f"bFS={get(p,'bFS')[0]:+.4f} /wt%")
print(f"knee:     kW = exp(lkW[regime] {get(p,'bK')[0]:+.4f} Si);  "
      f"kW(Si=0) as-sheared {np.exp(get(p,'lkW')[0]):.3f}, "
      f"decarb {np.exp(get(p,'lkW')[1]):.3f}")
print(f"wall tail kW*mW: as-sheared {np.exp(get(p,'ltail')[0]):.3f}, "
      f"decarb {np.exp(get(p,'ltail')[1]):.3f}")
print(f"pinning law:  ln hs = {get(p,'lhs0')[0]:+.3f}/{get(p,'lhs0')[1]:+.3f}"
      f" {get(p,'bS')[0]:+.4f} Si  (as-sheared/decarb)")
print(f"              ln hW = {get(p,'lhW0')[0]:+.3f}/{get(p,'lhW0')[1]:+.3f}"
      f" {get(p,'bW')[0]:+.4f} Si")
print(f"   i.e. hW falls {100*(1-np.exp(get(p,'bW')[0])):.1f}% per wt% Si"
      if get(p, "bW")[0] < 0 else "")

print("\n================ per-grade: the processing axis ================")
print("%-9s %5s %6s %8s %8s %8s %8s %9s %9s"
      % ("grade", "Si-eq", "mu_i", "hs A/m", "hW A/m", "eS", "eW", "dJs T",
         "RMS lnH"))
tot = []
for cu in curves:
    _fr, _ft, _fs, hs_, hW_, _kW, _mW, _hR, _, _ = M.parts(
        par, cu["i"], cu["sieq"], cu["regime"])
    mu_i = float(jmodel(p, cu, np.array([10.0]))[0] / (L.MU0 * 10.0)) + 1
    fn = lambda h, cu=cu: jmodel(p, cu, h)
    rr = L.lnh_residuals(_HJ(cu["h"], cu["j"]), fn)
    if not cu["excluded"]:
        tot.extend(rr)
    print("%-9s %5.2f %6.0f %8.1f %8.1f %+8.3f %+8.3f %+9.4f %9.3f%s"
          % (cu["name"], cu["sieq"], mu_i, hs_, hW_, get(p, "eS")[cu["i"]],
             get(p, "eW")[cu["i"]], get(p, "dJs")[cu["i"]], L.rms(rr),
             "  [excluded]" if cu["excluded"] else ""))
print(f"TOTAL RMS lnH = {L.rms(tot):.4f} over {len(tot)} points")

print("\nalloy pairs -- same resistivity, so the whole difference is eW/eS:")
for a, b in (("M19", "M22"), ("M27", "M36"), ("M47", "StatorB"),
             ("QCore_018in", "Type2S_018in")):
    ia, ib = [c["i"] for c in curves if c["key"] == a][0], \
             [c["i"] for c in curves if c["key"] == b][0]
    na, nb = rows[ia]["name"], rows[ib]["name"]
    print("  %-9s vs %-9s   d(ln hW) = %+.3f  (%+.1f%% in field), "
          "d(ln hs) = %+.3f"
          % (na, nb, get(p, "eW")[ib] - get(p, "eW")[ia],
             100 * (np.exp(get(p, "eW")[ib] - get(p, "eW")[ia]) - 1),
             get(p, "eS")[ib] - get(p, "eS")[ia]))

# ------------------------------------------------------------ save params
out = {"description": "bh_fit_si: silicon family, low-carbon form, "
                      "composition law + penalized per-grade pinning",
       "model": "si",
       "variant": dict(AKULOV_SIG=AKULOV_SIG, SIG_E=SIG_E, DECIM=DECIM,
                       EXCLUDE=EXCLUDE, TOE=TOE, SHOULDER=SHOULDER,
                       TOE_SCALE=TOE_SCALE, TAIL_SIG=TAIL_SIG,
                       GROUPS=GROUPS, SI_SPLIT=SPLIT,
                       SOFT=SOFT,
                       SIG_EK=SIG_EK),
       "grades": [r["key"] for r in rows],
       "params": {k: list(map(float, get(p, k))) for k in IX}}
with open(PARAMS, "w") as f:
    json.dump(out, f, indent=1)
print(f"\nwrote {PARAMS}")

# ---------------------------------------------------------------- emitter
F = M.SiFamily(PARAMS)
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(T.HMAX), 60)])
print(f"\n================ emitter -> {CSVDIR}/ ================")
nfail = 0
for r in rows:
    b = F.b(r["key"], HGRID)
    mu = b[1:] / (L.MU0 * HGRID[1:])
    # The low-carbon fit's gate (bhlib.gate): differential permeability,
    # the interval from the origin included, interior turns counted, and
    # the maximum required to lie past the first interval.
    dmin, turns, murat = L.gate(b, HGRID)
    ok = (bool(np.all(np.diff(b) > 0)) and dmin >= 0.999 and turns <= 1
          and murat < 1.0)
    nfail += not ok
    fn = f"{CSVDIR}/femm_{PFX}{r['key']}.csv"
    np.savetxt(fn, np.column_stack([b, HGRID]), fmt="%.9f", delimiter=",")
    print("  %-9s Js=%.4f (page %.4f)  mu_max=%5.0f  B(1000 Oe)=%.4f  B(4e5)=%.3f  "
          "min dB/dH/mu0=%.2f  turns=%d  mu_i/mu_max=%.2f%s"
          % (r["name"], F.js(r["key"]), F.js_fit(r["key"]), mu.max(),
             float(F.b(r["key"], 1000 * L.OE)), b[-1], dmin, turns, murat,
             "" if ok else "  <== GATE FAIL"))
print(f"  {len(rows)} tables, {nfail} gate failures")

# ------------------------------------------------------------------ plots
cmap = plt.get_cmap("viridis")
NROW = -(-len(curves) // 5)
fig, ax = plt.subplots(NROW, 5, figsize=(17, 3.6 * NROW), sharex=True,
                       sharey=True, squeeze=False)
for k, cu in enumerate(curves):
    a = ax[k // 5][k % 5]
    a.plot(cu["h"], cu["j"], "k.", ms=2, label="page")
    hh = np.logspace(np.log10(cu["h"][0]), np.log10(4e5), 300)
    a.plot(hh, jmodel(p, cu, hh), "-", color="crimson", lw=1.3, label="fit")
    a.axhline(cu["js_rho"], color="0.6", lw=0.6)
    a.set_xscale("log")
    a.set_ylim(0, 2.25)
    a.grid(alpha=0.3, which="both")
    a.set_title("%s  (%.2f%% Si-eq)" % (cu["name"], cu["sieq"]), fontsize=9)
    if k == 0:
        a.legend(fontsize=7, loc="upper left")
    if k >= 5:
        a.set_xlabel("H (A/m)")
    if k % 5 == 0:
        a.set_ylabel("J (T)")
fig.suptitle("bh_fit_si: US Steel 1978 non-oriented grades, four shared "
             "processes (toe, knee, shoulder, rotation) -- RMS lnH = %.4f"
             % L.rms(tot), fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.95))
fig.savefig(f"{OUT}/{PFX}percurve.png", dpi=120)

fig, ax = plt.subplots(1, 3, figsize=(15, 4.8))
si = np.array([r["sieq"] for r in rows])
for k, (lab, val, law) in enumerate((
        ("ln hW  (wall pinning scale)", "eW", ("lhW0", "bW")),
        ("ln hs  (shoulder pinning scale)", "eS", ("lhs0", "bS")),
        (None, None, None))):
    if lab is None:
        break
    a = ax[k]
    for j, r in enumerate(rows):
        y = (get(p, law[0])[S.regime(r, GROUPS)] + M._g(pdict(p), law[1], S.regime(r, GROUPS)) * r["sieq"]
             + get(p, val)[j])
        a.plot(r["sieq"], y, "o", color=cmap(j / (len(rows) - 1.0)))
        a.annotate(r["name"], (r["sieq"], y), fontsize=7,
                   xytext=(4, 3), textcoords="offset points")
    xs = np.linspace(0, 3.7, 50)
    for rg, ls in ((0, "-"), (1, "--")):
        a.plot(xs, get(p, law[0])[rg] + get(p, law[1])[0] * xs, ls,
               color="0.4", lw=1,
               label=["as sheared", "decarb annealed"][rg] + " law")
    a.set_xlabel("Si + 1.14 Al  (wt%)")
    a.set_ylabel(lab)
    a.grid(alpha=0.3)
    a.legend(fontsize=8)
a = ax[2]
for j, r in enumerate(rows):
    a.plot(r["sieq"], F.js(r["key"]), "o", color=cmap(j / (len(rows) - 1.0)))
    a.annotate(r["name"], (r["sieq"], F.js(r["key"])), fontsize=7,
               xytext=(4, 3), textcoords="offset points")
xs = np.linspace(0, 3.7, 50)
a.plot(xs, S.js(xs), "-", color="0.4", lw=1,
       label="Js = %.3f - %.3f Si (Exner)" % (S.JS_BASE, S.K_SI))
a.set_xlabel("Si + 1.14 Al  (wt%)")
a.set_ylabel("Js (T)")
a.grid(alpha=0.3)
a.legend(fontsize=8)
fig.suptitle("the composition law and the per-grade residuals that are the "
             "processing axis", fontsize=11)
fig.tight_layout(rect=(0, 0, 1, 0.94))
fig.savefig(f"{OUT}/{PFX}law.png", dpi=120)
print(f"wrote {OUT}/{PFX}percurve.png, {OUT}/{PFX}law.png")
