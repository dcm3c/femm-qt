# SPDX-License-Identifier: MIT
"""cofe_build.py -- three cobalt-iron library entries from pooled vendor tables.

  CoFe 49Co-2V, magnetic anneal
  CoFe 49Co-2V, rotor grade (strength anneal, ~400-600 MPa yield)
  CoFe 49Co-2V, high strength (~800 MPa yield)
  CoFe 27Co

Data: out/cofe_points.csv (cofe_points.py), every B(H) point the held vendor
documents tabulate, with its source.  No curve is taken from a figure.

Selection.  Carpenter's Hiperco 50 strip table is kept at 10 Oe only: from
50 Oe up it implies J up to 2.40 T at 16 kA/m, above the J_s these points
are held to, while every other source levels off at 2.28-2.31 T.  Bar stock
is excluded from the strip entries except the duplex-treated 50A bar, which
behaves as strip; plain annealed bar is 0.5 T harder at 160 A/m.  AFK 502 FP
(fully processed, 600 MPa) sits 0.1 T below the other rotor-grade sources at
10 kA/m and is shown, not fitted.  Where two grades share one table row
(VACOFLUX 50 and VACODUR 49) or a vendor repeats a value for each thickness,
the point is counted once.

Saturation is given, never fitted:
  49Co-2V   2.30 T  Bozorth, Ferromagnetism, Ch. 6 Table 1 (Fe-Co with 2% V,
                    23,000 G) and Vacuumschmelze's stated J_s
  rotor     2.29 T  Vacuumschmelze J_s of the grades pooled, VACODUR 49 2.30
                    and VACODUR 50 2.28
  27Co      2.38 T  Vacuumschmelze VACOFLUX 27 and Imphy AFK 1 (Carpenter's
                    Hiperco 27 sheet says 2.36, its brochure 2.29)

Form.  J/Js = (1 - fr) F(H; a1, k1, m1) + fr u / sqrt(1 + u^2), u = H/a2,
a Burr knee plus a term whose deficit falls as 1/(2u^2), the law of
approach (see shape()).  The Burr deficit falls as H^-(k1 m1); k1 m1 is
not held, but every entry fits it at 1 or above (1.05-2.33, 2026-10-03),
so no tail is slower than a0/H.
Residuals are horizontal, in ln H at each point's J, weighted so every
source-and-condition set carries equal total weight.  The vendors' maximum
permeability is a soft constraint (sigma 0.3 in ln), because for the
magnetic anneal the permeability peak lies below the lowest tabulated field,
and the knee exponent k has a mild prior, ln k ~ N(ln 1.5, 0.5), so an entry
with no low-field data cannot turn its knee into a step.

Usage:  python cofe_build.py   -> femm_cofe_<tag>.csv, cofe_fit.dat,
                                  out/cofe_fit.png, out/cofe_fit.json
"""
import csv
import os
import json
import numpy as np
from scipy.optimize import least_squares
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L

MU0 = 4e-7 * np.pi
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])
# read at import, so located beside this module: other papers' scripts
# import it from their own working directories
R = list(csv.DictReader(open(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "out", "cofe_points.csv"))))

ENTRIES = [
    dict(tag="49Co2V_mag", name="CoFe 49Co-2V, magnetic anneal",
         group="49V_mag", js=2.30, mu_max=12000, rho=41.0,
         keep=lambda r: not (r["grade"] == "Hiperco 50"
                             and float(r["H_Am"]) > 1000)
         and r["condition"] not in ("bar 1600F", "bar 1700F slow cool"),
         note="mu_max: VAC 15,000, AFK 502 R 8,000-11,000"),
    dict(tag="49Co2V_rotor", name="CoFe 49Co-2V, rotor grade",
         group="49V_rotor", js=2.29, mu_max=6300, rho=42.0,
         keep=lambda r: r["grade"] != "AFK 502 FP",
         note="mu_max: VACODUR 49 strength anneal 8,000, VACODUR 50 5,000"),
    dict(tag="49Co2V_hs", name="CoFe 49Co-2V, high strength",
         group="49V_hs", js=2.25, mu_max=1100, rho=42.0,
         keep=lambda r: r["grade"] != "Hiperco 50 HS",
         note="~800 MPa class: VACODUR S Plus strength anneal (Rp0.2 800, "
              "Rm 1200 MPa) and AFK 502 FP .8/.9 (800/950 MPa), which agree "
              "at 8-16 kA/m.  Js 2.25 T and mu_max 1,100: VAC for S Plus.  "
              "Hiperco 50 HS at 683 MPa is close to the rotor grade, shown "
              "not fitted.  No tabulated point below 800 A/m."),
    dict(tag="27Co", name="CoFe 27Co",
         group="27Co", js=2.38, mu_max=2800, rho=18.0,
         keep=lambda r: True,
         note="mu_max: AFK 1 2,500-2,600, VACOFLUX 27 3,000"),
]


def burr(h, a, k, m):
    return 1 - (1 + (h / a) ** k) ** (-m)


def shape(p, h):
    """(1 - fr) Burr knee + fr u/sqrt(1+u^2), u = H/a2.  The second term
    starts linearly and its deficit falls as 1/(2u^2), the law of
    approach; its slope only falls, so it cannot make a second peak in
    dB/dH the way an H^2-onset term does."""
    fr = 1 / (1 + np.exp(-p[0]))
    a1, k1, m1, a2 = np.exp(p[1:5])
    u = h / a2
    return (1 - fr) * burr(h, a1, k1, m1) + fr * u / np.sqrt(1 + u * u)


SIG_J = 0.02    # T: level scatter between vendors near saturation


def block(name, mu, bh, sigma):
    """A matlib.dat block, the same format as emit_matlib.block."""
    return (["<BeginBlock>", f'<BlockName> = "{name}"',
             f"<Mu_x> = {mu:.1f}", f"<Mu_y> = {mu:.1f}", "<H_c> = 0",
             "<H_cAngle> = 0", "<J_re> = 0", "<J_im> = 0",
             f"<Sigma> = {sigma}", "<d_lam> = 0", "<Phi_h> = 0",
             "<Phi_hx> = 0", "<Phi_hy> = 0", "<LamType> = 0",
             "<LamFill> = 1", "<NStrands> = 0", "<WireD> = 0",
             f"<BHPoints> = {len(bh)}"]
            + [f"\t{b:.9f}\t{h:.6f}" for b, h in bh] + ["<EndBlock>"])


def points(e):
    seen, pts = set(), []
    for r in R:
        if r["group"] != e["group"] or not e["keep"](r):
            continue
        h, b = float(r["H_Am"]), float(r["B_T"])
        key = (r["source"], round(h), round(b, 4))
        if key in seen:
            continue
        seen.add(key)
        pts.append((h, b - MU0 * h, r["source"] + " " + r["grade"] + ", "
                    + r["condition"]))
    sets = {}
    for h, j, s in pts:
        sets.setdefault(s, []).append((h, j))
    return pts, sets


def fit(e):
    pts, sets = points(e)
    h = np.array([p[0] for p in pts])
    j = np.array([p[1] for p in pts])
    w = np.array([1 / np.sqrt(len(sets[p[2]])) for p in pts])
    hd = np.logspace(0, np.log10(4e5), 800)

    def resid(p):
        jm = e["js"] * shape(p, h)
        dj = e["js"] * (shape(p, h * 1.01) - shape(p, h / 1.01)) / 0.0199
        r = w * (jm - j) / np.sqrt(dj ** 2 + SIG_J ** 2)
        mu = (e["js"] * shape(p, hd) + MU0 * hd) / (MU0 * hd)
        # knee exponent: a mild prior toward the gentle knees the well-
        # sampled 49Co fits choose (k 1.2-1.5).  Without it the high-
        # strength entry, with no point below 800 A/m, took k = 79, a step.
        return np.concatenate([r, [np.log(mu.max() / e["mu_max"]) / 0.3,
                                   (p[2] - np.log(1.5)) / 0.5]])

    best = None
    rng = np.random.default_rng(1)
    for s in range(12):
        p0 = np.array([np.log(0.1 / 0.9), np.log(np.median(h) / 3), np.log(3.0),
                       np.log(0.5), np.log(np.median(h) * 3)])
        if s:
            p0 = p0 + rng.normal(0, 0.5, 5)
        try:
            r = least_squares(resid, p0, loss="soft_l1", f_scale=0.1,
                              max_nfev=20000)
        except Exception:
            continue
        if best is None or r.cost < best.cost:
            best = r
    p = best.x
    dj = e["js"] * (shape(p, h * 1.01) - shape(p, h / 1.01)) / 0.0199
    lnh = (e["js"] * shape(p, h) - j) / np.maximum(dj, 1e-4)
    res = (e["js"] * shape(p, h) - j) / np.sqrt(dj ** 2 + SIG_J ** 2)
    return p, pts, sets, lnh, res, e["js"] * shape(p, h) - j


def main():
    out, lines = {}, []
    fig, ax = plt.subplots(2, 4, figsize=(24, 9),
                           gridspec_kw=dict(height_ratios=[2.2, 1]))
    for c, e in enumerate(ENTRIES):
        p, pts, sets, lnh, res, dJ = fit(e)
        knee = np.array([q[1] for q in pts]) < 0.95 * e["js"]
        b = e["js"] * shape(p, HGRID) + MU0 * HGRID
        b[0] = 0.0
        dmin, turns, murat = L.gate(b, HGRID)
        ok = bool((np.diff(b) > 0).all() and dmin >= 0.999 and turns <= 1
                  and murat < 1)
        mu = b[1:] / (MU0 * HGRID[1:])
        sigma = round(100.0 / e["rho"], 2)
        np.savetxt("femm_cofe_%s.csv" % e["tag"], np.column_stack([b, HGRID]),
                   delimiter=",", fmt="%.9f")
        print("%-34s Js %.2f  %d pts: knee RMS lnH %.3f (%d), top RMS dJ "
              "%.0f mT (%d)  mu_max %5.0f (target %d)  gate %s  Sigma %.2f"
              % (e["name"], e["js"], len(pts),
                 np.sqrt(np.mean(lnh[knee] ** 2)), knee.sum(),
                 1000 * np.sqrt(np.mean(dJ[~knee] ** 2)) if (~knee).any()
                 else float("nan"), (~knee).sum(),
                 mu.max(), e["mu_max"], "ok" if ok else "FAIL", sigma))
        out[e["tag"]] = dict(name=e["name"], js=e["js"], params=list(p),
                             rms_lnh_knee=float(np.sqrt(np.mean(lnh[knee] ** 2))),
                             rms_dJ_top_mT=(float(1000 * np.sqrt(np.mean(dJ[~knee] ** 2)))
                                            if (~knee).any() else None),
                             n=len(pts), mu_max=float(mu.max()), gate_ok=ok,
                             sigma_MSm=sigma, note=e["note"])
        lines += block(e["name"], float(mu[0]), list(zip(b, HGRID)), sigma)
        a, ar = ax[0, c], ax[1, c]
        hd = np.logspace(0, np.log10(4e5), 600)
        a.semilogx(hd, e["js"] * shape(p, hd), "k-", lw=1.6, label="entry")
        for s, v in sets.items():
            v = np.array(sorted(v))
            a.semilogx(v[:, 0], v[:, 1], "o", ms=4, label=s)
        k = [q for q in R if q["group"] == e["group"] and not e["keep"](q)]
        if k:
            hk = np.array([float(q["H_Am"]) for q in k])
            jk = np.array([float(q["B_T"]) for q in k]) - MU0 * hk
            a.semilogx(hk, jk, "x", color="gray", ms=6, label="not fitted")
        a.axhline(e["js"], color="gray", lw=0.6, ls=":")
        top = out[e["tag"]]["rms_dJ_top_mT"]
        a.set_title("%s  (Js %.2f T; knee RMS lnH %.3f, top %s)"
                    % (e["name"], e["js"], out[e["tag"]]["rms_lnh_knee"],
                       "%.0f mT" % top if top is not None else "no points"),
                    fontsize=10)
        a.set_ylim(0, 2.5)
        a.set_xlim(1, 4e5)
        a.grid(True, which="both", alpha=0.3)
        a.legend(fontsize=6, loc="lower right")
        a.set_ylabel("J = B - mu0 H [T]")
        ar.semilogx([q[0] for q in pts], res, "o", ms=4)
        ar.axhline(0, color="k", lw=0.6)
        ar.set_xlim(1, 4e5)
        ar.set_ylabel("residual, ln H (0.02 T at the top)")
        ar.set_xlabel("H [A/m]")
        ar.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig("out/cofe_fit.png", dpi=95)
    with open("cofe_fit.dat", "w", newline="\r\n") as f:
        f.write("\n".join(["<BeginFolder>", '<FolderName> = "Cobalt Iron (fit)"']
                          + lines + ["<EndFolder>"]) + "\n")
    json.dump(out, open("out/cofe_fit.json", "w"), indent=1)
    print("wrote femm_cofe_*.csv, cofe_fit.dat, out/cofe_fit.png, "
          "out/cofe_fit.json")


if __name__ == "__main__":
    main()
