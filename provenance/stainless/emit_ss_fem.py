# SPDX-License-Identifier: MIT
"""emit_ss_fem.py -- the eight stainless entries as a geometry-free FEMM
problem file, ss_fit_materials.fem, for opening and dragging into the
materials library.

The tables and conductivities are read from what ss_build.py wrote
(femm_ss_<tag>.csv, out/ss_fit.json), so this file and the staging
library ss_fit.dat cannot disagree.  Carpenter and Valbruna DC curves; see ss_build.py.

Usage:  python emit_ss_fem.py   (after python ss_build.py)
"""
import json
import numpy as np

MU0 = 4e-7 * np.pi
OUT = "ss_fit_materials.fem"
ORDER = ["410_ann", "416_ann", "420_ann", "12Cr_ann", "430F_ann", "430FR_ann",
         "446_ann", "430FR_cd"]

COMMENT = ("Stainless steels, annealed: 410, 416, 420, a 12Cr ferritic "
           "(Chrome Core 12-FM class), 430F, 430FR and 446, from one master "
           "shape J/Js = M(H/H50) fitted to four annealed curves (Carpenter "
           "Chrome Core 12-FM and 430FR, Valbruna MG2, Metals Handbook 416), "
           "scaled by saturation from composition and by each grade's "
           "maximum permeability; and 430FR cold drawn, from Valbruna's MG2 "
           "curve.  Materials only, no geometry.")


def mat_block(name, bb, hh, sigma):
    mu = float(bb[1] / (MU0 * hh[1]))
    ln = ["  <BeginBlock>", f'    <BlockName> = "{name}"',
          f"    <Mu_x> = {mu:.1f}", f"    <Mu_y> = {mu:.1f}",
          "    <H_c> = 0", "    <H_cAngle> = 0", "    <J_re> = 0",
          "    <J_im> = 0", f"    <Sigma> = {sigma}", "    <d_lam> = 0",
          "    <Phi_h> = 0", "    <Phi_hx> = 0", "    <Phi_hy> = 0",
          "    <LamType> = 0", "    <LamFill> = 1", "    <NStrands> = 0",
          "    <WireD> = 0", f"    <BHPoints> = {len(bb)}"]
    ln += [f"      {b:.9f}\t{h:.6f}" for b, h in zip(bb, hh)]
    ln.append("  <EndBlock>")
    return "\n".join(ln)


meta = json.load(open("out/ss_fit.json"))
blocks = []
for tag in ORDER:
    t = np.loadtxt("femm_ss_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    assert (np.diff(b) > 0).all() and (np.diff(h) > 0).all(), tag
    blocks.append(mat_block(meta[tag]["name"], b, h, meta[tag]["sigma_MSm"]))
    print("  %-34s %d points, B(1 kA/m) %.3f T, Sigma %.2f MS/m"
          % (meta[tag]["name"], len(b), np.interp(1000, h, b),
             meta[tag]["sigma_MSm"]))

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
