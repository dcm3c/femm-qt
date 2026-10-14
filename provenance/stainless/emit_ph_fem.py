# SPDX-License-Identifier: MIT
"""emit_ph_fem.py -- the nine PH stainless entries as a geometry-free FEMM
problem file, ph_fit_materials.fem, for opening and dragging into the
materials library.

The tables and conductivities are read from what ph_build.py wrote
(femm_ph_<tag>.csv, out/ph_fit.json), so this file and the staging library
ph_fit.dat cannot disagree.  Cleveland-Cliffs (ARMCO) DC curves; see
ph_build.py.

Usage:  python emit_ph_fem.py   (after python ph_build.py)
"""
import json
import numpy as np

MU0 = 4e-7 * np.pi
OUT = "ph_fit_materials.fem"
ORDER = ["174_A", "174_H900", "174_H1075", "174_H1150",
         "155_A", "155_H900", "155_H1075", "155_H1150", "138Mo_H950"]

COMMENT = ("Precipitation-hardening stainless steels: 17-4 PH and 15-5 PH "
           "in Condition A, H 900, H 1075/H 1100 and H 1150, and PH 13-8 Mo "
           "in Condition H 950, from the DC normal induction curves of the "
           "Cleveland-Cliffs (ARMCO) product bulletins, carried above the "
           "charts toward a saturation from composition.  Materials only, "
           "no geometry.")


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


meta = json.load(open("out/ph_fit.json"))
blocks = []
for tag in ORDER:
    t = np.loadtxt("femm_ph_%s.csv" % tag, delimiter=",")
    b, h = t[:, 0], t[:, 1]
    assert (np.diff(b) > 0).all() and (np.diff(h) > 0).all(), tag
    blocks.append(mat_block(meta[tag]["name"], b, h, meta[tag]["sigma_MSm"]))
    print("  %-34s %d points, B(8 kA/m) %.3f T, Sigma %.2f MS/m"
          % (meta[tag]["name"], len(b), np.interp(8000, h, b),
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

# round trip: read the file back the way compare_femm.py reads matlib.dat
import re
text = open(OUT, encoding="utf-8").read()
n = 0
for m in re.finditer(r"<BeginBlock>(.*?)<EndBlock>", text, re.S):
    body = m.group(1)
    name = re.search(r'<BlockName>\s*=\s*"([^"]*)"', body).group(1)
    tag = next(t for t in ORDER if meta[t]["name"] == name)
    rows = np.array([[float(v) for v in ln.split()] for ln in
                     body.split("<BHPoints>")[1].splitlines()[1:] if ln.strip()])
    ref = np.loadtxt("femm_ph_%s.csv" % tag, delimiter=",")
    assert rows.shape == ref.shape, name
    assert np.abs(rows - ref).max() < 1e-6, (name, np.abs(rows - ref).max())
    n += 1
assert n == len(ORDER)
print("wrote %s (%d material blocks, round trip ok)" % (OUT, len(blocks)))
