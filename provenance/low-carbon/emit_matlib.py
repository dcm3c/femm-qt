# SPDX-License-Identifier: MIT
"""emit_matlib.py -- write the fitted family as a femm-qt materials library.

Creates lowcarbon_fit.dat here in the project.  This is a STAGING file,
not an installable library: FEMM reads matlib.dat and nothing else, so a
.dat sitting beside it is inert.  The blocks here are in matlib.dat's
format so they can be spliced into it, displacing the stock entries they
supersede -- the AISI-numbered ones in its Low Carbon Steel folder.
Nothing in this project ever writes to matlib.dat.
with two folders: "Low Carbon Steel fit" (9 grades x {annealed 1400F,
as hot rolled} from the fitted model) and "Armco (AK Steel data)" (the two
curated digitized entries from measured.py). 61-point B-H tables,
(0,0) + log-spaced 10..4e5 A/m (~5030 Oe), CRLF, tab-separated rows in
the stock matlib.dat block format.

Only the two canonical conditions go in the library; all four are in the
companion .fem (emit_fem.py) for inspection.

Mu_x/Mu_y is set to the low-field relative permeability at the first
grid point (only used by FEMM if the BH curve is disabled). Sigma is
per-entry via measured.sigma_for (carbon grades from composition by the
USS formula calibrated to ASM Vol. 1; 9.35 MS/m pure iron). Phi_h = 0 (no hysteresis-lag data claimed).
"""

import numpy as np
import bhlib as L
import bhmodel as MDL
import measured as M

PARAMS = "out/fit_params.json"
OUT = "lowcarbon_fit.dat"        # in the project, not the FEMM tree
F = MDL.Family(PARAMS)
VER = F.version

# Armco composition is the AK Steel bulletin Gr.2 maxima (C <=0.010,
# Mn <=0.100).
GRADES = [("Armco Fe gr.2", 0.010, 0.100), ("1004", 0.040, 0.35),
          ("1006", 0.060, 0.325), ("1008", 0.080, 0.40),
          ("1010", 0.100, 0.45), ("1018", 0.175, 0.75),
          ("1117", 0.180, 1.15), ("1020", 0.200, 0.45),
          ("1030", 0.300, 0.75)]
CONDS = [(4, "annealed 1400F"), (2, "as hot rolled")]
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])


def block(name, mu, bh, sigma=M.SIGMA_CARBON):
    ln = ["<BeginBlock>", f'<BlockName> = "{name}"',
          f"<Mu_x> = {mu:.1f}", f"<Mu_y> = {mu:.1f}",
          "<H_c> = 0", "<H_cAngle> = 0", "<J_re> = 0", "<J_im> = 0",
          f"<Sigma> = {sigma}", "<d_lam> = 0", "<Phi_h> = 0", "<Phi_hx> = 0",
          "<Phi_hy> = 0", "<LamType> = 0", "<LamFill> = 1",
          "<NStrands> = 0", "<WireD> = 0", f"<BHPoints> = {len(bh)}"]
    ln += [f"\t{b:.9f}\t{h:.6f}" for b, h in bh]
    ln.append("<EndBlock>")
    return ln


lines = ["<BeginFolder>", f'<FolderName> = "Low Carbon Steel {VER}"']
for gname, c, mn in GRADES:
    for d, clab in CONDS:
        b = F.b(c, mn, d, HGRID)
        mu = float(F.b(c, mn, d, 10.0) / (L.MU0 * 10.0))
        name = f"{gname} {VER} {clab}"
        lines += block(name, mu, list(zip(b, HGRID)),
                       sigma=M.sigma_for(gname))
        print(f"{name:32s} mu_init={mu:7.0f}  B(4000 Oe)="
              f"{F.b(c, mn, d, 4000*L.OE):.4f} T")
lines.append("<EndFolder>")

# Second folder: MEASURED curves, deliberately kept apart from the fitted
# family so a user can always tell which is which.
lines += ["<BeginFolder>", '<FolderName> = "Armco (AK Steel data)"']
for e in M.MEASURED:
    b, h = M.load_table(e["csv"])
    mu = float(b[1] / (L.MU0 * h[1]))
    lines += block(e["name"], mu, list(zip(b, h)), sigma=e["sigma"])
    print(f'{e["name"]:32s} mu_init={mu:7.0f}  B(4e5)={b[-1]:.4f} T  '
          f'Sigma={e["sigma"]}')
lines.append("<EndFolder>")

with open(OUT, "w", newline="\r\n") as f:
    f.write("\n".join(lines) + "\n")
print(f"\nwrote {OUT} ({len(lines)} lines)")

# ---- verify: re-parse with the same regex logic as compare_femm.py ----
import re
text = open(OUT, encoding="utf-8").read()
nblk = 0
for m in re.finditer(r"<BeginBlock>(.*?)<EndBlock>", text, re.S):
    body = m.group(1)
    nm = re.search(r'<BlockName>\s*=\s*"([^"]*)"', body).group(1)
    npts = int(re.search(r"<BHPoints>\s*=\s*(\d+)", body).group(1))
    rows = [r for r in body[body.index("<BHPoints>"):].splitlines()[1:]
            if r.strip()]
    bh = np.array([[float(v) for v in r.split()] for r in rows[:npts]])
    assert len(bh) == npts, nm
    assert (np.diff(bh[:, 0]) > 0).all() and (np.diff(bh[:, 1]) > 0).all(), \
        f"non-monotone: {nm}"
    nblk += 1
print(f"verified: {nblk} blocks re-parsed, all B-H tables strictly "
      "monotone")
