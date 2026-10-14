# SPDX-License-Identifier: MIT
"""emit_si_matlib.py -- write the silicon family as a femm-qt materials library.

Creates silicon_fit.dat here in the project.  This is a STAGING file, not
an installable library: FEMM reads matlib.dat and nothing else, so a .dat
sitting beside it is inert.  The blocks here are in matlib.dat's format so
they can be spliced into it, displacing the stock entries they supersede --
the M-grades in its Silicon Iron folder.  The stock matlib.dat is never
opened for writing by anything in this project; compare_femm_si.py reads
it, and that is all.

Two folders, deliberately kept apart so a user can always tell which is
which -- the same split the low-carbon library uses:

  "Silicon Steel (USS 1978)"               the ten regressed entries, plus
                                           named grades the catalog lacks,
                                           from the composition law
                                           (silib.LAW_GRADES: M-15)
  "Silicon Steel (USS 1978, measured)"     the ten digitized pages, extended

61-point B-H tables, (0,0) + log-spaced 10..4e5 A/m (~5030 Oe), CRLF,
tab-separated rows in the stock block format.

WHAT IS BETTER SOURCED HERE THAN IN THE LOW-CARBON LIBRARY.

  Sigma is per grade and comes from the book's own stated resistivity
  through the p.116 composition formula -- 1.92 MS/m at 52 uOhm-cm up to
  6.25 at 16.  The low-carbon library had to use one stock figure for every
  carbon grade.  FEMM's stock M-grade entries carry 1.9-2.9 MS/m with no
  stated provenance; ours agree with those to within a few per cent where
  the grade names match, which is reassuring but is not why they are used.

  d_lam is the gauge the page itself names: 14 mil (0.356 mm) for the 29
  gauge grades, 18.5 mil (0.470) for 26 gauge, 18 mil (0.457) for the
  lamination sheet.  FEMM's stock entries all carry 0.635 mm (25 mil),
  which is heavier than any of these.

LamFill IS A SUGGESTED STACKING FACTOR, ON SOLID-METAL CURVES.  si_basis.py
establishes from the book (p.115) and from the modern bulletin for the
descendant product line that the catalog's B is flux divided by NET METAL
area -- specimen mass over assumed density -- so the lamination factor is
not in the curve.  The bulletin says so in as many words: "Designers may
approximate net area by multiplying gross area of the compacted core by its
lamination factor."  The tables are therefore solid-metal B-H curves, as the
Metglas entries are, and LamFill carries the stacking factor on top of them,
as the Metglas entries carry Metglas's own.  The steel's maker states none,
so it is the midpoint of the femm.info stacking-factor table (Tarter,
Constantinides, Nasar, Magnetics Inc.) at the nearest tabulated thickness:
0.014 in 0.95-0.96 -> 0.955, 0.0185 in 0.96-0.97 -> 0.965, 0.025 in
0.97-0.98 -> 0.975 (2026-10-03, user's call: an entry with LamFill 1 is an
entry nobody changes).  FEMM's stock M-grade entries carry 0.98.

Mu_x/Mu_y is the relative permeability at the first grid point (FEMM uses
it only if the B-H curve is disabled).  Phi_h = 0: no hysteresis-lag data
is claimed.
"""
import re
import numpy as np

import bhlib as L
import silib as S
import simodel as M

PARAMS = "out/si_params.json"
OUT = "silicon_fit.dat"          # in the project, not the FEMM tree
MIL = 0.0254                    # mm per mil
HGRID = np.concatenate([[0.0], np.logspace(1, np.log10(4.0e5), 60)])

F = M.SiFamily(PARAMS)
rows = S.table()


# femm.info "Stacking Factor" page (saved 2026-10-02, Metglas/), midpoint of
# the sources at each tabulated thickness: inches -> fill
STACK = {0.014: 0.955, 0.0185: 0.965, 0.025: 0.975}


def lam_fill(d_lam):
    """Suggested stacking factor for a lamination of d_lam mm: the table
    row nearest in thickness."""
    t = d_lam / 25.4
    return STACK[min(STACK, key=lambda k: abs(k - t))]


def block(name, bb, hh, sigma, d_lam, indent="", nl="\n"):
    mu = float(bb[1] / (L.MU0 * hh[1]))
    ln = [f"{indent}<BeginBlock>",
          f'{indent}<BlockName> = "{name}"',
          f"{indent}<Mu_x> = {mu:.1f}", f"{indent}<Mu_y> = {mu:.1f}",
          f"{indent}<H_c> = 0", f"{indent}<H_cAngle> = 0",
          f"{indent}<J_re> = 0", f"{indent}<J_im> = 0",
          f"{indent}<Sigma> = {sigma:.4g}",
          f"{indent}<d_lam> = {d_lam:.4g}",
          f"{indent}<Phi_h> = 0", f"{indent}<Phi_hx> = 0",
          f"{indent}<Phi_hy> = 0",
          f"{indent}<LamType> = 0", f"{indent}<LamFill> = {lam_fill(d_lam)}",
          f"{indent}<NStrands> = 0", f"{indent}<WireD> = 0",
          f"{indent}<BHPoints> = {len(bb)}"]
    ln += [f"{indent}\t{b_:.9f}\t{h_:.6f}" for b_, h_ in zip(bb, hh)]
    ln.append(f"{indent}<EndBlock>")
    return ln


def entries():
    """(folder, name, B, H, sigma, d_lam) for everything that ships."""
    out = []
    for r in rows:
        nm = "%s (USS 1978) %.0f mil" % (r["name"], r["mils"])
        out.append((0, nm, F.b(r["key"], HGRID), HGRID,
                    r["sigma"], r["mils"] * MIL))
    for g in S.law_grades():        # named grades from the composition law
        nm = "%s (law, %s) %.0f mil" % (g["name"], g["source"],
                                          g["mils"])
        out.append((0, nm, F.b_law(g["sieq"], g["regime"], HGRID), HGRID,
                    g["sigma"], g["mils"] * MIL))
    for r in rows:
        d = np.loadtxt("femm_simeas_%s.csv" % r["key"], delimiter=",")
        nm = "%s (USS 1978 measured) %.0f mil" % (r["name"], r["mils"])
        out.append((1, nm, d[:, 0], d[:, 1], r["sigma"], r["mils"] * MIL))
    return out


FOLDERS = ["Silicon Steel (USS 1978)",
           "Silicon Steel (USS 1978, measured)"]



def main():
    lines = []
    all_e = entries()
    for fi, fname in enumerate(FOLDERS):
        lines += ["<BeginFolder>", f'<FolderName> = "{fname}"']
        print("%s:" % fname)
        for f_, nm, bb, hh, sig, dl in all_e:
            if f_ != fi:
                continue
            lines += block(nm, bb, hh, sig, dl)
            print("  %-44s mu_init=%6.0f  B(1000 Oe)=%.4f  Sigma=%.2f MS/m  "
                  "d_lam=%.3f mm"
                  % (nm, bb[1] / (L.MU0 * hh[1]),
                     float(np.interp(1000 * L.OE, hh, bb)), sig, dl))
        lines.append("<EndFolder>")

    with open(OUT, "w", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    print("\nwrote %s (%d lines, %d blocks in %d folders)"
          % (OUT, len(lines), len(all_e), len(FOLDERS)))

    # ---- verify by re-parsing, as emit_matlib.py does -------------------------
    text = open(OUT, encoding="utf-8").read()
    nblk = 0
    for m in re.finditer(r"<BeginBlock>(.*?)<EndBlock>", text, re.S):
        body = m.group(1)
        nm = re.search(r'<BlockName>\s*=\s*"([^"]*)"', body).group(1)
        npts = int(re.search(r"<BHPoints>\s*=\s*(\d+)", body).group(1))
        rr = [x for x in body[body.index("<BHPoints>"):].splitlines()[1:]
              if x.strip()]
        bh = np.array([[float(v) for v in x.split()] for x in rr[:npts]])
        assert len(bh) == npts, nm
        assert (np.diff(bh[:, 0]) > 0).all(), "B not monotone: %s" % nm
        assert (np.diff(bh[:, 1]) > 0).all(), "H not monotone: %s" % nm
        assert (np.diff(bh[:, 0]) / np.diff(bh[:, 1]) >= L.MU0 * 0.999).all(), \
            "dB/dH < mu0: %s" % nm
        nblk += 1
    print("verified: %d blocks re-parsed, B and H strictly monotone, "
          "dB/dH >= mu0 throughout" % nblk)


if __name__ == "__main__":
    main()
