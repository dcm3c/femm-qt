# SPDX-License-Identifier: MIT
"""make_ironfig.py -- the figure for ironcurves.tex.

Left, ARMCO Pure Iron; right, ARMCO Telar 57.  Top row: the bulletin
curves each entry is built from, the entry itself, and the low-carbon
family (bhcurves) evaluated at the same composition in its softest
condition, 1500 F.  Bottom row: the joins, J = B - mu0 H against H from
3 to 400 kA/m, where the medium-induction curve hands over to the
high-induction one.

Usage:  python make_ironfig.py   -> out/ironcurves_fig1.png
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
import bhmodel as MDL

MU0 = L.MU0
F = MDL.Family("../low-carbon/out/fit_params.json")


def load(fn):
    d = np.loadtxt(fn, delimiter=",", skiprows=1)
    h, b = d[:, 0], d[:, 1]
    k = np.concatenate(([True], np.diff(b) > 0))
    return h[k], b[k]


def entry(tag):
    t = np.loadtxt("femm_%s.csv" % tag, delimiter=",")
    return t[1:, 1], t[1:, 0]


PANELS = [
    ("ARMCO Pure Iron", "ArmcoPureIron", 0.010, 0.10,
     [("out/pi_fig7_annealed_raw.csv", "Fig. 7, recrystallization anneal", "#1f3f8f"),
      ("out/pi_hr3mm_820C_raw.csv", "Fig. 15 C, 820 C, 3 mm", "#7a9fd6"),
      ("out/pi_fig8_high_raw.csv", "Fig. 8, high induction", "#d95f02")]),
    ("ARMCO Telar 57", "Telar57", 0.020, 0.425,
     [("out/telar_8mm_annealed_raw.csv", "Fig. 10, 8 mm bar", "#1f3f8f"),
      ("out/telar_10mm_annealed_raw.csv", "Fig. 11, 10 mm bar", "#4a6fb5"),
      ("out/telar_12mm_annealed_raw.csv", "Fig. 12, 12 mm bar", "#7a9fd6"),
      ("out/telar_fig5_high_raw.csv", "Fig. 5, high induction", "#d95f02")]),
]

fig, ax = plt.subplots(2, 2, figsize=(13, 9.5))
for col, (title, tag, c, mn, srcs) in enumerate(PANELS):
    top, bot = ax[0, col], ax[1, col]
    he, be = entry(tag)
    hm = np.logspace(0, np.log10(4e5), 600)
    bm = F.b(c, mn, 5, hm)
    for fn, lab, clr in srcs:
        h, b = load(fn)
        top.semilogx(h, b, "-", color=clr, lw=1.3, label=lab)
        k = h >= 3e3
        bot.semilogx(h[k], b[k] - MU0 * h[k], "-", color=clr, lw=1.3,
                     label=lab)
    top.semilogx(he, be, "k.", ms=5, label="library entry")
    top.semilogx(hm, bm, "--", color="crimson", lw=1.2,
                 label="low-carbon family, 1500 F, C %.3f Mn %.3f" % (c, mn))
    k = he >= 3e3
    bot.semilogx(he[k], be[k] - MU0 * he[k], "k.", ms=6,
                 label="library entry")
    k = hm >= 3e3
    bot.semilogx(hm[k], bm[k] - MU0 * hm[k], "--", color="crimson", lw=1.2,
                 label="low-carbon family, 1500 F")
    top.set_xlim(1, 4e5)
    top.set_ylim(0, 2.8)
    top.set_title(title)
    top.set_xlabel("H  [A/m]")
    top.set_ylabel("B  [T]")
    top.grid(True, which="both", alpha=0.3)
    top.legend(fontsize=8, loc="upper left")
    bot.set_xlim(3e3, 4e5)
    bot.set_ylim(1.7, 2.2)
    bot.set_xlabel("H  [A/m]")
    bot.set_ylabel("J = B - mu0 H  [T]")
    bot.grid(True, which="both", alpha=0.3)
    bot.legend(fontsize=8, loc="lower right")
fig.tight_layout()
fig.savefig("out/ironcurves_fig1.png", dpi=110)
print("wrote out/ironcurves_fig1.png")
