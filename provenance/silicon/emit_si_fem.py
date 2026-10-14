# SPDX-License-Identifier: MIT
"""emit_si_fem.py -- the silicon family as a geometry-free FEMM problem file.

Output: silicon_fit_materials.fem, here in the project.  Nothing is
written into the FEMM tree, and the stock matlib.dat is not touched by
anything here.

This is the file meant to be OPENED AND READ, so it carries more than the
.dat library does:

  10  regressed entries, one per catalog grade      (same as the .dat)
  10  measured entries, the pages extended          (same as the .dat)
   5  COMPOSITION-LAW entries at silicon contents the book never shows

The third group is what the regression buys over the ten pages on their
own.  Each is the family evaluated with the per-grade residuals eW and eS
set to zero, i.e. the curve the law predicts for that silicon at average
processing.  They are named so they cannot be mistaken for a grade, and
they are the honest form of "what would 1.5% silicon look like": the
answer carries no processing information, because the book records none.

Round-trip through femm_headless is checked by dev/verify_roundtrip.py.
"""
import numpy as np

import bhlib as L
import emit_si_matlib as E
import silib as S
import simodel as M

OUT = "silicon_fit_materials.fem"     # in the project, not the FEMM tree
F = M.SiFamily(E.PARAMS)
rows = S.table()
HGRID = E.HGRID

# Silicon contents to show the law at.  Chosen to span the book's own range
# (0.39 to 3.49) and to fall BETWEEN the grades, not on them.
LAW = [0.60, 1.40, 1.75, 2.70, 3.10]

COMMENT = (
    "Silicon Steel: fit to the fourteen DC magnetization curves of US Steel, "
    "Nonoriented Sheet Steel for Magnetic Applications, ADUSS 31-7103-01 "
    "(May 1978). Four-process model (reversible toe + Burr-XII knee + hard "
    "wall shoulder + Akulov-constrained rotation); silicon from the book's "
    "own resistivity via its p.116 formula; Js = 2.151 - 0.048 Si-eq "
    "(pure iron less the book's example 0.005 C and 0.25 Mn; Exner); "
    "K1(Si) from Bozorth Ferromagnetism Fig. 12-16 sets the rotation field "
    "scale, which the pages accept at no cost but do not pin. M-15 is "
    "not in the catalog: it is the composition law at the 50 uOhm-cm of "
    "the Cleveland-Cliffs DI-MAX bulletin (Si-eq 3.32), checked against "
    "Armco's 1984 DI-MAX M-15 sheet. "
    "B IS ON A NET-METAL BASIS: the catalog computes induction from "
    "specimen mass over assumed density, so the lamination factor is NOT "
    "in these curves, and LamFill is a suggested stacking factor by gauge "
    "(midpoint of the femm.info stacking-factor table: 0.955 at 14 mil, "
    "0.965 at 18-18.5 mil, 0.975 at 24-25 mil) -- set your own if you "
    "know it. Sigma and d_lam are per grade from the book. "
    "Materials only, no geometry. See sicurves.pdf.")


def mat_block(name, bb, hh, sigma, d_lam):
    return "\n".join(E.block(name, bb, hh, sigma, d_lam, indent="  "))


blocks, n = [], {}
for grp, (folder, want) in enumerate((("regressed", 0), ("measured", 1))):
    for f_, nm, bb, hh, sig, dl in E.entries():
        if f_ == want:
            blocks.append(mat_block(nm, bb, hh, sig, dl))
    n[folder] = len(blocks)

print("regressed entries: %d" % n["regressed"])
print("measured entries:  %d" % (n["measured"] - n["regressed"]))

print("\ncomposition law at silicon contents the book does not show:")
for sieq in LAW:
    b = F.b_law(sieq, 0, HGRID)
    nm = "Si-eq %.2f pct law only (no grade, USS 1978 fit)" % sieq
    blocks.append(mat_block(nm, b, HGRID, S.sigma(S.RHO0 + S.RHO_SI * sieq),
                            0.470))
    print("  %-46s Js=%.4f  mu_init=%6.0f  B(1000 Oe)=%.4f  "
          "Sigma=%.2f MS/m"
          % (nm, S.js(sieq), b[1] / (L.MU0 * HGRID[1]),
             float(F.b_law(sieq, 0, 1000 * L.OE)),
             S.sigma(S.RHO0 + S.RHO_SI * sieq)))

fem = "\n".join([
    "[Format]      =  4.0",
    "[Frequency]   =  0",
    "[Precision]   =  1e-008",
    "[MinAngle]    =  30",
    "[Depth]       =  1",
    "[LengthUnits] =  millimeters",
    "[ProblemType] =  planar",
    "[Coordinates] =  cartesian",
    "[ACSolver]    =  0",
    f'[Comment]     =  "{COMMENT}"',
    "[PointProps]   = 0",
    "[BdryProps]   = 0",
    f"[BlockProps]  = {len(blocks)}",
    *blocks,
    "[CircuitProps]  = 0",
    "[NumPoints] = 0",
    "[NumSegments] = 0",
    "[NumArcSegments] = 0",
    "[NumHoles] = 0",
    "[NumBlockLabels] = 0",
]) + "\n"

with open(OUT, "w", newline="\r\n") as f:
    f.write(fem)
print("\nwrote %s (%d lines, %d material blocks)"
      % (OUT, fem.count("\n"), len(blocks)))
