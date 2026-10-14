# SPDX-License-Identifier: MIT
"""make_cofefig.py -- the comparison figure for cofecurves.tex.

The four cobalt-iron entries together, against what FEMM's library already
carried for these alloys: the Metals Handbook Vanadium Permendur (Fig. 17
curve 2, from MetalsHandbookCurves.zip), and the stock "Hiperco-50" and
"Vanadium Permedur" entries.  Left: B against H; right: J = B - mu0 H from
1 to 400 kA/m, where the saturation levels separate.

Usage:  python make_cofefig.py   -> out/cofecurves_fig2.png
"""
import os
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

MU0 = 4e-7 * np.pi
# FEMM 4.2's own library, for the stock entries drawn beside the new ones;
# optional -- without it (as in the distributed package) they are left out
LIB = "matlibs/matlib_femm42.dat"
HAVE_STOCK = os.path.exists(LIB)
t = open(LIB, encoding="utf-8", errors="replace").read() if HAVE_STOCK else ""


def stock(name):
    m = re.search(r'<BlockName> = "%s"(.*?)<EndBlock>' % re.escape(name), t,
                  re.S).group(1)
    n = int(re.search(r"<BHPoints> = (\d+)", m).group(1))
    a = np.array([l.split() for l in m[m.index("<BHPoints>"):].splitlines()[1:]
                  if l.strip()][:n], float)[1:]
    return a[:, 1], a[:, 0]


ENT = [("49Co2V_mag", "49Co-2V, magnetic anneal", "#1f3f8f"),
       ("49Co2V_rotor", "49Co-2V, rotor grade", "#4a90d9"),
       ("49Co2V_hs", "49Co-2V, high strength", "#9ecae1"),
       ("27Co", "27Co", "#d95f02")]
z = np.loadtxt("out/_mhzip/matl02.txt")[1:]
OTHER = [((z[:, 0], z[:, 1]), "Metals Handbook Vanadium Permendur", "k", "--")]
if HAVE_STOCK:
    OTHER += [(stock("Hiperco-50"), 'FEMM stock "Hiperco-50"', "gray", ":"),
              (stock("Vanadium Permedur"), 'FEMM stock "Vanadium Permedur"',
               "gray", "-.")]

fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
for tag, lab, c in ENT:
    d = np.loadtxt("femm_cofe_%s.csv" % tag, delimiter=",")[1:]
    h, b = d[:, 1], d[:, 0]
    ax[0].semilogx(h, b, "-", color=c, lw=2, label=lab)
    k = h >= 1e3
    ax[1].semilogx(h[k], b[k] - MU0 * h[k], "-", color=c, lw=2, label=lab)
for (h, b), lab, c, ls in OTHER:
    ax[0].semilogx(h, b, ls, color=c, lw=1.2, label=lab)
    k = h >= 1e3
    ax[1].semilogx(h[k], b[k] - MU0 * h[k], ls, color=c, lw=1.2, label=lab)
ax[0].set_xlim(10, 4e5)
ax[0].set_ylim(0, 2.9)
ax[0].set_ylabel("B [T]")
ax[1].set_xlim(1e3, 4e5)
ax[1].set_ylim(1.8, 2.45)
ax[1].set_ylabel("J = B - mu0 H [T]")
for a in ax:
    a.set_xlabel("H [A/m]")
    a.grid(True, which="both", alpha=0.3)
    a.legend(fontsize=7, loc="lower right")
fig.tight_layout()
fig.savefig("out/cofecurves_fig2.png", dpi=110)
print("wrote out/cofecurves_fig2.png")
