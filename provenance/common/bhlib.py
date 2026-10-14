# SPDX-License-Identifier: MIT
"""bhlib.py -- shared data loading + diagnostics for the B-H fitting work.

Reads the curated digitized curves from out/lowcarbon_curves.csv, the single
source of truth for them (extracted 2026-10-04 from the arrays of the original
Mathematica notebook bh_fit_v9_r7.m, which is otherwise superseded).

Units convention inside this library:
  H in A/m, B and J in tesla.  J = B - mu0*H (magnetic polarization).
"""

import os
import numpy as np

MU0 = 4e-7 * np.pi
OE = 1000.0 / (4 * np.pi)   # A/m per oersted
KG = 0.1                    # T per kilogauss
K1 = 4.8e4   # J/m^3, Graham, Phys. Rev. 112(4):1117 (1958): (480+/-10)e3 erg/cm^3
             # at 300 K, with K2 = 0 +/- 50e3.  Enters diagnostics only.

JS_FE = 2.158   # T, Sanford & Bennett, NBS RP1354 (1941): 21.58 +/- 0.01 kG at 25 C
# The sourced quantity is the CARBON COEFFICIENT, k_C = (JS_FE-JS_CEM)/6.67
# = 0.1736 T/wt%, from Cheney's annealed manganese-free alloys (NBS Sci.
# Paper 463, vol.18 p.609, Table 4, annealed 760-930 C -- his 760 C IS the
# 1400 F condition).  Each specimen gives one intensity per constituent, so
# the series brackets rather than fixes it: branches 0.162 and 0.201, mean
# 0.175.  JS_CEM is how that coefficient is WRITTEN here, not an independent
# measurement.  As a phase value it is low: cementite measures 1.23 T
# (Bozorth p.367, 128 emu/g at 15 C).  The gap is Bozorth's point that
# carbon lowers
# the moment of the iron as well as diluting it, which a two-phase mixture
# has no term for.  Do NOT 'correct' JS_CEM to 1.23: that puts k_C at 0.139,
# outside Cheney's bracket and away from every alloy series we have.  And do
# not try to fit it -- regressing it off our own implied levels gives 0.5 to
# 1.9 T depending on which level-suspect trace is kept.
JS_CEM = 1.0    # T, an interpretation of k_C above, not a phase value
K_MN = 0.0228   # T/wt%, Maurer & Schroeter (1929) and Exner (1960) both give
                # A_Mn = 2.90e-6 T.m3/kg per mass%; x 7876 kg/m3 -> 0.0228 T/wt%.
                # Tabulated in Ouda, Danninger & Gierl-Mayer, Powder Metall. (2018).
                # 6% above the non-magnetic-substituent floor of 0.0216.

CURVES = "out/lowcarbon_curves.csv"   # relative to the working directory (the project root)


K_C = (JS_FE - JS_CEM) / 6.67      # T/wt%, = 0.1736; see the JS_CEM note


def js_anchor(c, mn):
    """Saturation polarization from composition [T].  Eq. (5) of the note:

        Js = JS_FE - K_C*C - K_MN*Mn,     C and Mn in weight percent

    one attenuation coefficient per alloying element.  K_C is sourced from
    Cheney; the two-phase reading of it (C/6.67 of the mass becoming a
    carbide of about JS_CEM) is an interpretation of the magnitude, not
    where the number comes from -- see the JS_CEM note above.  6.67 is the
    wt% carbon of Fe3C on the rounded atomic weights Fe=56, C=12 (exactly
    1/15); on current weights it is 12.011/179.546 = 6.690, worth <0.2 mT
    here, and both are arithmetic needing no citation.
    """
    return JS_FE - K_C * c - K_MN * mn




def load_raw():
    """Return dict name -> (n,2) array, columns (B, H_Oe); B in T for SLAC,
    kG for the rest (matches the flags in curve definitions)."""
    rows = {}
    with open(CURVES, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or line.startswith("curve,"):
                continue
            name, b, h, _unit = line.strip().split(",")
            rows.setdefault(name, []).append((float(b), float(h)))
    return {n: np.array(v, dtype=float) for n, v in rows.items()}


class Curve:
    def __init__(self, name, c, mn, cond, bh, b_in_tesla,
                 hmin_oe=0.0, hmax_oe=1e4, sig_js=0.02, trust=True):
        self.name, self.c, self.mn, self.cond = name, c, mn, cond
        self.sig_js, self.trust = sig_js, trust
        sel = (bh[:, 1] >= hmin_oe) & (bh[:, 1] <= hmax_oe)
        # A floor in B for chart-digitized traces.  The U.S. Steel sheets
        # carry a linear kG axis at about 24 px/kG with a ~3 px pen riding
        # the axis, so a point at 0.1 kG is two pixels of a three-pixel
        # line.  BMIN_KG removes everything within that many kG of the
        # axis, on every sheet alike; 0.3 kG is about seven pixels and
        # drops nine points over seven traces.
        if not b_in_tesla and BMIN_KG > 0:
            sel &= bh[:, 0] >= BMIN_KG
        b = bh[sel, 0] * (1.0 if b_in_tesla else KG)
        h = bh[sel, 1] * OE
        self.h = h                       # A/m
        self.b = b                       # T
        self.j = b - MU0 * h             # T
        # slope of J wrt ln H (finite differences, as in r7)
        lnh = np.log(h)
        self.slope = np.gradient(self.j, lnh)
        self.w = 1.0 / (self.slope**2 + (0.05 * self.slope.max())**2)
        self.js_a = js_anchor(c, mn)
        self.ha = 2 * K1 / self.js_a


BMIN_KG = float(os.environ.get("BMIN_KG", "0.3"))   # 0 = keep every point


def load_curves():
    """The 14 curated curves of r7, identical windows/flags.

    LEVEL_SIG (T) overrides the 0.02 level prior of every tightly held
    curve, SLAC_SIG that of the two SLAC curves alone: the test of whether
    the rotation level is absorbing source-level error.  Unset = 0.02.
    """
    out = _curated()
    lev, slac = os.environ.get("LEVEL_SIG"), os.environ.get("SLAC_SIG")
    for cu in out:
        if cu.sig_js == 0.02 and lev:
            cu.sig_js = float(lev)
        if cu.name.startswith("SLAC") and slac:
            cu.sig_js = float(slac)
    return out


def _curated():
    d = load_raw()
    return [
        Curve("SLAC 1010 ann 750C", 0.10, 0.45, 4, d["slac1010"], True,
              hmin_oe=5.0, sig_js=0.02, trust=True),
        Curve("SLAC 1004 ann 750C", 0.04, 0.45, 4, d["slac1004"], True,
              hmin_oe=5.0, sig_js=0.02, trust=True),
        Curve("USS 1010 ann 1400F toe(<5Oe)", 0.10, 0.45, 4,
              d["AISI1010at1400degF"], False, hmax_oe=5.0,
              sig_js=0.02, trust=False),
        Curve("USS 1010 as hot rolled", 0.10, 0.45, 2,
              d["AISI1010asHotRolled"], False, sig_js=0.10, trust=False),
        Curve("USS 1010 ann 1300F", 0.10, 0.45, 3,
              d["AISI1010at1300degF"], False, sig_js=0.10, trust=False),
        Curve("USS 1010 ann 1500F", 0.10, 0.45, 5,
              d["AISI1010at1500degF"], False, sig_js=0.02, trust=True),
        Curve("USS 1020 as hot rolled", 0.20, 0.45, 2,
              d["AISI1020asHotRolled"], False),
        Curve("USS 1020 ann 1300F", 0.20, 0.45, 3,
              d["AISI1020at1300degF"], False),
        Curve("USS 1020 ann 1400F", 0.20, 0.45, 4,
              d["AISI1020at1400degF"], False),
        Curve("USS 1020 ann 1500F", 0.20, 0.45, 5,
              d["AISI1020at1500degF"], False),
        Curve("USS 1030 as hot rolled", 0.30, 0.75, 2,
              d["AISI1030asHotRolled"], False),
        Curve("USS 1030 ann 1300F", 0.30, 0.75, 3,
              d["AISI1030at1300degF"], False),
        Curve("USS 1030 ann 1400F", 0.30, 0.75, 4,
              d["AISI1030at1400degF"], False),
        Curve("USS 1030 ann 1500F", 0.30, 0.75, 5,
              d["AISI1030at1500degF"], False),
    ] + (_cheney_curves() if os.environ.get("CHENEY") == "1" else [])


def _cheney_curves(npts=25):
    """Cheney's annealed iron-carbon curves (out/cheney_fig10.csv) as fit data.

    Condition 5: annealed in vacuo, most at 820 C, which is 1508 F.
    Manganese-free.  Subsampled to npts per curve, log-spaced in H, so that
    1600 traced points do not outweigh the digitized traces.  Untrusted in
    the fit's sense: his levels sit 2-8 % below the composition law.
    """
    # four comment lines (which contain commas) and the column header
    raw = np.loadtxt(os.path.join("out", "cheney_fig10.csv"),
                     delimiter=",", skiprows=5)            # C_pct, H_Oe, B_G
    out = []
    for cpct in (0.018, 0.14, 0.23, 0.32):
        s = raw[np.isclose(raw[:, 0], cpct)]
        s = s[np.argsort(s[:, 1])]
        hq = np.exp(np.linspace(np.log(s[0, 1]), np.log(s[-1, 1]), npts))
        bq = np.interp(hq, s[:, 1], s[:, 2]) / 1000.0         # kG
        out.append(Curve("Cheney %.3f%%C ann 820C" % cpct, cpct, 0.0, 5,
                         np.column_stack([bq, hq]), False,
                         sig_js=0.10, trust=False))
    return out


def load_holdout():
    """Fermilab TM-1197 as-received 1008/1020 -> list of dicts."""
    d = load_raw()
    out = []
    for name, c, mn, key in [("TM1197 1008", 0.08, 0.45, "tm1197x1008"),
                             ("TM1197 1020", 0.20, 0.45, "tm1197x1020")]:
        bh = d[key]
        h = bh[:, 1] * OE
        b = bh[:, 0] * KG
        out.append(dict(name=name, c=c, mn=mn, h=h, j=b - MU0 * h))
    return out


def lnh_residuals(curve, jfit_fn, eps=1e-3):
    """Horizontal (ln H) residuals of a fitted J(h) function vs curve data."""
    h, j = curve.h, curve.j
    jf = jfit_fn(h)
    djdlnh = (jfit_fn(h * (1 + eps)) - jf) / eps
    return (j - jf) / djdlnh


def rms(x):
    x = np.asarray(x, float)
    return float(np.sqrt(np.mean(x**2)))


def gate(b, h):
    """The single-hump gate, used by both fits.  Returns min dB/dH
    in units of mu0, the interior turns in it, and mu_i/mu_max.

    b and h must INCLUDE the origin.  The secant from 0 to the first grid
    point is a row of the emitted table and is where the toe term does
    its damage, so dropping it hid the interval most likely to be wrong.

    Turns, not peaks.  A single-humped permeability has exactly one
    interior turn and a monotone one has none, so turns <= 1 is the test
    the note claims to apply; counting only maxima passed a curve that
    falls, turns and rises.  Ties are flattened first, since a log grid
    leaves differences of rounding either side of the hump.

    The third return is d[0]/max(d).  At 1 the maximum sits on the first
    interval, which is a permeability that starts at its peak: initial
    above maximum is not a thing soft iron does.
    """
    d = np.diff(b) / np.diff(h) / MU0
    dd = np.diff(d)
    # ties: rounding in a linear ramp reaches ~2e-8 of the peak; real
    # changes are 1e-3 and up, so 1e-6 separates them with room to spare
    dd[np.abs(dd) < 1e-6 * d.max()] = 0.0
    sg = np.sign(dd)
    sg = sg[sg != 0]
    turns = int(np.sum(np.diff(sg) != 0))
    return float(d.min()), turns, float(d[0] / d.max())
