# SPDX-License-Identifier: MIT
"""make_metglasfig.py -- figure and numbers for metglascurves.tex.

Left panels: each alloy's two loops (upper halves), the 80 A/m loop's centre
line, the measured loop tips and the entry, on a linear field axis to 80 A/m.
Right panel: above 80 A/m, the entries, their bounds (J held at the 80 A/m
tip; J = Js) and, for comparison, tails of the form Js - (Js - Je)(He/H)^n
with n = 1 and 2 joined in value only.  Prints the numbers the paper quotes.

Usage:  python make_metglasfig.py   (after dig_metglas.py, metglas_build.py)
        -> out/metglascurves_fig1.png
"""
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import metglas_build as M

MU0 = M.MU0
meta = json.load(open("out/metglas_fit.json"))
fig = plt.figure(figsize=(14, 5.2))
gs = fig.add_gridspec(1, 3)
axr = fig.add_subplot(gs[0, 2])
cols = {"2605SA1": "tab:blue", "2605HB1M": "tab:green"}
for c, e in enumerate(M.ENTRIES):
    tag = e["tag"]
    t = np.loadtxt("femm_metglas_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    v80, v8 = M.loop(tag, "Hm80"), M.loop(tag, "Hm8")
    tip8, tip80 = v8[v8[:, 1].argmax()], v80[v80[:, 1].argmax()]
    bq = np.arange(0.2, tip80[1] - 0.004, 0.002)
    lo, hi = M.branches(v80, bq)
    hc = 0.5 * (lo + hi)
    a = fig.add_subplot(gs[0, c])
    a.plot(v80[:, 0], v80[:, 1], ",", color="0.55")
    a.plot(v8[:, 0], v8[:, 1], ",", color="0.75")
    a.plot(hc, bq, "--", color="tab:orange", lw=1.2, label="80 A/m loop centre line")
    a.plot(h, b, "k-", lw=1.6, label="entry")
    a.plot([tip8[0], tip80[0]], [tip8[1], tip80[1]], "ko", mfc="none", ms=7, label="loop tips")
    a.set_xlim(-3, 82)
    a.set_ylim(0, 1.7)
    a.grid(alpha=0.3)
    a.set_xlabel("H [A/m]")
    a.set_ylabel("B [T]")
    a.set_title("%s: loops (gray), centre line, entry" % e["name"], fontsize=9)
    a.legend(fontsize=8, loc="lower right")
    # numbers
    for bb in (1.2, 1.3, 1.35):
        print("%s  B %.2f T: centre line %.2f A/m, ascending %.2f, entry %.2f A/m"
              % (tag, bb, np.interp(bb, bq, hc), np.interp(bb, bq, hi), np.interp(bb, b, h)))
    hs = [1, 4, 10, 40, 80, 800, 8000, 80000]
    print("%s  B at %s A/m: %s" % (tag, hs, " ".join("%.3f" % np.interp(x, h, b) for x in hs)))
    mu = b[1:] / (MU0 * h[1:])
    print("%s  mu_max %.0f at %.2f A/m (B %.3f T); first-point mu %.0f"
          % (tag, mu.max(), h[1:][mu.argmax()], b[1:][mu.argmax()], mu[0]))
    # right panel: tail region
    js, je, he = e["js"], tip80[1] - MU0 * tip80[0], tip80[0]
    hh = np.logspace(np.log10(50), 5, 300)
    axr.semilogx(hh, np.interp(hh, h, b), "-", color=cols[tag], lw=1.6, label=tag + " entry")
    up = hh >= he
    axr.fill_between(hh[up], je + MU0 * hh[up], js + MU0 * hh[up], color=cols[tag], alpha=0.12)
    for n, ls in ((1, ":"), (2, "-.")):
        jt = js - (js - je) * (he / hh[up]) ** n
        axr.semilogx(hh[up], jt + MU0 * hh[up], ls, color=cols[tag], lw=1)
        for x in (300, 1000, 10000):
            print("%s  n=%d tail at %g A/m: B %.3f vs entry %.3f"
                  % (tag, n, x, js - (js - je) * (he / x) ** n + MU0 * x, np.interp(x, h, b)))
    axr.axhline(js, color=cols[tag], lw=0.6, ls="--")
axr.set_xlim(50, 1e5)
axr.set_ylim(1.38, 1.78)
axr.grid(True, which="both", alpha=0.3)
axr.set_xlabel("H [A/m]")
axr.set_ylabel("B [T]")
axr.set_title("Above 80 A/m: entries, bounds (shaded),\n1/H (dotted) and 1/H$^2$ (dash-dot) tails", fontsize=9)
axr.legend(fontsize=8, loc="upper left")
fig.tight_layout()
fig.savefig("out/metglascurves_fig1.png", dpi=110)
print("wrote out/metglascurves_fig1.png")
