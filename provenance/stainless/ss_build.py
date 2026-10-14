# SPDX-License-Identifier: MIT
"""ss_build.py -- annealed stainless steels as one family, and a cold-drawn
entry.

Four annealed DC curves -- Carpenter's Chrome Core 12-FM and 430FR Solenoid
Quality (fully annealed bar, Fahy permeameter), Valbruna's MG2 after its
magnetic anneal, and the Metals Handbook's 416 annealed -- fall on one
curve when J is divided by the saturation Js and H by H50, the field at
which J = Js/2: from Js/2 to 10 H50 they lie within 0.06 in J/Js of one
another, 0.03 on average, and below Js/2 they part (0.17 by 0.3 Js), although
their chromium runs from 12 to 18% and H50 from 174 to 1001 A/m.  And each
one's c = mu_max mu0 H50 / Js is close to the master's own c_M = 0.507:
0.506, 0.506, 0.516 and 0.542, the last being the Handbook's 416.  So

    J(H) = Js M(H / H50),     H50 = c_M Js / (mu0 (mu_max - 1))

with M the master shape, c_M = max M(u)/u its own constant, Js from
composition and mu_max from the maker, gives an entry for any annealed grade
with a tabulated maximum permeability.  Leave-one-out, M from three curves
predicts the fourth to an RMS of 0.05-0.18 in ln H over J/Js 0.3-0.84 given
that curve's own Js and mu_max (printed by main()).

Master.  Each member curve, from 0.1 T up (see below), is divided by its Js
and H50 and carried past its last point by nife_build.tail() toward J/Js =
1; the four are averaged in ln u at fixed J/Js (nife_build.pool), and ln M
is smoothed in log u at the narrowest Gaussian with which every entry
passes the gates.  Below the lowest common point each entry is a straight
line from the origin.  The Handbook 416 curve is used to 100 Oe, the
figure's own limit.  Points below 0.1 T are not used: Valbruna's charts
have linear axes on which a pixel is 5 mT, and Carpenter's curves begin at
0.05 T with a foot flatter than a curve from the origin can be.

Saturation from composition, carbon as chromium carbide.  In annealed
chromium steels the carbon is in (Cr,Fe)23C6, taken here as Cr23C6 (5.68%
C, 94.32% Cr by mass): a mass fraction f = C/0.0568 of carbide, carrying
0.9432 f of the chromium, and a ferrite matrix of the rest:

    Js = (1 - f) JS_FE - K_CR (Cr - 0.9432 f) - K_SI Si - K_MN Mn   (wt%)

JS_FE and K_MN as for the low-carbon steels (bhlib), K_SI Exner's as for
the silicon steels (silib), K_CR = 0.0306 T/wt% the slope of Bozorth's
saturation of iron-chromium alloys at 20 C (Ferromagnetism, Fig. 7-23,
p. 229, read as 21.68 - 0.306 Cr kG over 2-32% Cr).  Check: Tavares et al.
measured 180.2 A m2/kg at 5 T on annealed 420 of 13.5 Cr, 0.40 C, which at
their density of 7.8 g/cm3 is Js = 1.766 T; the formula gives 1.796 T with
no Si or Mn, 1.772 T with 420's typical 0.3 Si and 0.45 Mn, where treating
the carbon as cementite would give 1.68 T.  Ni, Mo, S, P, N are not
corrected for (together under 0.02 T for these grades).

Compositions are the manufacturers' analyses of Oxley et al.'s bars (J.
Magn. Magn. Mater. 321 (2009), Table 1), except the 12Cr grade (Carpenter's
mid-range) and MG2 (Valbruna's mid-range); resistivities are Oxley's
Table 2, Carpenter's (12Cr) and Valbruna's (MG2).  Maximum permeabilities
are Carpenter's Blue Book Table 1 (fully annealed bar, Fahy permeameter)
except where a member curve gives its own: 416 755 (Handbook curve; Blue
Book 750); 12Cr 4,064 (Carpenter's chart; its table says 3,100); 430FR
2,285, the geometric mean of Carpenter's chart (2,838; table 1,800) and
Valbruna's MG2 table (1,840).

Cold drawn: Valbruna's MG2 cold-drawn curve, which does not scale onto M
below Js/2, is kept as its own curve and follows the 430FR entry above its
last point (5000 A/m), the gap decaying as 1/H (nife_build.offset_tail).

Usage:  python ss_build.py   -> femm_ss_<tag>.csv, ss_fit.dat,
                                out/ss_fit.png, out/ss_fit.json
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
from nife_build import pool, smooth

MU0 = 4e-7 * np.pi
HGRID = np.concatenate([[0.0], np.logspace(np.log10(5.0), np.log10(4.0e5),
                                           70)])
K_CR = 0.0306       # T/wt%, Bozorth Fig. 7-23 (see docstring)
C_IN_CARBIDE = 6 * 12.011 / (23 * 51.996 + 6 * 12.011)    # 0.0568
CR_IN_CARBIDE = 1 - C_IN_CARBIDE                          # 0.9432
J_MIN = 0.10


def js_of(cr, si, mn, c):
    f = c / 100.0 / C_IN_CARBIDE           # carbide mass fraction
    return ((1 - f) * L.JS_FE - K_CR * (cr - 100.0 * CR_IN_CARBIDE * f)
            - S.K_SI * si - L.K_MN * mn)


# compositions: (Cr, Si, Mn, C) wt%
OX = {"410": (11.7, 0.28, 0.45, 0.13), "416": (12.2, 0.45, 1.14, 0.12),
      "420": (12.2, 0.32, 0.45, 0.34), "430F": (17.5, 0.40, 0.45, 0.027),
      "430FR": (17.6, 1.34, 0.42, 0.029), "446": (25.8, 0.53, 0.72, 0.063)}
CC12 = (12.0, 0.5, 0.45, 0.02)       # Carpenter Chrome Core 12-FM mid-range
MG2 = (17.75, 1.35, 0.45, 0.03)      # Valbruna MG2 mid-range

# the four member curves of the master shape: (label, source key, comp)
MEMBERS = [("Carpenter Chrome Core 12-FM", "Chrome Core 12-FM", CC12),
           ("Carpenter 430FR Solenoid Quality", "430FR Solenoid Quality",
            OX["430FR"]),
           ("Valbruna MG2 magnetic annealed", "MG2 magnetic annealed", MG2),
           ("Metals Handbook 416 annealed", "MH416", OX["416"])]

ENTRIES = [
    dict(tag="410_ann", name="Stainless 410, annealed", comp=OX["410"],
         mu=750, rho=57.0),
    dict(tag="416_ann", name="Stainless 416, annealed", comp=OX["416"],
         mu=755, rho=57.0, member="Metals Handbook 416 annealed"),
    dict(tag="420_ann", name="Stainless 420, annealed", comp=OX["420"],
         mu=950, rho=55.0),
    dict(tag="12Cr_ann", name="Stainless 12Cr ferritic, annealed", comp=CC12,
         mu=4064, rho=57.0, member="Carpenter Chrome Core 12-FM"),
    dict(tag="430F_ann", name="Stainless 430F, annealed", comp=OX["430F"],
         mu=1800, rho=60.0),
    dict(tag="430FR_ann", name="Stainless 430FR, annealed", comp=OX["430FR"],
         mu=round(np.sqrt(2838 * 1840)), rho=76.0,
         member=("Carpenter 430FR Solenoid Quality",
                 "Valbruna MG2 magnetic annealed")),
    dict(tag="446_ann", name="Stainless 446, annealed", comp=OX["446"],
         mu=1000, rho=64.0),
]
COLD = dict(tag="430FR_cd", name="Stainless 430FR, cold drawn", rho=76.0,
            ref="430FR_ann", curve="MG2 cold drawn")


def curve(key):
    if key == "MH416":
        z = np.loadtxt("out/_mhzip/matl18.txt")[1:]
        z = z[z[:, 0] <= 7958.0]
        return z[:, 0], z[:, 1] - MU0 * z[:, 0]
    rows = [r for r in csv.DictReader(open("out/ss_curves.csv"))
            if r["curve"] == key]
    h = np.array([float(r["H_Am"]) for r in rows])
    return h, np.array([float(r["B_T"]) for r in rows]) - MU0 * h


def members():
    """Each member as (u, x) = (H/H50, J/Js), from 0.1 T up."""
    out = {}
    for lab, key, comp in MEMBERS:
        h, j = curve(key)
        k = j + MU0 * h >= J_MIN
        h, j = h[k], np.maximum.accumulate(j[k])
        js = js_of(*comp)
        x = j / js
        h50 = float(np.interp(0.5, x, h))
        mu = (j + MU0 * h) / (MU0 * h)
        out[lab] = dict(h=h, j=j, js=js, h50=h50, u=h / h50, x=x,
                        mu_max=float(mu.max()))
    return out


def master(mem, sigma):
    sets = {lab: list(zip(m["u"], m["x"])) for lab, m in mem.items()}
    u, x, ns = pool(sets, 1.0)
    if sigma > 0:
        u, x = smooth(u, x, sigma)
    return u, x, ns


def entry_b(u, x, js, mu):
    cm = float(np.max(x / u))
    h50 = cm * js / (MU0 * (mu - 1))
    uq = HGRID[1:] / h50
    jg = js * np.interp(np.log(uq), np.log(u), x)
    lo = uq < u[0]
    jg[lo] = js * x[0] * uq[lo] / u[0]
    return np.concatenate([[0.0], jg + MU0 * HGRID[1:]]), h50


def gate_ok(b):
    dmin, turns, murat = L.gate(b, HGRID)
    return bool((np.diff(b) > 0).all() and dmin >= 0.999 and turns <= 1
                and murat < 1)


def cold_drawn(ref_b):
    h, j = curve(COLD["curve"])
    k = j + MU0 * h >= J_MIN
    h, j = h[k], np.maximum.accumulate(j[k])
    ref = ref_b[1:] - MU0 * HGRID[1:]
    dj0 = j[-1] - np.interp(np.log(h[-1]), np.log(HGRID[1:]), ref)
    up = HGRID[1:] > h[-1] * 1.001
    he, ht = h[-1], HGRID[1:][up]
    h = np.concatenate([h, ht])
    j = np.concatenate([j, ref[up] + dj0 * he / ht])
    kk = np.concatenate([[True], j[1:] > np.maximum.accumulate(j)[:-1]])
    h, j = h[kk], j[kk]
    for sigma in (0.0, 0.02, 0.05, 0.07, 0.1):
        hs, jsm = smooth(h, j, sigma)
        jg = np.interp(np.log(HGRID[1:]), np.log(hs), jsm)
        lo = HGRID[1:] < hs[0]
        jg[lo] = jsm[0] * HGRID[1:][lo] / hs[0]
        b = np.concatenate([[0.0], jg + MU0 * HGRID[1:]])
        if gate_ok(b):
            break
    return b, dj0, sigma, (curve(COLD["curve"]))


def main():
    mem = members()
    for sigma in (0.0, 0.01, 0.02, 0.03, 0.05, 0.07, 0.1):
        u, x, ns = master(mem, sigma)
        bs = {e["tag"]: entry_b(u, x, js_of(*e["comp"]), e["mu"])
              for e in ENTRIES}
        if all(gate_ok(b) for b, _ in bs.values()):
            break
    cm = float(np.max(x / u))
    print("master: %d members, smoothing %.2f decade, c_M = %.3f, tails %s"
          % (len(mem), sigma, cm, "/".join("%.2f" % n for n in ns)))
    for lab, m in mem.items():
        # leave-one-out: the master from the other three, this curve's own
        # Js and mu_max, over the collapsing part of the curve
        uo, xo, _ = master({k: v for k, v in mem.items() if k != lab}, sigma)
        h50 = float(np.max(xo / uo)) * m["js"] / (MU0 * (m["mu_max"] - 1))
        k = (m["x"] >= 0.3) & (m["x"] <= 0.84)
        e = np.log(m["h"][k]) - np.log(h50 * np.exp(
            np.interp(m["x"][k], xo, np.log(uo))))
        print("  member %-34s Js %.3f  H50 %4.0f A/m  mu_max %5.0f  "
              "c = %.3f  leave-one-out ln H mean %+.2f RMS %.2f"
              % (lab, m["js"], m["h50"], m["mu_max"],
                 m["mu_max"] * MU0 * m["h50"] / m["js"],
                 e.mean(), np.sqrt((e * e).mean())))
    out = dict(master=dict(u=list(u), x=list(x), c_M=cm, smooth_dec=sigma,
                           members={k: dict(js=v["js"], h50=v["h50"],
                                            mu_max=v["mu_max"])
                                    for k, v in mem.items()}))
    lines = []
    fig, ax = plt.subplots(1, 3, figsize=(21, 6))
    for e in ENTRIES:
        js = js_of(*e["comp"])
        b, h50 = bs[e["tag"]]
        mu = b[1:] / (MU0 * HGRID[1:])
        sig = round(100.0 / e["rho"], 2)
        np.savetxt("femm_ss_%s.csv" % e["tag"], np.column_stack([b, HGRID]),
                   delimiter=",", fmt="%.9f")
        lines += block(e["name"], float(mu[0]), list(zip(b, HGRID)), sig)
        rep = ""
        mems = e.get("member")
        mems = (mems,) if isinstance(mems, str) else (mems or ())
        errs = {}
        jb = b[1:] - MU0 * HGRID[1:]
        for lab in mems:
            m = mem[lab]
            for lo_, hi_ in ((0.0, 0.3), (0.3, 0.95)):
                k = (m["x"] >= lo_) & (m["x"] < hi_) & (m["j"] > jb[0])
                d = np.log(m["h"][k]) - np.interp(m["j"][k], jb,
                                                  np.log(HGRID[1:]))
                errs[(lab, lo_)] = (float(np.mean(d)), float(np.sqrt(np.mean(d * d))))
            rep += "  [%s: ln H mean/RMS %+.2f/%.2f below 0.3 Js, %+.2f/%.2f above]" % (
                lab.split()[0], *errs[(lab, 0.0)], *errs[(lab, 0.3)])
        print("%-34s Js %.3f  mu_max %5.0f (%5.0f)  H50 %4.0f  B(1k/5k/40k) "
              "%.3f/%.3f/%.3f  gate %s  Sigma %.2f%s"
              % (e["name"], js, e["mu"], mu.max(), h50,
                 *[np.interp(q, HGRID, b) for q in (1e3, 5e3, 4e4)],
                 "ok" if gate_ok(b) else "FAIL", sig, rep))
        out[e["tag"]] = dict(name=e["name"], js=js, comp=e["comp"],
                             mu_max_in=e["mu"], mu_max=float(mu.max()),
                             h50=h50, sigma_MSm=sig, gate_ok=gate_ok(b),
                             member_err={"%s|%s" % k: v for k, v in errs.items()})
        ax[2].semilogx(HGRID[1:], b[1:], lw=1.5, label=e["name"])
    bc, dj0, sgc, (hc, jc) = cold_drawn(bs["430FR_ann"][0])
    sig = round(100.0 / COLD["rho"], 2)
    np.savetxt("femm_ss_%s.csv" % COLD["tag"], np.column_stack([bc, HGRID]),
               delimiter=",", fmt="%.9f")
    muc = bc[1:] / (MU0 * HGRID[1:])
    lines += block(COLD["name"], float(muc[0]), list(zip(bc, HGRID)), sig)
    print("%-34s from Valbruna MG2 cold drawn, offset at 5 kA/m %+.3f T, "
          "smooth %.2f, mu_max %4.0f, B(1k/5k/40k) %.3f/%.3f/%.3f  gate %s"
          % (COLD["name"], dj0, sgc, muc.max(),
             *[np.interp(q, HGRID, bc) for q in (1e3, 5e3, 4e4)],
             "ok" if gate_ok(bc) else "FAIL"))
    out[COLD["tag"]] = dict(name=COLD["name"], offset_T=dj0, smooth_dec=sgc,
                            mu_max=float(muc.max()), gate_ok=gate_ok(bc),
                            sigma_MSm=sig)
    ax[2].semilogx(HGRID[1:], bc[1:], "k--", lw=1.5, label=COLD["name"])

    # master and its members, normalized
    for lab, m in mem.items():
        ax[0].semilogx(m["u"], m["x"], ".", ms=3, label=lab)
    ax[0].semilogx(u, x, "k-", lw=1.5, label="master M(u)")
    ax[0].set_xlim(0.05, 1e3)
    ax[0].set_ylim(0, 1.02)
    ax[0].set_xlabel("u = H / H50")
    ax[0].set_ylabel("J / Js")
    ax[0].set_title("Four annealed curves, normalized, and the master shape",
                    fontsize=10)
    for lab, m in mem.items():
        d = np.log(m["u"]) - np.interp(m["x"], x, np.log(u))
        ax[1].plot(m["x"], d, ".", ms=3, label=lab)
    ax[1].axhline(0, color="k", lw=0.6)
    ax[1].set_ylim(-0.6, 0.6)
    ax[1].set_xlabel("J / Js")
    ax[1].set_ylabel("ln u(member) - ln u(master) at fixed J/Js")
    ax[1].set_title("Members against the master", fontsize=10)
    ax[2].set_xlim(10, 4e5)
    ax[2].set_ylim(0, 2.0)
    ax[2].set_xlabel("H [A/m]")
    ax[2].set_ylabel("B [T]")
    ax[2].set_title("The entries", fontsize=10)
    for a in ax:
        a.grid(True, which="both", alpha=0.3)
        a.legend(fontsize=7, loc="upper left")
    fig.tight_layout()
    fig.savefig("out/ss_fit.png", dpi=95)
    with open("ss_fit.dat", "w", newline="\r\n") as f:
        f.write("\n".join(["<BeginFolder>",
                           '<FolderName> = "Ferritic Stainless (vendor data)"']
                          + lines + ["<EndFolder>"]) + "\n")
    json.dump(out, open("out/ss_fit.json", "w"), indent=1)
    print("wrote femm_ss_*.csv, ss_fit.dat, out/ss_fit.png, out/ss_fit.json")


if __name__ == "__main__":
    main()
