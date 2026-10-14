# SPDX-License-Identifier: MIT
"""compare_dimax.py -- Armco (1984) against US Steel (1978).

Armco sheets: *ARMCO Nonoriented Electrical Steels*, ARMCO Specialty
Steels Division, Middletown, OH, 1984 -- the DI-MAX grades.

The first genuinely independent test this silicon family has had.

Armco was a COMPETITOR of US Steel, not its successor: American Rolling
Mill Co -> Armco -> AK Steel -> Cleveland-Cliffs, whose modern DI-MAX
bulletin is also in SiliconSteel/.  US Steel is a separate company.  That
distinction is what gives these sheets their value -- two different mills,
six years apart, measuring the same AISI grade names on their own steel.
Everything in this project so far rests on one 1978 catalog; this asks
whether another mill's M-19 looks like US Steel's M-19.

WHAT LINES UP AND WHAT DOES NOT.  Four grades can be compared like for
like, because both sheets say the specimens were tested AS SHEARED: M-19,
M-22, M-36 and M-45.  Two cannot: Armco's M-43 and M-47 sheets are
semi-processed material tested QUALITY ANNEALED, which is what that product
is sold to have done to it, and annealing removes exactly the cold-worked
edge the US Steel as-sheared specimens still carry.  Those two are shown
but must not be read as a disagreement between mills.

Armco's M-15 has no US Steel counterpart; US Steel's M-27, Stator B,
Q-Core and Type 2-S have no Armco sheet here.

THE OVERLAP IS 6 TO 20 kG.  The Armco sheets are "at High Induction" and
their frames start at 6 kG, so they say nothing about the toe; the US Steel
pages run from zero but squeeze 0 to 28 kG onto one page.  Each is strong
where the other is weak, and the comparison is made where both are good.

Usage:  python compare_dimax.py
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
import silib as S
import simodel as M

F = M.SiFamily("out/si_params.json")
rows = {r["name"]: r for r in S.table()}
LAW = {g["name"]: g for g in S.law_grades()}
BG = np.arange(8.0, 19.1, 2.0)          # kilogausses; 20 is the Armco frame edge
HG = np.array([80., 200., 500., 2000., 8000., 30000.])   # A/m

# Armco grade -> (US Steel key, comparable?), with the reason when not
PAIR = [("M-19", "M19", True, ""),
        ("M-22", "M22", True, ""),
        ("M-36", "M36", True, ""),
        ("M-45", "M45", True, ""),
        ("M-43", "M43", False, "Armco sheet is SP tested quality-annealed"),
        ("M-47", "M47", False, "Armco sheet is SP tested quality-annealed"),
        ("M-15", None, False, "no US Steel M-15 page")]


def armco(g):
    d = np.loadtxt("out/dimax_%s_raw.csv" % g.replace("-", ""),
                   delimiter=",", skiprows=1)
    return d[:, 0] * L.OE, d[:, 1] * L.KG          # H A/m, B T


def uss(key):
    h, b, _ = S.load_raw(key)
    return h, b


def h_at(h, b, bt):
    if bt < b[0] or bt > b[-1]:
        return np.nan
    return float(np.interp(bt, b, h))


print("H at fixed induction, A/m.  ratio = Armco / US Steel;  >1 means the")
print("Armco steel needed MORE field, i.e. it is the harder of the two.\n")
print("%-6s %-9s%s" % ("grade", "source",
                       "".join("%11s" % ("%.0f kG" % x) for x in BG)))
res = {}
for g, key, ok, why in PAIR:
    ha_, ba_ = armco(g)
    va = [h_at(ha_, ba_, x * L.KG) for x in BG]
    print("%-6s %-9s%s" % (g, "Armco",
                           "".join("%11.1f" % v for v in va)))
    if key is None:
        print("%-6s %-9s%s" % ("", "(" + why + ")", ""))
        continue
    hu, bu = uss(key)
    vu = [h_at(hu, bu, x * L.KG) for x in BG]
    print("%-6s %-9s%s" % ("", "USS'78", "".join("%11.1f" % v for v in vu)))
    rr = np.array(va) / np.array(vu)
    res[g] = (rr, ok)
    print("%-6s %-9s%s%s" % ("", "ratio",
                             "".join("%11.2f" % v for v in rr),
                             "" if ok else "   <- " + why))
    print("")

good = [g for g in res if res[g][1]]
allr = np.concatenate([res[g][0] for g in good])
allr = allr[np.isfinite(allr)]
print("Over the four like-for-like grades (%s), %d comparisons:"
      % (", ".join(good), len(allr)))
print("  median %+.0f%%, range %+.0f%% to %+.0f%%; by grade: %s"
      % (100 * (np.median(allr) - 1), 100 * (allr.min() - 1),
         100 * (allr.max() - 1),
         ", ".join("%s %+.0f%%" % (g, 100 * (np.nanmedian(res[g][0]) - 1))
                   for g in good)))

# Above the knee the curve is nearly flat, so H at fixed B is the wrong
# metric -- a per cent in B becomes tens of per cent in H.  Compare B at
# fixed H there instead, which is what a FEMM solution is actually
# sensitive to.
print("\nB at fixed field, kilogausses.  Same four grades, the metric that")
print("is well conditioned above the knee:")
print("%-6s %-9s%s" % ("grade", "source",
                       "".join("%10s" % ("%.0f A/m" % x) for x in HG)))
dev = []
for g, key, ok, why in PAIR:
    if key is None or not ok:
        continue
    ha_, ba_ = armco(g)
    hu, bu = uss(key)
    va = np.interp(HG, ha_, ba_ / L.KG, left=np.nan, right=np.nan)
    vu = np.interp(HG, hu, bu / L.KG, left=np.nan, right=np.nan)
    dev.append(va - vu)
    print("%-6s %-9s%s" % (g, "Armco", "".join("%10.2f" % v for v in va)))
    print("%-6s %-9s%s" % ("", "USS'78", "".join("%10.2f" % v for v in vu)))
    print("%-6s %-9s%s" % ("", "diff kG",
                           "".join("%10.2f" % v for v in va - vu)))
dev = np.array(dev)
print("  -> Armco minus US Steel: %+.2f kG median, %+.2f to %+.2f, i.e. "
      "%.1f%% of 20 kG"
      % (np.nanmedian(dev), np.nanmin(dev), np.nanmax(dev),
         100 * np.nanmedian(np.abs(dev)) / 20))

# M-15 has no US Steel page.  Its entry is the composition law at the
# resistivity the Cleveland-Cliffs bulletin states (silib.LAW_GRADES);
# compare it with Armco's sheet, and set that beside what the same law
# does against Armco for the four like-for-like grades.
print("\nComposition-law entries against Armco, B at fixed field (kG):")
for g, lg in LAW.items():
    ha_, ba_ = armco(g)
    d = ba_ / L.KG - F.b_law(lg["sieq"], lg["regime"], ha_) / L.KG
    print("  %-5s Si-eq %.2f (%s): Armco minus law %+.2f kG median, "
          "%+.2f to %+.2f" % (g, lg["sieq"], lg["source"], np.median(d),
                              d.min(), d.max()))
for g, key, ok, why in PAIR:
    if key is None or not ok:
        continue
    ha_, ba_ = armco(g)
    d = ba_ / L.KG - F.b_law(rows[g]["sieq"], rows[g]["regime"], ha_) / L.KG
    print("  %-5s Si-eq %.2f (USS resistivity): Armco minus law %+.2f kG "
          "median" % (g, rows[g]["sieq"], np.median(d)))

print("\nSaturation, from each mill's own composition figure:")
print("  %-6s %12s %12s %10s" % ("grade", "USS Js (T)", "Armco B(20kG pt)",
                                 "H there"))
for g, key, ok, why in PAIR:
    if key is None:
        continue
    ha_, ba_ = armco(g)
    print("  %-6s %12.4f %12.2f kG %8.0f A/m"
          % (g, rows[g]["js_rho"], ba_[-1] / L.KG, ha_[-1]))
print("  The Armco sheets stop at their frame edge, 20 kG, not at "
      "saturation,\n  so this is where each curve leaves the page, not a "
      "measured Js.")

# ------------------------------------------------------------------ figure
fig, ax = plt.subplots(2, 4, figsize=(19, 8.5), sharex=True, sharey=True)
for i, (g, key, ok, why) in enumerate(PAIR):
    a = ax[i // 4][i % 4]
    ha_, ba_ = armco(g)
    a.semilogx(ha_, ba_ / L.KG, "-", lw=1.8, color="#d95f02",
               label="Armco DI-MAX 1984")
    if key is not None:
        hu, bu = uss(key)
        k = bu >= 5.5 * L.KG
        a.semilogx(hu[k], bu[k] / L.KG, "-", lw=1.8, color="#1f3f8f",
                   label="US Steel 1978")
        hg = np.logspace(np.log10(50), np.log10(1e5), 200)
        a.semilogx(hg, F.b(key, hg) / L.KG, "--", lw=1.3, color="crimson",
                   label="our regressed entry")
    elif g in LAW:
        hg = np.logspace(np.log10(50), np.log10(1e5), 200)
        lg = LAW[g]
        a.semilogx(hg, F.b_law(lg["sieq"], lg["regime"], hg) / L.KG, "--",
                   lw=1.3, color="crimson",
                   label="our law entry, Si-eq %.2f" % lg["sieq"])
    a.set_xlim(5, 1e5)
    a.set_ylim(6, 21)
    a.grid(True, which="both", alpha=0.3)
    tag = ("" if ok else "  (no US Steel page; law at %s)"
           % LAW[g]["source"] if key is None
           else "  (conditions differ)")
    a.set_title("%s%s" % (g, tag), fontsize=10,
                color="k" if ok else "#a00")
    a.legend(fontsize=7, loc="lower right")
    if i % 4 == 0:
        a.set_ylabel("B  [kG]")
    if i >= 3:
        a.set_xlabel("H  [A/m]")
ax[1][3].axis("off")
fig.suptitle("Two mills, six years apart, same AISI grade names: Armco "
             "DI-MAX (1984) against US Steel (1978).\nOrange and blue are "
             "measurements; red dashed is the family regressed from the US "
             "Steel pages alone.  M-43 and M-47 are not like-for-like -- "
             "the Armco sheets for those are quality-annealed.", fontsize=11)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig("out/cmp_dimax.png", dpi=115)
print("\nwrote out/cmp_dimax.png")
