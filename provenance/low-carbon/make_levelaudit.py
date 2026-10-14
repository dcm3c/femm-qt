# SPDX-License-Identifier: MIT
"""make_levelaudit.py -- Figure 1 of the writeup: the level audit as a bar
chart.

Same computation as dev/audit_levels.py: for every curated trace, over
its points above 150 Oe, the saturation the trace implies when divided
by the fitted shape for its own composition and condition (no per-curve
nuisance), expressed as a deviation from the dilution anchor.  Above
150 Oe the shape is nearly flat, so this is a B-scale check that an
H-axis error cannot mimic.  Bars beyond +1% are red: the trace
saturates above the level its composition permits, which is impossible.

Usage:  python make_levelaudit.py [params.json]   (default out/fit_params.json)
Writes levelaudit.png (the file the DokuWiki export references).
"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import bhlib as L
import bhmodel as MDL

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/fit_params.json"
F = MDL.Family(SRC)
HMIN = 150.0 * L.OE

names, devs = [], []
for cu in L.load_curves():
    sel = cu.h >= HMIN
    if sel.sum() < 2:
        continue
    s = F.shape(cu.c, cu.cond, cu.h[sel], cu.mn)
    impl = np.median(cu.j[sel] / s - cu.js_a)
    names.append(cu.name.replace(" ann ", " ").replace(" as hot rolled",
                                                       " as rolled"))
    devs.append(100 * impl / cu.js_a)

devs = np.array(devs)
fig, ax = plt.subplots(figsize=(9, 6.2))
y = np.arange(len(names))[::-1]
ax.axvspan(-1, 1, color="#d9d9d9", alpha=0.8, label="heat-to-heat scatter")
ax.barh(y, devs, color=np.where(devs > 1.0, "#d62728", "#1f77b4"),
        height=0.6)
ax.axvline(0, color="k", lw=1)
ax.set_yticks(y)
ax.set_yticklabels(names, fontsize=9)
ax.set_xlabel("implied Js deviation from dilution anchor  [%]")
ax.set_title("Saturation implied by each measured trace above 150 Oe")
ax.grid(True, axis="x", alpha=0.3)
ax.legend(loc="lower right", fontsize=9)
fig.tight_layout()
fig.savefig("levelaudit.png", dpi=120)
print("wrote levelaudit.png  (source %s, model %s)" % (SRC, F.version))
for n, d in zip(names, devs):
    print("  %-22s %+5.1f %%" % (n, d))
