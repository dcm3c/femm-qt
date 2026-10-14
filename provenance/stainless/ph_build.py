# SPDX-License-Identifier: MIT
"""ph_build.py -- precipitation-hardening stainless library entries from the
Cleveland-Cliffs (ARMCO) bulletins' DC normal induction curves.

  17-4 PH   Condition A, H 900, H 1075/H 1100, H 1150
  15-5 PH   the same four
  PH 13-8 Mo  Condition H 950

Data: out/ph_curves.csv (dig_ph.py).  These are hardened martensites, well
outside the maximum-permeability range of the annealed ferritic family
(ss_build.py, 755-4064; here 138-445), and they do not scale onto its
master shape (check_ph.py), so each condition is its own curve, as the
nickel-irons are (nife_build.py): the entry IS the vendor curve, carried
past its last charted point toward a saturation taken from composition.

The two bulletins publish the same curves.  Condition by condition, over
the field range where both are drawn, 17-4 PH and 15-5 PH agree to 0.02 T
in B (0.10 T for one near-vertical stretch of the H 900 pair) and to
0.02-0.03 in ln H at fixed B -- closer than either is to any other curve in
this project.  So each condition is pooled from the two, which matters
because the 15-5 chart does not draw the foot of its H 900 and Condition A
curves at all (they begin at 0.49 and 0.25 T) while the 17-4 chart draws
all four to the origin.  The two alloys then differ only in their
saturation and their resistivity.

Below 0.1 T nothing is used.  Both charts are linear in B over 0-14 kG in
about 220 points of artwork, so a point is 6 mT and 50 A/m, and what the
redraws put near the origin is not data: on the 15-5 chart two curves are
missing there, and on the 17-4 chart the H 900 and H 1075 curves stand
nearly vertical between 11 and 13 Oe.  Below its first used point each
entry is a straight line from the origin, as elsewhere in this project.

SATURATION, from composition, given and not fitted.  Js = Js(Fe) - sum
k_i x_i over the specification mid-range, k in T per weight per cent:

    Cr 0.0306   Bozorth Fig. 7-23, p. 229, as ss_build.py uses it
    Mn 0.0228   Maurer and Schroeter / Exner, as bhlib.py uses it
    Si 0.048    Exner, as silib.py uses it
    Al 0.055    Exner, as silib.py uses it
    Ni 0.0020   Exner, A_Ni = 0.25e-6 T m3/kg per mass%, x 7876 kg/m3
    Mo 0.0099   Exner, A_Mo = 1.26e-6
    Cu 0.0228   Hoselitz, A_Cu = 2.89e-6; Exner's set has no copper
    Nb 0.0216   no coefficient published in any of the three sets; taken
                as a non-magnetic substituent, Js(Fe)/100, which is what
                Hoselitz's copper very nearly is (0.0228)
    C  0.1736   bhlib.K_C, carbon as cementite

all from Table 1 of Ouda, Danninger and Gierl-Mayer, Powder Metall. 61(5)
(2018), which collects Maurer and Schroeter (1929), Exner (1960) and
Hoselitz, converted at the density of iron, 7876 kg/m3, as bhlib.K_MN is.
Two of the four coefficients this project already had are reproduced by
that table exactly (Mn, and Si and Al are taken from it); Exner's chromium,
0.0292 T/wt%, agrees with Bozorth's 0.0306 to 5%, which is 0.02 T on 16% Cr.

Silicon, manganese and carbon are specified only as maxima; they are
carried at half the maximum, and the uncertainty printed with each Js is
the half-width of the band spanned by the whole specification.

    17-4 PH  (S17400)  1.51 T     15-5 PH (S15500)  1.57 T
    PH 13-8 Mo (S13800) 1.66 T

CHECK.  The law of approach, J = Js - a/H, says the exponent
n = (dJ/dlnH)/(Js - J) that nife_build.tail() measures at a curve's last
point should be 1.  Against the composition Js, with nothing fitted, it
comes out near 1 for the three softer conditions of both alloys: 1.14,
1.49 and 1.15 for 17-4's Condition A, H 900 and H 1075 on Js = 1.514, and
0.93, 1.13 and 0.86 for 15-5's on 1.570.  H 1150 (0.64, 0.57) and
PH 13-8 Mo H 950 (0.52) do not get there: either they are still short of
the asymptotic range at the end of their charts (the last field is 7 and
6 times their coercive force, against 12 for H 1075), or a few per cent of
their volume is non-magnetic -- reverted austenite is the usual reading
for H 1150.  These charts cannot separate the two, and no measurement of
either is held, so the composition Js stands and the tail approaches it at
the curve's own pace.

Coercive force, from the hysteresis figures (out/ph_hyst.csv), is reported
beside each entry.  It is not fitted; it is what makes the ordering of the
curves make sense, and the deficit at the end of each chart tracks it:
(Js - J)H/Hc there is 1.9-2.6 for the three softer conditions, 3.3-3.7 for
H 1150, and 3.7 for 13-8 Mo H 950.  The hysteresis figures draw only the
top of each loop (they stop at -2 kG), so these are minor-loop values, the
tips at about 10 kG.

RESISTIVITY.  17-4 PH Table 17 and 15-5 PH Table 18 give 100 and 98
microhm-cm in Condition A and 80 and 77 aged (17-4 lists 80 for both H 900
and H 1075; neither bulletin gives a figure for H 1150, which is taken at
the aged value).  PH 13-8 Mo Table 18 gives 100.1 microhm-cm at 25 C in
Condition A only; the H 950 entry uses 80, that times the 0.80 by which
aging lowers 17-4's -- an inference across alloys, not a published number.

Usage:  python ph_build.py   -> femm_ph_<tag>.csv, ph_fit.dat,
                                out/ph_fit.png, out/ph_fit.json
"""
import csv
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
import silib as S
from cofe_build import block
from nife_build import tail, smooth

MU0 = 4e-7 * np.pi
HGRID = np.concatenate([[0.0], np.logspace(np.log10(5.0), np.log10(4.0e5),
                                           70)])
J_MIN = 0.10

RHO_FE = 7876.0          # kg/m3, the conversion bhlib.K_MN is stated with
# T/wt%; see the docstring for where each one comes from
K = dict(cr=0.0306, mn=L.K_MN, si=S.K_SI, al=S.K_AL,
         ni=0.25e-6 * RHO_FE, mo=1.26e-6 * RHO_FE, cu=2.89e-6 * RHO_FE,
         nb=L.JS_FE / 100.0, c=L.K_C)

# specification composition, wt%: (low, high) per element, "max" as (0, x)
COMP = {
    "17-4": dict(cr=(15.00, 17.50), ni=(3.00, 5.00), cu=(3.00, 5.00),
                 nb=(0.15, 0.45), si=(0.0, 1.00), mn=(0.0, 1.00),
                 c=(0.0, 0.07)),
    "15-5": dict(cr=(14.00, 15.50), ni=(3.50, 5.50), cu=(2.50, 4.50),
                 nb=(0.15, 0.45), si=(0.0, 1.00), mn=(0.0, 1.00),
                 c=(0.0, 0.07)),
    "13-8": dict(cr=(12.25, 13.25), ni=(7.50, 8.50), al=(0.90, 1.35),
                 mo=(2.00, 2.50), si=(0.0, 0.10), mn=(0.0, 0.10),
                 c=(0.0, 0.05)),
}


M_NI, M_AL = 58.693, 26.982


def js_of(comp, nial=False):
    """Saturation at the mid-range composition, and the half-width of the
    band the whole specification spans.

    nial=True (PH 13-8 Mo aged): all the aluminium as beta-NiAl, which
    carries no moment, exactly as check_455.py ties nickel into Ni3Ti for
    Custom 455 -- the matrix loses the aluminium and the nickel it takes,
    is renormalized, and its saturation is scaled by 1 - f.  About -10 mT.
    (17-4 and 15-5 need nothing: epsilon-Cu is nearly pure copper, which
    as a non-magnetic phase costs Js_Fe/100 = 0.0216 T/wt%, within 0.0012
    of the 0.0228 charged for copper in solution, about 4 mT in all.)
    """
    mids = {e: (lo + hi) / 2 for e, (lo, hi) in comp.items()}
    half = sum(K[e] * (hi - lo) / 2 for e, (lo, hi) in comp.items())
    if nial and "al" in mids:
        ni_tied = mids["al"] * M_NI / M_AL
        f = (mids["al"] + ni_tied) / 100.0
        m = {e: x / (1 - f) for e, x in mids.items() if e != "al"}
        m["ni"] = (mids["ni"] - ni_tied) / (1 - f)
        return (1 - f) * (L.JS_FE - sum(K[e] * x for e, x in m.items())), half
    return L.JS_FE - sum(K[e] * x for e, x in mids.items()), half


SRC = {"17-4": "Cleveland-Cliffs ARMCO 17-4 PH",
       "15-5": "Cleveland-Cliffs ARMCO 15-5 PH VAC CE",
       "13-8": "Cleveland-Cliffs ARMCO PH 13-8 Mo"}
CONDS = ["Condition A", "H900", "H1075 & H1100", "H1150"]
# curve name in out/ph_curves.csv -> (tag suffix, entry name suffix)
SHOWN = dict(zip(CONDS, [("A", "Condition A"), ("H900", "H 900"),
                         ("H1075", "H 1075/H 1100"), ("H1150", "H 1150")]))

ENTRIES = []
for alloy, rho_a, rho_h in [("17-4", 100.0, 80.0), ("15-5", 98.0, 77.0)]:
    for cond in CONDS:
        suffix, pretty = SHOWN[cond]
        ENTRIES.append(dict(
            tag="%s_%s" % (alloy.replace("-", ""), suffix),
            name="Stainless %s PH, %s" % (alloy, pretty),
            alloy=alloy, cond=cond,
            rho=rho_a if cond == "Condition A" else rho_h,
            # both bulletins draw this condition; pool them
            keep=[(SRC["17-4"], cond), (SRC["15-5"], cond)]))
ENTRIES.append(dict(tag="138Mo_H950", name="Stainless PH 13-8 Mo, H 950",
                    alloy="13-8", cond="H950", rho=80.0,
                    keep=[(SRC["13-8"], "H950")]))

HYST = {(r["source"], r["curve"]): r
        for r in csv.DictReader(open("out/ph_hyst.csv"))}
# the hysteresis figures label the conditions their own way
HC_OF = {("17-4", "Condition A"): ("17-4", "Condition A"),
         ("17-4", "H900"): ("17-4", "H1100 & H900"),
         ("17-4", "H1075 & H1100"): ("17-4", "H1075"),
         ("17-4", "H1150"): ("17-4", "H1150"),
         ("15-5", "Condition A"): ("15-5", "Condition A"),
         ("15-5", "H900"): ("15-5", "H1100 & H900"),
         ("15-5", "H1075 & H1100"): ("15-5", "H1075"),
         ("15-5", "H1150"): ("15-5", "H1150")}


def hc_of(e):
    """Coercive force for an entry, A/m: the hysteresis figure's, or the
    13-8 Mo bulletin's stated 27 Oe."""
    if e["alloy"] == "13-8":
        return 27.0 * 1000 / (4 * np.pi)
    a, c = HC_OF[(e["alloy"], e["cond"])]
    return float(HYST[(SRC[a], c)]["Hc_Am"])


R = list(csv.DictReader(open("out/ph_curves.csv")))


def curves(keep):
    """{label: (H, J)} for the wanted (source, curve) pairs, from 0.1 T."""
    out = {}
    for src, name in keep:
        v = sorted((float(r["H_Am"]), float(r["B_T"])) for r in R
                   if r["source"] == src and r["curve"] == name)
        h, b = np.array(v).T
        k = b >= J_MIN
        out["%s, %s" % (src, name)] = (h[k], np.maximum.accumulate(b[k])
                                       - MU0 * h[k])
    return out


def pool(sets, js):
    """Each curve with its tail to Js, then the mean of ln H at fixed J.
    Unlike nife_build.pool the J range is the union, not the intersection:
    where the 15-5 chart does not draw the foot of a curve, the 17-4 chart
    carries that stretch alone."""
    cur, ns = [], []
    for h, j in sets.values():
        ht, jt, n = tail(h, j, js)
        ns.append(n)
        k = np.concatenate([[True], np.diff(jt) > 1e-7])
        cur.append((ht[k], jt[k]))
    jq = np.linspace(min(c[1][0] for c in cur), min(c[1][-1] for c in cur),
                     2000)
    num = np.zeros_like(jq)
    den = np.zeros_like(jq)
    for ht, jt in cur:
        m = (jq >= jt[0]) & (jq <= jt[-1])
        num[m] += np.interp(jq[m], jt, np.log(ht))
        den[m] += 1.0
    return np.exp(num / den), jq, ns


def trim_foot(h, j):
    """Drop the leading points, if any, over which the secant permeability
    J/H falls.  The entry's foot is a straight line from the origin through
    the first point, so a first point whose J/H is above the next one's
    puts a dip in dB/dH at the join and costs the single-hump gate.  It is
    an artefact of the smoothing, whose low-end power-law extension lifts
    the first point; the charted curves themselves rise out of J/H."""
    i = int(np.argmax(np.diff(j / h) >= 0))     # first point it rises from
    return h[i:], j[i:]


def on_grid(h, j):
    h, j = trim_foot(h, j)
    jg = np.interp(np.log(HGRID[1:]), np.log(h), j)
    lo = HGRID[1:] < h[0]
    jg[lo] = j[0] * HGRID[1:][lo] / h[0]
    return np.concatenate([[0.0], jg + MU0 * HGRID[1:]])


def gate_ok(b):
    dmin, turns, murat = L.gate(b, HGRID)
    return bool((np.diff(b) > 0).all() and dmin >= 0.999 and turns <= 1
                and murat < 1)


def main():
    out, lines = {}, []
    ncol = 5
    nblk = -(-len(ENTRIES) // ncol)
    fig, ax = plt.subplots(2 * nblk, ncol, figsize=(5.2 * ncol, 4.5 * nblk),
                           squeeze=False,
                           gridspec_kw=dict(height_ratios=[2.2, 1] * nblk))
    for c, e in enumerate(ENTRIES):
        # aged 13-8 Mo: its aluminium is in beta-NiAl (see js_of)
        js, djs = js_of(COMP[e["alloy"]],
                        nial=e["alloy"] == "13-8" and e["cond"] != "A")
        sets = curves(e["keep"])
        h, j, ns = pool(sets, js)
        for sigma in (0.0, 0.01, 0.02, 0.03, 0.05, 0.07, 0.1, 0.15):
            hs, jsm = smooth(h, j, sigma)
            b = on_grid(hs, jsm)
            if gate_ok(b):
                break
        ok = gate_ok(b)
        mu = b[1:] / (MU0 * HGRID[1:])
        sig = round(100.0 / e["rho"], 2)
        hc = hc_of(e)
        jb = b[1:] - MU0 * HGRID[1:]
        res, dev = {}, []
        for s, (hh, jj) in sets.items():
            d = np.log(hh) - np.interp(jj, jb, np.log(HGRID[1:]))
            res[s] = (hh, d)
            dev += list(d)
        rms = float(np.sqrt(np.mean(np.array(dev) ** 2)))
        # end of the charted data, and the deficit there against Hc
        he = max(hh[-1] for hh, _ in sets.values())
        je = max(jj[-1] for _, jj in sets.values())
        np.savetxt("femm_ph_%s.csv" % e["tag"], np.column_stack([b, HGRID]),
                   delimiter=",", fmt="%.9f")
        lines += block(e["name"], float(mu[0]), list(zip(b, HGRID)), sig)
        print("%-32s Js %.3f+-%.3f  %d curve(s) RMS lnH %.3f  tail n %s  "
              "smooth %.2f  mu_max %4.0f at %5.0f A/m  Hc %4.0f A/m  "
              "(Js-J)H/Hc %.1f  B(8k) %.3f  gate %s  Sigma %.2f"
              % (e["name"], js, djs, len(sets), rms,
                 "/".join("%.2f" % n for n in ns), sigma, mu.max(),
                 HGRID[1:][mu.argmax()], hc, (js - je) * he / hc,
                 np.interp(8e3, HGRID, b), "ok" if ok else "FAIL", sig))
        out[e["tag"]] = dict(name=e["name"], alloy=e["alloy"],
                             cond=e["cond"], js=js, js_halfband=djs,
                             comp=COMP[e["alloy"]], curves=list(sets),
                             tail_n=ns, smooth_dec=sigma, rms_lnh=rms,
                             mu_max=float(mu.max()), mu_init=float(mu[0]),
                             hc_Am=hc, rho_uohmcm=e["rho"], sigma_MSm=sig,
                             gate_ok=ok)
        a, ar = ax[2 * (c // ncol), c % ncol], ax[2 * (c // ncol) + 1,
                                                  c % ncol]
        a.semilogx(HGRID[1:], jb, "k-", lw=1.6, label="entry")
        for s, (hh, jj) in sets.items():
            a.semilogx(hh, jj, ".", ms=4, label=s.split(", ")[0][17:])
        a.axhline(js, color="gray", lw=0.6, ls=":")
        a.set_title("%s\nJs %.3f T, mu_max %.0f, Hc %.0f A/m"
                    % (e["name"], js, mu.max(), hc), fontsize=9)
        a.set_xlim(5, 4e5)
        a.set_ylim(0, 1.8)
        a.grid(True, which="both", alpha=0.3)
        a.legend(fontsize=6, loc="upper left")
        a.set_ylabel("J = B - mu0 H [T]")
        for s, (hh, d) in res.items():
            ar.semilogx(hh, d, ".", ms=3)
        ar.axhline(0, color="k", lw=0.6)
        ar.set_xlim(5, 4e5)
        ar.set_ylim(-0.3, 0.3)
        ar.set_xlabel("H [A/m]")
        ar.set_ylabel("curve - entry, ln H")
        ar.grid(True, which="both", alpha=0.3)
    for c in range(len(ENTRIES), nblk * ncol):
        ax[2 * (c // ncol), c % ncol].axis("off")
        ax[2 * (c // ncol) + 1, c % ncol].axis("off")
    fig.tight_layout()
    fig.savefig("out/ph_fit.png", dpi=95)
    with open("ph_fit.dat", "w", newline="\r\n") as f:
        f.write("\n".join(
            ["<BeginFolder>",
             '<FolderName> = "PH Stainless (Cleveland-Cliffs data)"']
            + lines + ["<EndFolder>"]) + "\n")
    json.dump(out, open("out/ph_fit.json", "w"), indent=1)
    print("wrote femm_ph_*.csv, ph_fit.dat, out/ph_fit.png, out/ph_fit.json")


if __name__ == "__main__":
    main()
