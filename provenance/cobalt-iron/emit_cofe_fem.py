# SPDX-License-Identifier: MIT
"""emit_cofe_fem.py -- the four cobalt-iron entries as a geometry-free FEMM
problem file, cofe_fit_materials.fem, for opening and dragging into the
materials library.

The tables and conductivities are read from what cofe_build.py wrote
(femm_cofe_<tag>.csv, out/cofe_fit.json), so this file and the staging
library cofe_fit.dat cannot disagree.  See cofecurves.tex.

Usage:  python emit_cofe_fem.py   (after python cofe_build.py)
"""
import json
import numpy as np

MU0 = 4e-7 * np.pi
OUT = "cofe_fit_materials.fem"
ORDER = ["49Co2V_mag", "49Co2V_rotor", "49Co2V_hs", "27Co"]

COMMENT = ("Cobalt-iron: 49Co-2V after the magnetic anneal, as a rotor grade "
           "(strength anneal, 390-600 MPa yield) and at high strength (~800 "
           "MPa), and 27Co.  Each is a fit to the typical B(H) values "
           "tabulated by Vacuumschmelze, Carpenter and Aperam Imphy, pooled; "
           "saturation given (49Co-2V 2.30 T after Bozorth's vanadium "
           "additions, 27Co 2.38 T), law of approach above the data.  Below "
           "160 A/m (49Co) and 800 A/m (27Co, high strength) the shape is the "
           "fitted form, not measurement.  Materials only, no geometry.  "
           "See cofecurves.pdf.")


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


meta = json.load(open("out/cofe_fit.json"))
blocks = []
for tag in ORDER:
    t = np.loadtxt("femm_cofe_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    assert (np.diff(b) > 0).all() and (np.diff(h) > 0).all(), tag
    blocks.append(mat_block(meta[tag]["name"], b, h, meta[tag]["sigma_MSm"]))
    print("  %-34s %d points, B(40 kA/m) %.3f T, Sigma %.2f MS/m"
          % (meta[tag]["name"], len(b), np.interp(40000, h, b),
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
