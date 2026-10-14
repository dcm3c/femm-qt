# SPDX-License-Identifier: MIT
"""make_nifefig.py -- the anneal figure for the nickel-iron entries.

Left: B against H for the four entries, with Carpenter's intermediate
hydrogen anneals of High Permeability 49 (1450, 1600, 1750 F; not entries)
between the as-received and fully annealed 48Ni curves.  Right: relative
permeability B/(mu0 H) against B, where the anneal's effect is plainest.

Usage:  python make_nifefig.py   -> out/nifecurves_fig1.png
"""
import csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

MU0 = 4e-7 * np.pi
ENT = [("48Ni_H2", "48Ni, hydrogen anneal", "#1f3f8f", "-"),
       ("48Ni_mill", "48Ni, mill anneal (as received)", "#1f3f8f", "--"),
       ("80Ni4Mo_H2", "80Ni-4Mo, hydrogen anneal", "#d95f02", "-"),
       ("80Ni5Mo_H2", "80Ni-5Mo, hydrogen anneal", "#7570b3", "-")]
R = list(csv.DictReader(open("out/nife_curves.csv")))

fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
for c in ("1450F", "1600F", "1750F"):
    h = np.array([float(r["H_Am"]) for r in R
                  if r["source"] == "Carpenter HP49" and r["curve"] == c])
    b = np.array([float(r["B_T"]) for r in R
                  if r["source"] == "Carpenter HP49" and r["curve"] == c])
    lab = "48Ni, H2 1450, 1600, 1750 F (Carpenter; not entries)"
    ax[0].semilogx(h, b, ":", color="gray", lw=1.1,
                   label=lab if c == "1450F" else None)
    ax[1].semilogx(b, b / (MU0 * h), ":", color="gray", lw=1.1)
ax[0].plot([], [], ":", color="gray")
for tag, lab, col, ls in ENT:
    d = np.loadtxt("femm_nife_%s.csv" % tag, delimiter=",")[1:]
    b, h = d[:, 0], d[:, 1]
    ax[0].semilogx(h, b, ls, color=col, lw=2, label=lab)
    k = b > 0.01
    ax[1].semilogy(b[k], b[k] / (MU0 * h[k]), ls, color=col, lw=2, label=lab)
ax[0].set_xlim(0.05, 1e5)
ax[0].set_ylim(0, 1.8)
ax[0].set_xlabel("H [A/m]")
ax[0].set_ylabel("B [T]")
ax[0].legend(fontsize=8, loc="upper left")
ax[1].set_xscale("linear")
ax[1].set_xlim(0, 1.7)
ax[1].set_ylim(1e3, 1e6)
ax[1].set_xlabel("B [T]")
ax[1].set_ylabel("relative permeability B / (mu0 H)")
ax[1].legend(fontsize=8, loc="upper right")
for a in ax:
    a.grid(True, which="both", alpha=0.3)
fig.tight_layout()
fig.savefig("out/nifecurves_fig1.png", dpi=110)
print("wrote out/nifecurves_fig1.png")
