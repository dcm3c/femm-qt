# SPDX-License-Identifier: MIT
"""emit_nife_fem.py -- the four nickel-iron entries as a geometry-free FEMM
problem file, nife_fit_materials.fem, for opening and dragging into the
materials library.

The tables and conductivities are read from what nife_build.py wrote
(femm_nife_<tag>.csv, out/nife_fit.json), so this file and the staging
library nife_fit.dat cannot disagree.  Carpenter DC curves; see nife_build.py.

Usage:  python emit_nife_fem.py   (after python nife_build.py)
"""
import json
import numpy as np

MU0 = 4e-7 * np.pi
OUT = "nife_fit_materials.fem"
ORDER = ["48Ni_H2", "48Ni_mill", "80Ni4Mo_H2", "80Ni5Mo_H2"]

COMMENT = ("Nickel-iron: 48Ni after a dry hydrogen anneal and as received "
           "(mill process annealed, not re-annealed after fabrication), "
           "80Ni-4Mo and 80Ni-5Mo after a dry hydrogen anneal.  Each is the "
           "Carpenter DC normal-induction curve (High Permeability 49, HyMu "
           "80, HyMu 800), pooled in ln H where several are charted, carried "
           "above 10 Oe toward a given saturation (48Ni 1.60 T, 80Ni-4Mo "
           "0.82 T, 80Ni-5Mo 0.79 T, after Bozorth).  The as-received entry's "
           "maximum permeability is about 4,000; annealed, 150,000.  "
           "Materials only, no geometry.")


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


meta = json.load(open("out/nife_fit.json"))
blocks = []
for tag in ORDER:
    t = np.loadtxt("femm_nife_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    assert (np.diff(b) > 0).all() and (np.diff(h) > 0).all(), tag
    blocks.append(mat_block(meta[tag]["name"], b, h, meta[tag]["sigma_MSm"]))
    print("  %-34s %d points, B(800 A/m) %.3f T, Sigma %.2f MS/m"
          % (meta[tag]["name"], len(b), np.interp(800, h, b),
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
