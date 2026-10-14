# SPDX-License-Identifier: MIT
"""simodel.py -- the silicon-family model form, in one place.

Sibling of bhmodel.py.  Kept separate so the low-carbon chain does not
acquire a dependency on openpyxl and the catalog spreadsheet, and because
the two families are indexed by different things: low carbon by (carbon,
heat treatment), silicon by (silicon-equivalent, product form, grade).

THE FORM is the low-carbon fit's with ONE process added, as shares of one saturation:

  J(H)/Js =  ft P(H/ht)                 reversible toe, ht = q hW
           + fs H/(H + hs)              hard wall population -- the shoulder
           + fo (1 - 1/sqrt(1+(H/ho)^2))  soft wall population, ho < hW
           + (1-ft-fs-fo-fr) Burr(H; hW,kW,mW)   the knee
           + fr Burr(H; hR,kR,mR)                rotation

  In the adopted fit every wall parameter -- shares, scale ratios, silicon
  slopes, knee width, tail -- is per test condition (as sheared, decarb
  annealed; silib.regime), and only the rotation term and the composition
  laws are shared.  Annealed material then takes the low-carbon structure:
  its toe share falls to 0.001 and the soft population (5% at 0.43 hW)
  carries the rise from the origin into the knee.

  P(u) = u/sqrt(1+u^2), the low-carbon fit's toe, with q one fitted global
  ratio (variant TOE=straight, TOE_SCALE=1; fitted q = 3.2).  Parameter
  sets fitted before the variant was recorded used P(u) = u/(1+u) at
  ht = hW, and SiFamily reproduces them.

WHY FOUR AND NOT THREE.  The low-carbon fit's three processes fitted these pages to
RMS lnH 0.215 with 87% of the squared error in the bottom decade of the
chart, where it was wrong by a factor of 2 to 4 in field for M-47 and
Stator B.  The cause is structural, not numerical.  Low-carbon steel has a
soft knee and a long wall tail (exponent 0.9-1.2), so its wall Burr covered
the whole approach and left the Rayleigh term free to be a toe.  These
silicon curves have a SHARP knee, which forces the wall tail exponent to
2.2-2.6, so the Burr dies soon after the knee and something else must carry
the long shoulder between 1 and 30 kA/m.  the low-carbon Rayleigh term was
conscripted for it -- the fit put hi at 800-3100 A/m, an order of magnitude
ABOVE hW, which is not a toe by any reading -- and nothing was left for the
initial permeability.  Two cheaper repairs were tried and both failed: a
per-grade knee width (made it worse, RMS 0.29), and freeing the Rayleigh
scale per grade (the fit declined to use it, RMS unchanged).  Dropping the
chart's bottom decade instead fixed the residuals precisely BECAUSE it
removed the evidence -- hi stayed at 3200 A/m.

So the shoulder term keeps its place and its own scale hs, and is named for
what it does; and a genuine reversible toe is added.

THE TOE COSTS THREE PARAMETERS, NOT TEN: a share, its silicon slope, and
one global ratio q of its scale to the wall pinning field, ht = q hW, as the
low-carbon toe's scale is a fitted ratio to its wall scale.  The ratio is
what keeps the permeability single-humped.  At ht = hW the toe rolls off
before the knee (onset ~ H^5 to H^6) has begun, and the permeability dips
between the origin and the knee on the low-silicon grades -- by up to 12%
with the rational toe u/(1+u), whose slope falls from the origin, and by
0.5% with the straight toe -- where the pages rise steadily (M-47: 590 at
10 A/m to 990 at 40 A/m).  With q free the fit puts it at 3.2 and the
share at 0.10 to 0.18, so the toe is a reversible linear-onset population
reaching into the knee, not only the Rayleigh region.

THE SHOULDER STAYS RATIONAL.  The low-carbon fit's soft-wall kernel
1 - 1/sqrt(1+u^2) (H^2 onset) and the straight kernel (1/H^2 tail) both fit
these pages worse, RMS lnH 0.078 and 0.077 against 0.066, and fail the gate
more: the pages want the shoulder's linear onset and 1/H approach.

WHAT IS INDEXED BY WHAT.  The low-carbon family had one composition axis
(carbon) and four heat treatments, and several curves per treatment, so a
per-treatment pinning scale was identified.  Here every grade contributes
exactly ONE curve, so a free per-grade pinning scale would fit each curve
alone and leave nothing for a composition law to explain.  The alloy pairs
prove a second axis is needed all the same: M-19 and M-22 have the same
resistivity, hence the same silicon, and still differ; so do M-27/M-36 and
M-47/Stator B.  What separates them is processing (grain size, the M-number
is a core-loss grade), and the book states nothing about it.

So the pinning scales are a composition LAW plus a PENALIZED per-grade
residual:

    ln hW_i = lhW0[regime] + bW * sieq_i + eW_i,   eW_i ~ N(0, SIG_E)
    ln hs_i = lhs0[regime] + bS * sieq_i + eS_i,   eS_i ~ N(0, SIG_E)

The law is identified because the residuals are shrunk toward zero; the
residuals are the processing axis, one number per grade, and they are the
interesting output -- within an alloy pair they are the whole difference.

WHAT IS SHARED.  Everything about the SHAPE: the rotation share fr, its
Burr exponents, the knee width per product form, the wall tail exponent per
product form.  All ten grades therefore claim one knee shape and one
approach to saturation, differing only in where the knee sits, how high it
goes, and a silicon-scaled knee width.  That is the falsifiable content.

THE ROTATION SCALE IS NOT FREE.  the low-carbon fit fitted hR with an Akulov-consistency
prior around Ha*sqrt((2/105)/fr).  Here Ha varies by grade -- 43.3 down to
32.9 kA/m as silicon rises -- so the same relation fixes hR per grade from
Bozorth's K1(Si), leaving only ONE global multiplier:

    hR_i = Ha(sieq_i) * sqrt((2/105) / fr) * exp(lrs),   lrs ~ N(0, sig)

The silicon DEPENDENCE of the rotation term is thus imposed by Bozorth and
not fitted; only its overall level can move.  The pages do not measure that
level.  With the tail prior at 0.2 it slid with the exponent (freed: 1.17,
exponent 2.51); with the tail at 2 +/- 0.05, as low-carbon, it sits at 0.999
(freed 0.994), but a profile at 0.9/1.0/1.1/1.2 gives RMS 0.0746/0.0722/
0.0713/0.0711 -- the cost minimum at 1.0 is the exponent prior's, not the
data's.  So lrs near 0 says the K2 = 0 Akulov scale costs almost nothing.

NO FIELD-SCALE NUISANCE.  the low-carbon fit gives every curve a dlnH to absorb
disagreement between its data sources.  There is only one source here, one
book with one set of axes, calibrated by si_digitize to about 2 px rms, so
there is nothing for such a parameter to absorb except model error.  It is
left out deliberately.
"""
import json
import numpy as np

import bhlib as L
import silib as S


def burr(h, a, k, m):
    return 1 - (1 + (h / a) ** k) ** (-m)


def _sig(z):
    return 1 / (1 + np.exp(-z))


def _g(par, name, r):
    """A parameter that may be global (one value) or per regime."""
    v = par[name]
    return v[r] if len(v) > 1 else v[0]


def parts(par, i, sieq, r):
    """The process parameters for grade index i, silicon sieq, regime r.

    i may be None, which drops the per-grade residuals and gives the bare
    composition law -- the curve the family predicts for a silicon content
    it was never shown.
    """
    fr = _sig(par["lfr"][0])
    kR, mR = np.exp(par["rot"])
    hR = S.ha(sieq) * np.sqrt((2.0 / 105.0) / fr) * np.exp(par["lrs"][0])
    ft = _sig(_g(par, "lft", r) + _g(par, "bFT", r) * sieq)
    fs = _sig(_g(par, "lfs", r) + _g(par, "bFS", r) * sieq)
    eS = 0.0 if i is None else par["eS"][i]
    eW = 0.0 if i is None else par["eW"][i]
    eK = 0.0 if i is None or "eK" not in par else par["eK"][i]
    hs = np.exp(par["lhs0"][r] + _g(par, "bS", r) * sieq + eS)
    hW = np.exp(par["lhW0"][r] + _g(par, "bW", r) * sieq + eW)
    kW = np.exp(par["lkW"][r] + _g(par, "bK", r) * sieq + eK)
    mW = np.exp(par["ltail"][r]) / kW
    return fr, ft, fs, hs, hW, kW, mW, hR, kR, mR


def shape_si(par, i, sieq, r, h, toe="rational", shoulder="rational"):
    """toe: "rational" u/(1+u) or "straight" u/sqrt(1+u^2), u = H/hW.
    shoulder: "rational" H/(H+hs), "straight" (H/hs)/sqrt(1+(H/hs)^2), or
    "soft" 1 - 1/sqrt(1+(H/hs)^2), the low-carbon fit's soft-wall kernel.  The rational forms are steepest at
    the origin, so a large share of either makes the permeability fall
    before the knee; the straight toe and soft kernel start with zero
    curvature and zero slope respectively.  All four keep a 1/H or 1/H^2
    tail, so the approach to saturation is unchanged in kind.
    """
    fr, ft, fs, hs, hW, kW, mW, hR, kR, mR = parts(par, i, sieq, r)
    h = np.asarray(h, float)
    ht = hW * np.exp(_g(par, "lqT", r)) if "lqT" in par else hW
    if toe == "straight":
        t = h / np.sqrt(h * h + ht * ht)
    elif toe in ("curved", "scurved"):
        u = h / ht
        v = u * (1.0 + np.exp(par["lct"][0]) * u)       # Rayleigh's quadratic
        t = v / (1.0 + v) if toe == "curved" else v / np.sqrt(1.0 + v * v)
    else:
        t = h / (h + ht)
    if shoulder == "soft":
        s = 1 - 1 / np.sqrt(1 + (h / hs) ** 2)
    elif shoulder == "straight":
        s = h / np.sqrt(h * h + hs * hs)
    else:
        s = h / (h + hs)
    # optional soft-wall population, the low-carbon fit's H^2-onset kernel
    # 1 - 1/sqrt(1+(H/ho)^2) at ho = hW exp(lqo), share and ratio per regime
    fo, so = 0.0, 0.0
    if "lfo" in par:
        fo = _sig(_g(par, "lfo", r))
        ho = hW * np.exp(_g(par, "lqo", r))
        so = 1 - 1 / np.sqrt(1 + (h / ho) ** 2)
    return (ft * t + fs * s + fo * so
            + (1 - ft - fs - fo - fr) * burr(h, hW, kW, mW)
            + fr * burr(h, hR, kR, mR))


class SiFamily:
    """A fitted silicon family, loaded from its parameter JSON."""

    def __init__(self, path):
        P = json.load(open(path))
        self.P = P
        self.version = P["model"]
        self.par = {k: np.array(v, float) for k, v in P["params"].items()}
        self.keys = list(P["grades"])
        self.rows = {r["key"]: r for r in S.table()}
        self.idx = {k: i for i, k in enumerate(self.keys)}
        # Kernel forms are recorded variants; anything fitted before they
        # were recorded used the rational toe and rational shoulder.
        v = P.get("variant", {})
        self.kern = dict(toe=v.get("TOE", "rational"),
                         shoulder=v.get("SHOULDER", "rational"))
        # which shape-parameter set each grade uses (silib.regime)
        self.groups = v.get("GROUPS", "form")
        for r in self.rows.values():
            r["regime"] = S.regime(r, self.groups)

    # -- a named grade ------------------------------------------------
    def js(self, key):
        """Saturation for the EMITTED curve: the composition law, always.

        dJs is deliberately not added here.  It is a description of the
        PAGE, not of the steel -- it absorbs how far that one drawing runs
        short at its top, which si_composition.py measures at 0.4 to 2.7%
        and si_basis.py shows is neither the core-plate coating nor the
        assumed density.  Letting it set the emitted saturation put M-47
        (0.99% Si) above Type 2-S (0.39%) and M-36 (3.23%) above M-43
        (2.37%), i.e. it let a 1978 draughtsman's ink override the
        resistivity the same book states.  The shape is what the page
        measured and is kept; the level comes from composition, exactly as
        for the measured entries and for the whole low-carbon family.
        """
        return S.js(self.rows[key]["sieq"])

    def js_fit(self, key):
        """The level the page itself reached, i.e. what was fitted.  For
        diagnostics and for the page-shortfall audit, not for emission."""
        return (self.rows[key]["js_rho"]
                + float(self.par["dJs"][self.idx[key]]))

    def j(self, key, h):
        r = self.rows[key]
        return self.js(key) * shape_si(self.par, self.idx[key], r["sieq"],
                                       r["regime"], np.asarray(h, float),
                                       **self.kern)

    def b(self, key, h):
        h = np.asarray(h, float)
        return self.j(key, h) + L.MU0 * h

    def parts_of(self, key):
        r = self.rows[key]
        return parts(self.par, self.idx[key], r["sieq"], r["regime"])

    # -- the bare composition law, for a grade never measured ---------
    def j_law(self, sieq, regime, h):
        return S.js(sieq) * shape_si(self.par, None, sieq, regime,
                                     np.asarray(h, float), **self.kern)

    def b_law(self, sieq, regime, h):
        h = np.asarray(h, float)
        return self.j_law(sieq, regime, h) + L.MU0 * h
