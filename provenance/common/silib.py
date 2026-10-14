# SPDX-License-Identifier: MIT
"""silib.py -- the silicon-steel family: what each grade is, and the three
constants that turn its stated resistivity into magnetics.

One home for the per-grade table so that si_composition.py (the audit),
si_measured.py (the digitized entries) and bh_fit_si.py (the regression)
cannot disagree about what M-27 is, in the same way bhmodel.py is one home
for the low-carbon model form.

Everything here comes from the book itself or from a cited measurement:

  resistivity, density   per grade, US Steel, *Nonoriented Sheet Steel for
                         Magnetic Applications*, ADUSS 31-7103-01 (May 1978),
                         transcribed by the user into SiliconSteel/density.xlsx
  rho(composition)       same book p.116 (scan img012)
  density(composition)   same book p.115 (scan img014)
  k_Si, k_Al             Exner (1960), via Ouda, Danninger & Gierl-Mayer,
                         Powder Metall. 61(5) (2018) Table 1
  Js(Fe)                 Sanford & Bennett, NBS RP1354 (1941)  [bhlib.JS_FE],
                         less the book's example C and Mn at the low-carbon
                         family's own coefficients [bhlib.js_anchor]
  K1(Fe)                 Graham, Phys. Rev. 112(4):1117 (1958) [bhlib.K1]
  dK1/dSi                Bozorth, *Ferromagnetism* (1951) Fig. 12-16

THE COMPOSITION COORDINATE.  The book states no chemistry.  Its resistivity
formula is linear in every element, so one resistivity fixes one combination:
with the minor elements held at the book's own worked example, rho gives
Si + 1.14 Al.  That is within 1% of the saturation-equivalent Si + 1.15 Al
on Exner's coefficients, so the one thing the resistivity can tell us is
exactly the one thing the saturation level needs.  Si and Al separately are
NOT recoverable -- the book's densities are the ASTM band values of its own
p.115 table, quoted by silicon band, and carry no information beyond the band.
Everywhere below, `sieq` means Si + 1.14 Al in wt%, and the silicon laws are
applied to it.  For anisotropy that is an assumption: Fe-Al lowers K1 too,
but Bozorth gives no Fe-Al curve to weight it by, and Al is at most ~0.4 wt%
here.

THE ANISOTROPY COORDINATE.  Bozorth Fig. 12-16 plots K1 against per cent
silicon in iron (Tarasov [39T2] plus others) and draws a straight line
through it.  Read off its intersections with the x = 0 and x = 6 rules of
the figure's own grid, the line is

    K1 = (487 - 44.2 Si) x 10^3 erg/cm^3   =   4.87e4 - 4.42e3 Si   J/m^3

which extrapolates to zero at 11.0 wt% Si -- Bozorth's text says "about
11-12% (20-22 atomic per cent)", so the reading is right.  The intercept
4.87e4 J/m^3 also agrees with Graham's later (4.80 +/- 0.10)e4 for pure
iron, so the line is taken as a FRACTIONAL law anchored on Graham:

    K1(sieq) = K1_Fe (1 - sieq / 11.0)

Caveat kept in view: Bozorth's own 3.85% Si single crystal (Fig. 12-9) has
K2 = 1e4 J/m^3 against K1 = 2.87e4, where pure iron has K2 = 0 +/- 0.5e4.
The Akulov coefficient 8/105 (K1/Js)^2 assumes K2 = 0, so at the high-silicon
end it is good to perhaps 10-20% -- about the width of the prior it feeds,
and the fit is free to move within that.
"""
import numpy as np
from scipy import ndimage
import openpyxl

import bhlib as L

XLSX = "SiliconSteel/density.xlsx"

# --- the book's two composition formulas -----------------------------------
# p.116, 80 F, +/-0.7 uOhm-cm:
#   rho = 10.45 + 6.0 C + 4.5 Mn + 13.7 P - 10.4 S + 11.6 Si + 3.1 Cu
#         + 2.4 Ni + 4.7 Cr + 2.8 Mo + 13.2 Al
# p.115, 20 C:
#   density = 7.865 - 0.065 (Si + 1.7 Al)
C, MN, S = 0.005, 0.25, 0.007       # the book's own worked example
RHO0 = 10.45 + 6.0 * C + 4.5 * MN - 10.4 * S        # 11.53 uOhm-cm
RHO_SI, RHO_AL = 11.6, 13.2         # uOhm-cm per wt%
SIG_RHO = 0.7                       # the formula's stated accuracy
BAND = {7.85: (0.00, 0.65), 7.80: (0.66, 1.40), 7.75: (1.41, 2.15),
        7.70: (2.16, 2.95), 7.65: (2.96, 3.70)}     # p.115, in Si + 1.7 Al

# --- saturation and anisotropy ---------------------------------------------
K_SI, K_AL = 0.048, 0.055           # T/wt%, Exner via Ouda 2018 Table 1
SI0 = 11.0                          # wt% where Bozorth's K1 line reaches zero

# --- the grades ------------------------------------------------------------
# key -> (catalog name, gauge in mils, catalog page, condition)
ASHEARED = ("fully processed, half lengthwise and half transverse, "
            "tested as sheared")
DECARB = ("cold rolled lamination sheet, tested after a decarburizing "
          "anneal at 1450 F")
GRADES = [
    ("M19",          "M-19",     14.0, 19,  ASHEARED),
    ("M22",          "M-22",     14.0, 27,  ASHEARED),
    ("M27",          "M-27",     14.0, 35,  ASHEARED),
    ("M36",          "M-36",     14.0, 43,  ASHEARED),
    ("M43",          "M-43",     18.5, 59,  ASHEARED),
    ("M45",          "M-45",     18.5, 69,  ASHEARED),
    ("M47",          "M-47",     18.5, 79,  ASHEARED),
    ("StatorB",      "Stator B", 18.5, 85,  ASHEARED),
    ("QCore_018in",  "Q-Core",   18.0, 95,  DECARB),
    ("Type2S_018in", "Type 2-S", 18.0, 101, DECARB),
    # scanned 2026-09-27, the same "Cold Rolled Lamination Sheet" section:
    # 24 gage is 0.025 in, 25 mil
    ("StatorB_24FP", "Stator B", 25.0, 87,  ASHEARED),
    ("StatorB_24SP", "Stator B SP", 25.0, 89, DECARB),
    ("QCore_024in",  "Q-Core",   24.0, 97,  DECARB),
    ("Type2S_024in", "Type 2-S", 24.0, 103, DECARB),
]
# The book's composition table (density.xlsx) has one row per grade, not
# per gauge or processing; a display name that differs from its row says
# which row it reads.
BOOK_ROW = {"Stator B SP": "Stator B"}
# The book files Stator B, Q-Core and Type 2-S in section 4, "USS Cold
# Rolled Lamination Sheet", apart from the M-grades.
LAMINATION = ("Stator B", "Stator B SP", "Q-Core", "Type 2-S")


def regime(row, groups="form"):
    """Which set of shape parameters a grade uses.

    groups="form": 0 = tested as sheared, 1 = tested after the 1450 F
    decarburizing anneal (the book's two test conditions).
    groups="section": as "form", but as-sheared lamination sheet (Stator B
    fully processed) gets its own set, 2, apart from the M-grades.
    """
    r = 0 if row["cond"] is ASHEARED else 1
    if groups == "section" and r == 0 and row["section"] == "lamination":
        return 2
    return r

# The book gives the y axis two scales at once, B in kG on the left and
# normal permeability on the right, at 1000 mu per 2 kG (si_digitize
# KG_PER_MAJOR / MU_PER_MAJOR).  That ratio alone sets the field at which
# its two drawn curves are equally good measurements of B -- see load_raw.
MU_PER_KG = 500.0
H_X = (1000.0 / MU_PER_KG) * L.OE     # 2.00 Oe = 159 A/m

LEVEL_TOL = 0.015       # curve top vs resistivity: beyond this si_composition
                        # calls the page low and leaves it out of the k_Si fit.
                        # si_measured does NOT use it -- it rescales every
                        # page, because all ten run low, not just these three.


def si_eq(rho):
    """Si + 1.14 Al [wt%] from the book's resistivity, p.116."""
    return (rho - RHO0) / RHO_SI


# The minor elements held in RHO0 dilute the moment too.  Leaving them out of
# the saturation while keeping them in the resistivity put every level 6.6 mT
# (0.3%) high -- 5.7 of it the 0.25 Mn -- and a fit of k_Si through pure
# iron absorbed that offset as extra slope (0.0525 instead of 0.0501).
# bhlib.js_anchor is the low-carbon family's own law, so the two agree.
JS_BASE = L.js_anchor(C, MN)        # 2.1514 T


def js(sieq):
    """Saturation polarization [T] from the composition coordinate."""
    return JS_BASE - K_SI * sieq


def k1(sieq):
    """Cubic anisotropy constant [J/m^3], Bozorth Fig. 12-16 as a
    fractional law anchored on Graham's pure-iron value."""
    return L.K1 * (1.0 - sieq / SI0)


def ha(sieq):
    """Anisotropy field 2 K1 / Js [A/m] -- the scale the rotation term and
    the Akulov law of approach both live on."""
    return 2.0 * k1(sieq) / js(sieq)


def akulov_frac(h, sieq):
    """J/Js at field h [A/m] under the law of approach, J = Js(1 - a/H^2)
    with a = (2/105) Ha^2, i.e. the (8/105)(K1/Js)^2 coefficient."""
    return 1.0 - (2.0 / 105.0) * ha(sieq) ** 2 / h ** 2


def sigma(rho):
    """Conductivity [MS/m] from resistivity [uOhm-cm]."""
    return 100.0 / rho


def raw_csv(key):
    return "out/si_%s_raw.csv" % key


MU_JUMP = 0.2          # ln units between neighbouring columns


def _mu_break(h, mu):
    """First column below 2 H_X at which the permeability trace jumps, or
    len(h) if it never does.

    The two curves cross exactly once, and by the axis design they cross at
    H_X, so "the permeability trace has landed on the magnetization stroke"
    cannot be tested by comparing them -- there they are equal for real.
    A tracking break shows instead as a DISCONTINUITY, and the margin is
    wide: Q-Core jumps 0.80 in ln mu between two columns at 70 A/m (its
    tracker follows the wrong stroke until the true crossing at 160 A/m),
    while the largest jump on any other page is 0.06 and the typical
    column-to-column step is 0.001.  Past the break the magnetization curve
    carries the whole reading, which costs nothing: B is 0.9-1.4 T there
    and one pixel of it is 0.08%.
    """
    k = np.where((h[1:] < 2 * H_X)
                 & (np.abs(np.diff(np.log(mu))) > MU_JUMP))[0]
    return int(k[0]) + 1 if len(k) else len(h)


def load_raw(key):
    """(H [A/m], B [T], mu) of a si_digitize trace, with B read from BOTH
    curves the page draws and combined by which one the chart resolves.

    THE PAGE MEASURES B TWICE.  The magnetization curve gives B directly;
    the permeability curve gives it as B = mu * H.  Each is read to about
    one pixel of the same y axis, and the book shares that axis between
    them at MU_PER_KG = 500 permeability units per kilogauss.  So one pixel
    is 1/500 kG read off the B curve, and (H[Oe]/1000) x 500/500 ... in
    plain terms, one pixel of the mu curve is worth H[Oe]/2 pixels of the
    B curve.  The two readings are therefore equally precise at

        H_X = 1000 / MU_PER_KG = 2.00 oersted    (159 A/m)

    -- and note what is NOT in that: no page, no pixel count, no grade.
    The px/kG calibration cancels, leaving only the ratio of the two axis
    scales, which the book chose once for all ten charts.

    Below 2 Oe the magnetization curve is drawn ON the axis, where a pen
    width is the whole value: at the left edge it implies initial
    permeabilities of 1100 to 5200, against 460 to 1750 from the
    permeability curve, and for M-43 it reads 4x high.  Above 2 Oe the
    permeability curve is the compressed one, collapsing toward its own
    axis as mu falls through 1000 and then 100.

    Neither is discarded.  Both errors are one pixel, i.e. CONSTANT in B,
    so the inverse-variance combination is parameter-free:

        B = [ (H_X/H)^2 B_mu  +  B_mag ] / [ (H_X/H)^2 + 1 ]

    which is the permeability curve at low field, the magnetization curve
    at high field, and an even average where they cross.  Where they
    overlap they agree to 1-4%, so the blend has nothing to smooth over.

    Finally the result is forced non-decreasing in the POLARIZATION
    J = B - mu0 H, not just in B.  The trace is one sample per scanned
    column and carries +/- 2 px of stroke-width noise; near the top of the
    chart, where J is nearly flat, that noise makes J fall between
    neighbouring columns even though B still rises, and interpolating it
    gives a table whose dB/dH dips below mu0.  A 21-column running median
    (the width of the noise, 1.6% of a decade in H, and exact on any
    monotone run) plus a running maximum for the residue.  Discarding the
    offending samples instead throws away 40-50% of the trace and truncates
    it at the first noise peak -- for M-19 that cost the top 18% of the
    field range.
    """
    d = np.loadtxt(raw_csv(key), delimiter=",", skiprows=1)
    h, b, mu = d[:, 0] * L.OE, d[:, 1] * L.KG, d[:, 2]
    t = (H_X / h) ** 2
    t[_mu_break(h, mu):] = 0.0
    b = (t * ndimage.median_filter(mu * L.MU0 * h, size=21, mode="nearest")
         + b) / (t + 1.0)
    j = np.maximum.accumulate(ndimage.median_filter(b - L.MU0 * h, size=21,
                                                    mode="nearest"))
    return h, j + L.MU0 * h, mu


def _book_values():
    """name -> (density, resistivity) as transcribed from the book."""
    ws = openpyxl.load_workbook(XLSX, data_only=True).worksheets[0]
    out = {}
    for row in list(ws.iter_rows(values_only=True))[1:]:
        if row[0] is not None:
            out[row[0]] = (float(row[1]), float(row[2]))
    return out


def table():
    """One dict per grade, in catalog order, carrying everything downstream
    needs: what the book says, what that implies, and what the page draws."""
    book = _book_values()
    rows = []
    for key, name, mils, page, cond in GRADES:
        dens, rho = book[BOOK_ROW.get(name, name)]
        sieq = si_eq(rho)
        h, b, _ = load_raw(key)
        j = b - L.MU0 * h
        j_top = j[-1] / akulov_frac(h[-1], sieq)        # curve's own Js
        rows.append(dict(
            key=key, name=name, mils=mils, page=page, cond=cond,
            rho=rho, dens=dens, sieq=sieq, sig_sieq=SIG_RHO / RHO_SI,
            band=BAND[round(dens, 2)], sigma=sigma(rho),
            section="lamination" if name in LAMINATION else "M",
            regime=0 if cond is ASHEARED else 1,
            js_rho=js(sieq), js_curve=j_top, k1=k1(sieq), ha=ha(sieq),
            h_last=h[-1], low=abs(j_top / js(sieq) - 1.0) >= LEVEL_TOL))
    return rows


def al_bound(sieq, band):
    """Upper bound on Al [wt%] from the density band: the band constrains
    Si + 1.7 Al while the resistivity fixes Si + 1.14 Al."""
    return max(0.0, (band[1] - sieq) / (1.7 - RHO_AL / RHO_SI))


# Named grades the catalog does not carry, emitted from the composition law
# (no per-grade residual: average processing, as sheared).  Each composition
# is a stated resistivity from a held source, reduced to Si-eq by the same
# p.116 formula as the catalog's own grades.  An M-number is a core-loss
# grade, not a composition, so the source is part of the entry.
#   M-15: Cleveland-Cliffs, DI-MAX Non-Oriented Electrical Steels (April
#   2026), Table 8, "DI-MAX M-15 FP": 50 uOhm-cm, saturation 20.1 kG,
#   maximum permeability 8000 (typical, as-sheared specimens).  Table 5
#   rates M-15 at 29 and 26 gauge; 29 gauge (14 mil) is used.  Checked
#   against Armco's 1984 DI-MAX M-15 sheet (fully processed, as sheared),
#   the same product line, in compare_dimax.py.
LAW_GRADES = [dict(name="M-15", rho=50.0, mils=14,
                   source="DI-MAX rho 50", regime=0)]


def law_grades():
    """LAW_GRADES with the composition quantities filled in."""
    out = []
    for g in LAW_GRADES:
        d = dict(g)
        d.update(sieq=si_eq(g["rho"]), sigma=sigma(g["rho"]),
                 js_rho=js(si_eq(g["rho"])))
        out.append(d)
    return out
