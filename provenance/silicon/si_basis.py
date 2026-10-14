# SPDX-License-Identifier: MIT
"""si_basis.py -- what "B" means on the US Steel catalog pages, and the two
systematic biases that follow from it.

The question this answers: fully processed non-oriented sheet carries a
core-plate coating (C-5 and relatives), which takes up space in a stack.
Is the catalog's B already diluted by that -- i.e. is the lamination factor
baked into the curve -- and is the coating the reason the pages run low at
the top?

NO, on both counts, and the book says so itself.

BASIS.  Book p.115 ("Density of steel"): "Density is necessary for magnetics
testing and core design calculations", and it then quotes the ASTM table of
ASSUMED DENSITIES by silicon-plus-1.7-aluminum band -- which is exactly the
set of densities the book states per grade.  The same house says it in full
in the modern bulletin for the descendant product line (Cleveland-Cliffs,
DI-MAX Non-Oriented Electrical Steels, Apr 2026, "Testing Methods"):

    "Induction is calculated from the net cross-sectional area of the
     specimen.  Net area is determined from the actual weight of the
     specimen and the assumed specific gravity values (as specified by
     ASTM).  Designers may approximate net area by multiplying gross area
     of the compacted core by its lamination factor, expressed as a
     decimal."

So the area is m / (l * delta), a NET metal area.  The coating's THICKNESS
never enters it, and the lamination factor is explicitly the designer's own
separate multiplier.  The emitted tables are therefore solid-metal B-H
curves, which is the basis a FEMM material wants: the stacking factor is
applied by the user, and applying it is not double-counting.

WHAT THE COATING DOES DO.  "Actual weight of the specimen" includes the
coating, while the density divided into it is the steel's.  So the net area
is overstated by the coating's MASS fraction and B is understated by the
same fraction.  That is a mass ratio, not a thickness ratio, and it is
roughly an order of magnitude smaller: a few g/m^2 of coating against
2.7-3.7 kg/m^2 of steel.  No coating weight is stated in either the book or
the modern bulletin (which says only that C-5 "has little effect on
lamination factor"), so this script prints a sensitivity rather than a
number pretending to a source.

THE OTHER BIAS, same convention.  The assumed density is banded in 0.05
g/cm^3 steps, so it differs from the grade's own formula density; that
error goes into the area directly and with the opposite sign.

Neither bias explains the SCATTER in how low the pages run.  The two
uncoated grades (Q-Core, Type 2-S -- lamination sheet, annealed by the
customer) are the two worst at +1.8 and +2.6%, and the coated silicon
grades average +0.8%.  Whatever makes M-43 run 2.7% low is not its coating.

Usage:  python si_basis.py
"""
import numpy as np

import silib as S

MIL = 25.4e-3                       # mm per mil
COAT = [1.0, 3.0, 6.0]              # g/m^2, TOTAL of both sides


def formula_density(sieq, al):
    """Book p.115: density = 7.865 - 0.065 (Si + 1.7 Al), with
    Si = sieq - 1.14 Al, so Si + 1.7 Al = sieq + 0.56 Al."""
    return 7.865 - 0.065 * (sieq + (1.7 - S.RHO_AL / S.RHO_SI) * al)


rows = S.table()
print("B on the catalog pages is flux / net metal area, net area = specimen "
      "mass / assumed density.\nSo the coating enters as MASS, not as "
      "thickness, and the lamination factor is not in the curve.\n")

print("%-9s %5s %8s   %-22s   %-24s  %7s"
      % ("grade", "gauge", "steel", "coating mass fraction",
         "assumed vs formula rho", "page"))
print("%-9s %5s %8s   %-22s   %-24s  %7s"
      % ("", "mil", "g/m^2", "  ".join("%4.0fg" % c for c in COAT),
         "   Al=0     Al=max    ", "low %"))
tot = []
for r in rows:
    areal = r["mils"] * MIL * r["dens"] * 1000.0        # g/m^2 of steel
    fr = ["%+.3f%%" % (100 * c / areal) for c in COAT]
    al_hi = S.al_bound(r["sieq"], r["band"])
    d0 = 100 * (r["dens"] / formula_density(r["sieq"], 0.0) - 1)
    d1 = 100 * (r["dens"] / formula_density(r["sieq"], al_hi) - 1)
    low = 100 * (r["js_rho"] / r["js_curve"] - 1)
    tot.append((100 * COAT[1] / areal, d0, d1, low))
    print("%-9s %5.1f %8.0f   %-22s   %+7.3f%% %+8.3f%%   %+6.2f"
          % (r["name"], r["mils"], areal, "  ".join(fr), d0, d1, low))

c3, d0, d1, low = (np.array(x) for x in zip(*tot))
print("\nAt 3 g/m^2 of core plate the coating biases B LOW by %.2f-%.2f%%.  "
      "The density band biases\nit HIGH by %+.2f to %+.2f%% (Al from 0 to its "
      "upper bound), so the two partly cancel.\nBoth are far under the %+.2f%% "
      "median shortfall, and neither has the right pattern:"
      % (c3.min(), c3.max(), min(d0.min(), d1.min()),
         max(d0.max(), d1.max()), np.median(low)))
co = np.array([1.0 if r["cond"] is S.ASHEARED else 0.0 for r in rows])
print("  coated (%d fully processed pages): %+.2f%% mean shortfall"
      % ((co == 1).sum(), low[co == 1].mean()))
print("  uncoated (%d decarb-annealed pages): %+.2f%% mean shortfall"
      % ((co == 0).sum(), low[co == 0].mean()))
print("  correlation of shortfall with being coated: %+.2f"
      % np.corrcoef(low, co)[0, 1])
