# SPDX-License-Identifier: MIT
"""emit_fem.py -- write the fitted family as a geometry-free FEMM problem
file (.fem) holding only material definitions, for view/drag-and-drop
import into the materials library.

Output: lowcarbon_fit_materials.fem, here in the project
This is the file meant to be OPENED AND READ, so it carries everything:
the model entries for all 9 grades in all FOUR conditions (the .dat
library carries only annealed-1400F and as-rolled), the 2 curated
measured entries (byte-identical to lowcarbon_fit.dat), and all 21
individual digitized AK Steel curves for side-by-side inspection.
61-point B-H tables to ~5030 Oe. Round-trip through femm_headless is
checked by dev/verify_roundtrip.py.
"""

import numpy as np
import bhlib as L
import bhmodel as MDL
import measured as M
import femm_table as T
import inventory as INV

PARAMS = "out/fit_params.json"
OUT = "lowcarbon_fit_materials.fem"   # in the project, not the FEMM tree
F = MDL.Family(PARAMS)
VER = F.version

# Armco composition from the AK Steel bulletin (Gr.2 maxima), not assumed.
GRADES = [("Armco Fe gr.2", 0.010, 0.100), ("1004", 0.040, 0.35),
          ("1006", 0.060, 0.325), ("1008", 0.080, 0.40),
          ("1010", 0.100, 0.45), ("1018", 0.175, 0.75),
          ("1117", 0.180, 1.15), ("1020", 0.200, 0.45),
          ("1030", 0.300, 0.75)]
CONDS = [(4, "annealed 1400F"), (2, "as hot rolled"),
         (3, "annealed 1300F"), (5, "annealed 1500F")]
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])

COMMENT = (f"Low Carbon Steel {VER}: unified fit to SLAC-TN-89-04 + USS "
           "datasheet data, Fermilab TM-1197 held out; toe + soft and "
           "sharp wall populations + Akulov-constrained rotation, three "
           "heat-treatment regimes split at A1; Js from composition. "
           "Materials only, no geometry. See bhcurves.pdf.")


def mat_block(name, bb, hh, sigma):
    ln = ["  <BeginBlock>",
          f'    <BlockName> = "{name}"',
          f"    <Mu_x> = {T.mu_init(bb, hh):.1f}",
          f"    <Mu_y> = {T.mu_init(bb, hh):.1f}",
          "    <H_c> = 0", "    <H_cAngle> = 0",
          "    <J_re> = 0", "    <J_im> = 0",
          f"    <Sigma> = {sigma}", "    <d_lam> = 0",
          "    <Phi_h> = 0", "    <Phi_hx> = 0", "    <Phi_hy> = 0",
          "    <LamType> = 0", "    <LamFill> = 1",
          "    <NStrands> = 0", "    <WireD> = 0",
          f"    <BHPoints> = {len(bb)}"]
    ln += [f"      {b_:.9f}\t{h_:.6f}" for b_, h_ in zip(bb, hh)]
    ln.append("  <EndBlock>")
    return "\n".join(ln)


blocks = []
print("model entries:")
for gname, c, mn in GRADES:
    for d, clab in CONDS:
        b = F.b(c, mn, d, HGRID)
        blocks.append(mat_block(f"{gname} {VER} {clab}", b, HGRID,
                                M.sigma_for(gname)))
print(f"  {len(blocks)} blocks ({len(GRADES)} grades x {len(CONDS)} "
      "conditions)")

# --- the two curated entries, byte-identical to what the .dat library ships --
print("curated (these are the entries in lowcarbon_%s.dat):" % VER)
for name, csv in INV.CURATED:
    mb, mh = M.load_table(csv)
    sig = [e["sigma"] for e in M.MEASURED if e["csv"] == csv][0]
    blocks.append(mat_block(name, mb, mh, sig))
    print("  %-40s mu_init=%7.0f  Sigma=%.2f" % (name, T.mu_init(mb, mh), sig))

# --- every individual digitized curve, for inspection ------------------------
#
# This file exists to be opened and read, so the whole measured set goes in,
# not just the two that were curated into the library. That is what makes the
# choices auditable: the 8/10/12 mm Telar scatter the curated entry averages
# over, and the five Pure Iron anneals it does not use, are all here to look
# at side by side.
print("\nindividual digitized curves:")
nbad = 0
for name, csv, js in INV.RAW:
    h_, b_ = T.load_raw(csv)
    try:
        bb, hh, akul = T.build(h_, b_, js)
    except ValueError as e:
        print("  %-40s SKIPPED: %s" % (name, e))
        nbad += 1
        continue
    blocks.append(mat_block(name, bb, hh, M.sigma_for_raw(name)))
    print("  %-40s mu_init=%7.0f  measured to %6.0f A/m  B(4e5)=%.3f"
          % (name, T.mu_init(bb, hh), h_[-1], bb[-1]))
if nbad:
    print("  (%d curve(s) could not make a valid table)" % nbad)

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
print(f"wrote {OUT} ({fem.count(chr(10))} lines, "
      f"{len(blocks)} material blocks)")
