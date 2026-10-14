# SPDX-License-Identifier: MIT
"""emit_metglas_fem.py -- the two Metglas entries as a geometry-free FEMM
problem file, metglas_fit_materials.fem, for opening and dragging into the
materials library.

The tables and conductivities are read from what metglas_build.py wrote
(femm_metglas_<tag>.csv, out/metglas_fit.json), so this file and the staging
library metglas_fit.dat cannot disagree.

Usage:  python emit_metglas_fem.py   (after python metglas_build.py)
"""
import json
import numpy as np

MU0 = 4e-7 * np.pi
OUT = "metglas_fit_materials.fem"
ORDER = ["2605SA1", "2605HB1M"]

COMMENT = ("Amorphous ribbon: Metglas 2605SA1 and 2605HB1M after Metglas's "
           "longitudinal field anneal.  Each normal curve is built from the "
           "single-strip DC loops (Hm 8 and 80 A/m) of Metglas's 'Amorphous "
           "Alloys for Transformer Cores' (2011) and passes through both loop "
           "tips; above 80 A/m nothing is measured and the curve is a law-of-"
           "approach tail to the given saturation (1.56 and 1.63 T), bounded "
           "within 0.13 T.  Ribbon 25 um (d_lam); LamFill 0.84, Metglas's "
           "lamination factor for these alloys (>= 84 %).  Materials only, no "
           "geometry.")


def mat_block(name, bb, hh, sigma, dlam, fill):
    mu = float(bb[1] / (MU0 * hh[1]))
    ln = ["  <BeginBlock>", f'    <BlockName> = "{name}"',
          f"    <Mu_x> = {mu:.1f}", f"    <Mu_y> = {mu:.1f}",
          "    <H_c> = 0", "    <H_cAngle> = 0", "    <J_re> = 0",
          "    <J_im> = 0", f"    <Sigma> = {sigma}", f"    <d_lam> = {dlam}",
          "    <Phi_h> = 0", "    <Phi_hx> = 0", "    <Phi_hy> = 0",
          "    <LamType> = 0", f"    <LamFill> = {fill}", "    <NStrands> = 0",
          "    <WireD> = 0", f"    <BHPoints> = {len(bb)}"]
    ln += [f"      {b:.9f}\t{h:.6f}" for b, h in zip(bb, hh)]
    ln.append("  <EndBlock>")
    return "\n".join(ln)


meta = json.load(open("out/metglas_fit.json"))
blocks = []
for tag in ORDER:
    t = np.loadtxt("femm_metglas_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    assert (np.diff(b) > 0).all() and (np.diff(h) > 0).all(), tag
    m = meta[tag]
    blocks.append(mat_block(m["name"], b, h, m["sigma_MSm"], m["d_lam_mm"], m["lam_fill"]))
    print("  %-34s %d points, B(80 A/m) %.3f T, B(1 kA/m) %.3f T, Sigma %.3f MS/m"
          % (m["name"], len(b), np.interp(80, h, b), np.interp(1000, h, b), m["sigma_MSm"]))

fem = "\n".join([
    "[Format]      =  4.0", "[Frequency]   =  0", "[Precision]   =  1e-008",
    "[MinAngle]    =  30", "[Depth]       =  1",
    "[LengthUnits] =  millimeters", "[ProblemType] =  planar",
    "[Coordinates] =  cartesian", "[ACSolver]    =  0",
    f'[Comment]     =  "{COMMENT}"', "[PointProps]   = 0",
    "[BdryProps]   = 0", f"[BlockProps]  = {len(blocks)}", *blocks,
    "[CircuitProps]  = 0", "[NumPoints] = 0", "[NumSegments] = 0",
    "[NumArcSegments] = 0", "[NumHoles] = 0", "[NumBlockLabels] = 0",
]) + "\n"
with open(OUT, "w", newline="\r\n") as f:
    f.write(fem)
print("wrote %s (%d material blocks)" % (OUT, len(blocks)))
