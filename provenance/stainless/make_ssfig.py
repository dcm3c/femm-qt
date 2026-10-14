# SPDX-License-Identifier: MIT
"""make_ssfig.py -- the stainless entries against what FEMM's library carried.

Left: B against H for the eight entries, with the stock "416 Stainless
Steel" and "430 Stainless Steel" and the Metals Handbook 416 annealed curve.
Right: relative permeability B/(mu0 H) against B.

Usage:  python make_ssfig.py   -> out/sscurves_fig1.png
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
                  if l.strip()][:n], float)
    a = a[a[:, 1] > 0]
    return a[:, 1], a[:, 0]


ENT = [("410_ann", "410, annealed", "#6baed6", "-"),
       ("416_ann", "416, annealed", "#2171b5", "-"),
       ("420_ann", "420, annealed", "#08306b", "-"),
       ("12Cr_ann", "12Cr ferritic, annealed", "#d95f02", "-"),
       ("430F_ann", "430F, annealed", "#74c476", "-"),
       ("430FR_ann", "430FR, annealed", "#238b45", "-"),
       ("446_ann", "446, annealed", "#984ea3", "-"),
       ("430FR_cd", "430FR, cold drawn", "#238b45", "--")]
z = np.loadtxt("out/_mhzip/matl18.txt")[1:]
OTHER = [((z[:, 0], z[:, 1]), "Metals Handbook 416, annealed", "k", ":")]
if HAVE_STOCK:
    OTHER += [(stock("416 Stainless Steel"), 'FEMM stock "416 Stainless Steel"',
               "gray", "-."),
              (stock("430 Stainless Steel"), 'FEMM stock "430 Stainless Steel"',
               "gray", "--")]

fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
for tag, lab, c, ls in ENT:
    d = np.loadtxt("femm_ss_%s.csv" % tag, delimiter=",")[1:]
    h, b = d[:, 1], d[:, 0]
    ax[0].semilogx(h, b, ls, color=c, lw=2, label=lab)
    k = b > 0.05
    ax[1].plot(b[k], b[k] / (MU0 * h[k]), ls, color=c, lw=2, label=lab)
for (h, b), lab, c, ls in OTHER:
    ax[0].semilogx(h, b, ls, color=c, lw=1.2, label=lab)
    k = (b > 0.05) & (h > 0)
    ax[1].plot(b[k], b[k] / (MU0 * h[k]), ls, color=c, lw=1.2, label=lab)
ax[0].set_xlim(10, 4e5)
ax[0].set_ylim(0, 2.0)
ax[0].set_xlabel("H [A/m]")
ax[0].set_ylabel("B [T]")
ax[1].set_xlim(0, 1.8)
ax[1].set_ylim(0, 4500)
ax[1].set_xlabel("B [T]")
ax[1].set_ylabel("relative permeability B / (mu0 H)")
for a in ax:
    a.grid(True, which="both", alpha=0.3)
    a.legend(fontsize=8, loc="upper left")
fig.tight_layout()
fig.savefig("out/sscurves_fig1.png", dpi=110)
print("wrote out/sscurves_fig1.png")


# --- second figure: the PH entries -----------------------------------------
PH = [("174_A", "17-4 PH, Condition A", "#6baed6", "-"),
      ("174_H900", "17-4 PH, H 900", "#2171b5", "-"),
      ("174_H1075", "17-4 PH, H 1075/H 1100", "#08306b", "-"),
      ("174_H1150", "17-4 PH, H 1150", "#d95f02", "-"),
      ("138Mo_H950", "PH 13-8 Mo, H 950", "#984ea3", "-")]

fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
for tag, lab, c, ls in PH:
    d = np.loadtxt("femm_ph_%s.csv" % tag, delimiter=",")[1:]
    h, b = d[:, 1], d[:, 0]
    ax[0].semilogx(h, b, ls, color=c, lw=2, label=lab)
    k = b > 0.05
    ax[1].plot(b[k], b[k] / (MU0 * h[k]), ls, color=c, lw=2, label=lab)
    if tag.startswith("174"):      # the 15-5 entry of the same condition
        e = np.loadtxt("femm_ph_155_%s.csv" % tag[4:], delimiter=",")[1:]
        ax[0].semilogx(e[:, 1], e[:, 0], ":", color=c, lw=1.2,
                       label="15-5 PH, " + lab.split(", ")[1])
for (h, b), lab, c, ls in ([(stock("455 Stainless Steel"),
                             'FEMM stock "455 Stainless Steel"', "gray", "-."),
                            (stock("416 Stainless Steel"),
                             'FEMM stock "416 Stainless Steel"', "gray", "--")]
                           if HAVE_STOCK else []):
    ax[0].semilogx(h, b, ls, color=c, lw=1.2, label=lab)
    k = (b > 0.05) & (h > 0)
    ax[1].plot(b[k], b[k] / (MU0 * h[k]), ls, color=c, lw=1.2, label=lab)
d = np.loadtxt("femm_ss_410_ann.csv", delimiter=",")[1:]
ax[0].semilogx(d[:, 1], d[:, 0], "-", color="k", lw=1.0,
               label="410, annealed (Fig. 1)")
k = d[:, 0] > 0.05
ax[1].plot(d[k, 0], d[k, 0] / (MU0 * d[k, 1]), "-", color="k", lw=1.0,
           label="410, annealed (Fig. 1)")
ax[0].set_xlim(10, 4e5)
ax[0].set_ylim(0, 2.0)
ax[0].set_xlabel("H [A/m]")
ax[0].set_ylabel("B [T]")
ax[1].set_xlim(0, 1.8)
ax[1].set_ylim(0, 900)
ax[1].set_xlabel("B [T]")
ax[1].set_ylabel("relative permeability B / (mu0 H)")
for a in ax:
    a.grid(True, which="both", alpha=0.3)
    a.legend(fontsize=8, loc="upper left")
fig.tight_layout()
fig.savefig("out/sscurves_fig2.png", dpi=110)
print("wrote out/sscurves_fig2.png")
