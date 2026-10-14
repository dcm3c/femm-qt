# SPDX-License-Identifier: MIT
"""metglas_build.py -- amorphous-ribbon library entries from Metglas DC loops.

  Metglas 2605SA1, field annealed     (Fe-B-Si, the standard transformer alloy)
  Metglas 2605HB1M, field annealed    (higher-saturation successor)

Data: out/metglas_loops.csv (dig_metglas.py), the upper halves of the
Hm = 8 and 80 A/m DC hysteresis loops of Metglas, "Amorphous Alloys for
Transformer Cores" (29 April 2011), pp. 5 and 10: single straight strips,
25.4 x 200 mm, after a 2-hour longitudinal field anneal.

The normal curve.  Metglas charts loops, not an initial magnetization
curve, so the entry is built from the loops and anchored on their two
measured tips, which are points of the normal curve by definition (the
normal curve is the locus of the tips of symmetric loops).  At each B the
entry lies between the centre line of the 80 A/m loop, Hc(B), and its
ascending branch, Ha(B):  H(B) = Hc + f(B) (Ha - Hc), with f rising linearly
from 0 at B = 0 to the value f8 that puts the curve through the 8 A/m loop's
tip, and held at f8 above it.  Both branches meet at the 80 A/m tip, so the
curve passes through that tip whatever f is.  At low B the entry is the
centre line, where FEMM is insensitive to it (relative permeability of
order 10^5).

Above 80 A/m nothing is measured.  The tail runs from the 80 A/m tip
(H_e, J_e) to the given saturation Js with deficit
    Js - J = a (H_e/H)^(1/2) + c (H_e/H),
a + c = Js - J_e and a/2 + c = s, the curve's own slope dJ/dlnH over its
last 0.3 decade, so J and dJ/dH are continuous at the join.  The form is
the first two terms of Kronmueller's law of approach for amorphous alloys,
M = Ms [1 - a_1/2 (mu0 H)^-1/2 - a_1 (mu0 H)^-1 - a_2 (mu0 H)^-2]
+ b (mu0 H)^1/2, as given by Pietrusiewicz et al. (Archives of Metallurgy
and Materials 59, 2014, Eq. 1): the H^-1/2 term from point defects (free
volume), 1/H from quasi-dislocation dipoles.  Here a and c are set by the
join, not fitted to high-field data, and c comes out negative for 2605SA1,
so the tail borrows the law's form, not its physical coefficients.  It is
an assumption, bounded below by J_e and above by Js, and the bounds are
reported.

Saturation is given, never fitted (Metglas, p. 1, "General Properties"):
2605SA1 1.56 T, 2605HB1M 1.63 T.  Resistivity 1.3 and 1.2 uOhm m.  Ribbon
25 +- 4 um thick (d_lam).  LamFill is Metglas's lamination factor for these
two alloys, >= 84 % (p. 1), the bound used as the value.  (The FEMM wiki's
`stackingfactor` page gives 0.75-0.80 for generic 0.001 in laminations;
Metglas's own figure for its ribbon is preferred.)  Set LamFill to suit the
core being modelled.

Usage:  python metglas_build.py  -> femm_metglas_<tag>.csv, metglas_fit.dat,
                                    out/metglas_fit.png, out/metglas_fit.json
"""
import csv
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
from nife_build import HGRID, smooth, on_grid

MU0 = 4e-7 * np.pi
ENTRIES = [
    dict(tag="2605SA1", name="Metglas 2605SA1, field annealed", js=1.56, rho=130.0),
    dict(tag="2605HB1M", name="Metglas 2605HB1M, field annealed", js=1.63, rho=120.0),
]
D_LAM_MM = 0.025
LAM_FILL = 0.84
B_START = 0.2
R = list(csv.DictReader(open("out/metglas_loops.csv")))


def loop(alloy, name):
    return np.array([(float(r["H_Am"]), float(r["B_T"])) for r in R
                     if r["alloy"] == alloy and r["loop"] == name])


def branches(v, bq, tol=0.003):
    """Left (descending) and right (ascending) branch H at each B of bq."""
    lo, hi = [], []
    for b in bq:
        m = np.abs(v[:, 1] - b) < tol
        lo.append(v[m, 0].min())
        hi.append(v[m, 0].max())
    return np.array(lo), np.array(hi)


def normal_curve(e):
    v80, v8 = loop(e["tag"], "Hm80"), loop(e["tag"], "Hm8")
    tip80 = v80[v80[:, 1].argmax()]
    tip8 = v8[v8[:, 1].argmax()]
    # below 0.2 T the centre line sits at 0.05-0.3 A/m, the size of the
    # chart's H calibration error (~0.05 A/m), so it starts at 0.2 T; below
    # that the entry is a straight line from the origin (on_grid)
    bq = np.arange(B_START, tip80[1] - 0.004, 0.002)
    lo, hi = branches(v80, bq)
    hc = 0.5 * (lo + hi)
    # f8: the weight that puts the curve through the 8 A/m tip
    hc8, ha8 = np.interp(tip8[1], bq, hc), np.interp(tip8[1], bq, hi)
    f8 = float((tip8[0] - hc8) / (ha8 - hc8))
    f = f8 * np.minimum(1.0, bq / tip8[1])
    h = hc + f * (hi - hc)
    h = np.concatenate([h, [tip80[0]]])
    b = np.concatenate([bq, [tip80[1]]])
    k = h > 0
    h, b = h[k], b[k]
    h = np.maximum.accumulate(h)
    keep = np.concatenate([[True], np.diff(h) > 1e-6])
    return h[keep], b[keep] - MU0 * h[keep], tip8, tip80, f8


def tail(h, j, js, hmax=4.0e5):
    he, je = h[-1], j[-1]
    m = h >= he / 2.0
    s = float(np.polyfit(np.log(h[m]), j[m], 1)[0])
    de = js - je
    a, c = 2.0 * (de - s), 2.0 * s - de
    ht = np.logspace(np.log10(he), np.log10(hmax), 300)[1:]
    x = he / ht
    jt = js - (a * np.sqrt(x) + c * x)
    assert (np.diff(jt) > 0).all() and (jt < js).all(), (a, c)
    return np.concatenate([h, ht]), np.concatenate([j, jt]), dict(a=a, c=c, s=s, de=de)


def block(name, mu, bh, sigma):
    return (["<BeginBlock>", f'<BlockName> = "{name}"',
             f"<Mu_x> = {mu:.1f}", f"<Mu_y> = {mu:.1f}", "<H_c> = 0",
             "<H_cAngle> = 0", "<J_re> = 0", "<J_im> = 0",
             f"<Sigma> = {sigma}", f"<d_lam> = {D_LAM_MM}", "<Phi_h> = 0",
             "<Phi_hx> = 0", "<Phi_hy> = 0", "<LamType> = 0",
             f"<LamFill> = {LAM_FILL}", "<NStrands> = 0", "<WireD> = 0",
             f"<BHPoints> = {len(bh)}"]
            + [f"\t{b:.9f}\t{h:.6f}" for b, h in bh] + ["<EndBlock>"])


def main():
    out, lines = {}, []
    fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
    for c, e in enumerate(ENTRIES):
        h, j, tip8, tip80, f8 = normal_curve(e)
        ht, jt, tp = tail(h, j, e["js"])
        for sigma in (0.0, 0.01, 0.02, 0.03, 0.05, 0.07, 0.1, 0.15, 0.2):
            hs, jsm = smooth(ht, jt, sigma)
            b = on_grid(hs, jsm)
            dmin, turns, murat = L.gate(b, HGRID)
            ok = bool((np.diff(b) > 0).all() and dmin >= 0.999 and turns <= 1 and murat < 1)
            if ok:
                break
        bg = lambda hh: float(np.interp(hh, HGRID, b))
        miss8 = bg(tip8[0]) - tip8[1]
        miss80 = bg(tip80[0]) - tip80[1]
        mu = b[1:] / (MU0 * HGRID[1:])
        sig = round(100.0 / e["rho"], 3)
        np.savetxt("femm_metglas_%s.csv" % e["tag"], np.column_stack([b, HGRID]),
                   delimiter=",", fmt="%.9f")
        # bounds above 80 A/m: J held at J_e, and J = Js
        hb = np.array([1e2, 3e2, 1e3, 3e3, 1e4])
        jent = [bg(x) - MU0 * x for x in hb]
        print("%-34s f8 %.2f  tip misses %+.4f / %+.4f T  tail a %.3f c %+.3f (s %.3f)  "
              "smooth %.2f  gate %s  mu_max %.0f at %.2f A/m  Sigma %.3f"
              % (e["name"], f8, miss8, miss80, tp["a"], tp["c"], tp["s"], sigma,
                 "ok" if ok else "FAIL", mu.max(), HGRID[1:][mu.argmax()], sig))
        print("    J at %s A/m: %s  (bounds %.3f .. %.2f)"
              % ("/".join("%g" % x for x in hb), " ".join("%.3f" % x for x in jent),
                 tip80[1] - MU0 * tip80[0], e["js"]))
        out[e["tag"]] = dict(name=e["name"], js=e["js"], f8=f8, tip8=list(map(float, tip8)),
                             tip80=list(map(float, tip80)), tail=tp, smooth_dec=sigma,
                             gate_ok=ok, mu_max=float(mu.max()), sigma_MSm=sig,
                             miss8_T=miss8, miss80_T=miss80, d_lam_mm=D_LAM_MM, lam_fill=LAM_FILL,
                             J_at=dict(zip(map(str, hb), jent)))
        lines += block(e["name"], float(mu[0]), list(zip(b, HGRID)), sig)
        a = ax[c]
        for nm, col in (("Hm80", "tab:blue"), ("Hm8", "tab:red")):
            v = loop(e["tag"], nm)
            a.semilogx(np.abs(v[:, 0]) + 1e-3, v[:, 1], ",", color=col, alpha=0.4)
        a.semilogx(HGRID[1:], b[1:], "k-", lw=1.5, label="entry")
        a.semilogx([tip8[0], tip80[0]], [tip8[1], tip80[1]], "ko", mfc="none", label="loop tips")
        hh = HGRID[HGRID >= tip80[0]]
        a.fill_between(hh, tip80[1] + MU0 * (hh - tip80[0]), e["js"] + MU0 * hh,
                       color="gray", alpha=0.2, label="bounds above 80 A/m")
        a.axhline(e["js"], color="gray", ls=":", lw=0.8)
        a.set_xlim(0.1, 4e5)
        a.set_ylim(0, 1.8)
        a.grid(True, which="both", alpha=0.3)
        a.set_title("%s (Js %.2f T)" % (e["name"], e["js"]), fontsize=10)
        a.set_xlabel("H [A/m]")
        a.set_ylabel("B [T]")
        a.legend(fontsize=8, loc="lower right")
    fig.tight_layout()
    fig.savefig("out/metglas_fit.png", dpi=95)
    with open("metglas_fit.dat", "w", newline="\r\n") as f:
        f.write("\n".join(["<BeginFolder>", '<FolderName> = "Amorphous Ribbon (Metglas data)"']
                          + lines + ["<EndFolder>"]) + "\n")
    json.dump(out, open("out/metglas_fit.json", "w"), indent=1)
    print("wrote femm_metglas_*.csv, metglas_fit.dat, out/metglas_fit.png, out/metglas_fit.json")


if __name__ == "__main__":
    main()
