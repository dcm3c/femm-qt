# SPDX-License-Identifier: MIT
"""compare_scans.py -- the scanned source data against the regressed
curves fitted to it, on one page.

Usage:  python compare_scans.py [params.json] [tag]

Everything here is read back from a saved parameter file (default
out/fit_params.json) through bhmodel.Family, so this never
re-fits: what is drawn is exactly what emit_matlib.py and emit_fem.py
write into the libraries.  The header line printed at the end names the
saturation constants in force, so a figure can always be tied back to
the parameter set that produced it.

Two figures:

  out/cmp_scans.png   the 14 curated USS/SLAC traces plus the two
                      Fermilab hold-outs, grouped by heat treatment,
                      with the regressed curve for the same grade drawn
                      through them; underneath, the error in H at fixed B.
  out/cmp_armco.png   the ARMCO Telar 57 and Pure Iron scans against
                      the Armco Fe gr.2 entry.

Filled markers are points the fit actually saw. Open markers are points
present in the scan but outside the fitting window -- the whole USS 1010
1400F trace above 5 Oe is open, because its level is impossible for its
composition (see the level audit) and only its toe was used.
"""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import bhlib as L

import sys
import bhmodel as MDL

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/fit_params.json"
TAG = ("_" + sys.argv[2]) if len(sys.argv) > 2 else ""
F = MDL.Family(SRC)
djsbar = F.djsbar
jfam, bfam = F.j, F.b

STAMP = ("%s (%s):  Js(c, Mn) = %.3f - %.4f c - %.4f Mn"
         % (SRC, F.version, L.JS_FE, L.K_C, L.K_MN)
         + ("" if djsbar == 0 else "  dJsBar = %+.4f T" % djsbar))


def herr(c, mn, d, h, j, eps=1e-3):
    """Fractional error in H at fixed B: positive means the regressed
    curve needs more field than the scan did to reach the same
    induction."""
    jf = jfam(c, mn, d, h)
    dj = (jfam(c, mn, d, h * (1 + eps)) - jf) / eps
    return (j - jf) / dj


# label, grade, C, Mn, cond, raw array, B already in tesla, fit window (Oe)
SCANS = [
    ("SLAC 1004", "1004", 0.04, 0.45, 4, "slac1004", True, (5.0, 1e4)),
    ("SLAC 1010", "1010", 0.10, 0.45, 4, "slac1010", True, (5.0, 1e4)),
    ("USS 1010", "1010", 0.10, 0.45, 2, "AISI1010asHotRolled", False, (0.0, 1e4)),
    ("USS 1010", "1010", 0.10, 0.45, 3, "AISI1010at1300degF", False, (0.0, 1e4)),
    ("USS 1010 (toe only)", "1010", 0.10, 0.45, 4, "AISI1010at1400degF", False, (0.0, 5.0)),
    ("USS 1010", "1010", 0.10, 0.45, 5, "AISI1010at1500degF", False, (0.0, 1e4)),
    ("USS 1020", "1020", 0.20, 0.45, 2, "AISI1020asHotRolled", False, (0.0, 1e4)),
    ("USS 1020", "1020", 0.20, 0.45, 3, "AISI1020at1300degF", False, (0.0, 1e4)),
    ("USS 1020", "1020", 0.20, 0.45, 4, "AISI1020at1400degF", False, (0.0, 1e4)),
    ("USS 1020", "1020", 0.20, 0.45, 5, "AISI1020at1500degF", False, (0.0, 1e4)),
    ("USS 1030", "1030", 0.30, 0.75, 2, "AISI1030asHotRolled", False, (0.0, 1e4)),
    ("USS 1030", "1030", 0.30, 0.75, 3, "AISI1030at1300degF", False, (0.0, 1e4)),
    ("USS 1030", "1030", 0.30, 0.75, 4, "AISI1030at1400degF", False, (0.0, 1e4)),
    ("USS 1030", "1030", 0.30, 0.75, 5, "AISI1030at1500degF", False, (0.0, 1e4)),
]
# Fermilab TM-1197, as received: never fitted, drawn in the as-rolled panel
HOLD = [("TM-1197 1008 (hold-out)", "1008", 0.08, 0.45, 2, "tm1197x1008"),
        ("TM-1197 1020 (hold-out)", "1020", 0.20, 0.45, 2, "tm1197x1020")]

COND = {2: "as hot rolled", 3: "annealed 1300 F", 4: "annealed 1400 F",
        5: "annealed 1500 F"}
COL = {"1004": "#1f77b4", "1008": "#9467bd", "1010": "#2ca02c",
       "1020": "#ff7f0e", "1030": "#d62728"}
MRK = {"SLAC": "o", "USS": "s", "TM-1197": "^"}

raw = L.load_raw()
hline = np.logspace(np.log10(0.3 * L.OE), np.log10(5e3 * L.OE), 400)

fig, axes = plt.subplots(2, 4, figsize=(21, 10),
                         gridspec_kw=dict(height_ratios=[2.4, 1]))
stats = []
for col, d in enumerate([2, 3, 4, 5]):
    ax, axr = axes[0, col], axes[1, col]
    seen = set()
    for lab, g, c, mn, cond, key, in_t, win in SCANS:
        if cond != d:
            continue
        bh = raw[key]
        h = bh[:, 1] * L.OE
        b = bh[:, 0] * (1.0 if in_t else L.KG)
        j = b - L.MU0 * h
        used = (bh[:, 1] >= win[0]) & (bh[:, 1] <= win[1])
        src = lab.split()[0]
        ax.semilogx(h[used] / L.OE, b[used] / L.KG, MRK[src], ms=4.5,
                    color=COL[g], mec=COL[g], label=lab)
        if (~used).any():
            ax.semilogx(h[~used] / L.OE, b[~used] / L.KG, MRK[src], ms=4.5,
                        mfc="none", color=COL[g], mec=COL[g], alpha=0.45,
                        label="_nolegend_")
        r = herr(c, mn, d, h, j)
        axr.semilogx(h[used] / L.OE, 100 * r[used], "-", lw=1.4, color=COL[g])
        if (~used).any():
            axr.semilogx(h[~used] / L.OE, 100 * r[~used], ":", lw=1.4,
                         color=COL[g], alpha=0.7)
        stats.append((COND[d], lab, L.rms(r[used])))
        if g not in seen:
            seen.add(g)
            ax.semilogx(hline / L.OE, bfam(c, mn, d, hline) / L.KG, "-",
                        lw=1.6, color=COL[g], alpha=0.85,
                        label=g + " regressed")
    if d == 2:
        for lab, g, c, mn, cond, key in HOLD:
            bh = raw[key]
            h, b = bh[:, 1] * L.OE, bh[:, 0] * L.KG
            ax.semilogx(h / L.OE, b / L.KG, "^", ms=4.5, mfc="none",
                        color=COL[g], mec=COL[g], alpha=0.55, label=lab)
            if g not in seen:
                seen.add(g)
                ax.semilogx(hline / L.OE, bfam(c, mn, d, hline) / L.KG,
                            "--", lw=1.3, color=COL[g], alpha=0.7,
                            label=g + " regressed")
    ax.set_title(COND[d], fontsize=12)
    ax.set_xlim(0.3, 5000)
    ax.set_ylim(0, 24)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7.5, loc="upper left")
    axr.axhline(0, color="k", lw=0.8)
    for y in (-20, 20):
        axr.axhline(y, color="k", lw=0.6, ls=":", alpha=0.5)
    axr.set_xlim(0.3, 5000)
    axr.set_ylim(-45, 45)
    axr.grid(True, which="both", alpha=0.3)
    axr.set_xlabel("H [Oe]")
    if col == 0:
        ax.set_ylabel("B [kG]")
        axr.set_ylabel("H error [%]")

fig.suptitle("Scanned source data and the regressed curves fitted to it "
             "(markers: scan; line: the library entry for that grade "
             "and anneal).  Open markers lie outside the fitting "
             "window.\nLower row: error in H at fixed B, "
             "100 x (H_fit - H_scan)/H_scan.",
             fontsize=12)
fig.text(0.5, 0.004, STAMP, ha="center", fontsize=8, color="#555555")
fig.tight_layout(rect=[0, 0.012, 1, 0.95])
fig.savefig("out/cmp_scans%s.png" % TAG, dpi=110)
print("wrote out/cmp_scans%s.png" % TAG)

print("")
print(STAMP)
print("RMS H-error of the regressed curve vs each fitted scan window:")
for cond, lab, r in stats:
    print("  %-16s %-22s %5.1f %%" % (cond, lab, 100 * r))
print("  %-39s %5.1f %%" % ("overall", 100 * L.rms([s[2] for s in stats])))

# --------------------------------------------------------------- ARMCO
#
# Two different things are being compared here and they must not be
# confused.  The thin coloured traces and the heavy coloured line are
# MEASUREMENT: the AK Steel bulletin scans, and the curated table built
# from them that is emitted in the "Armco (AK Steel data)" folder.  The black lines
# are the fitted FAMILY evaluated at Armco's composition, i.e. what a
# very low-carbon steel would do given the same shop anneal as the USS
# bars.  Real Armco is melted and annealed to a far softer state than
# that, so the black lines sit well to the right, and that gap is the
# reason the measured entries ship separately instead of as model output.
fig, axes = plt.subplots(1, 2, figsize=(15, 6.5))
AC, AMN = 0.010, 0.100
PANELS = [
    ("ARMCO Telar 57 bars, annealed 815 C",
     [("../iron/out/telar_4mm_annealed_raw.csv", "4 mm bar (excluded: bulletin "
       "notes cold-drawn stock)"),
      ("../iron/out/telar_8mm_annealed_raw.csv", "8 mm bar"),
      ("../iron/out/telar_10mm_annealed_raw.csv", "10 mm bar"),
      ("../iron/out/telar_12mm_annealed_raw.csv", "12 mm bar"),
      ("../iron/out/telar_fig5_high_raw.csv", "Fig 5 high induction")],
     "../iron/femm_Telar57.csv", "ARMCO Telar 57, curated measured entry"),
    ("ARMCO Pure Iron sheet, annealed 820 C",
     [("../iron/out/pi_cr2mm_820C_raw.csv", "2 mm cold rolled"),
      ("../iron/out/pi_hr3mm_820C_raw.csv", "3 mm hot rolled"),
      ("../iron/out/pi_fig7_annealed_raw.csv", "Fig 7 annealed"),
      ("../iron/out/pi_fig8_high_raw.csv", "Fig 8 high induction")],
     "../iron/femm_ArmcoPureIron.csv", "ARMCO Pure Iron, curated measured entry"),
]
for ax, (ttl, scans, curated, clab) in zip(axes, PANELS):
    for path, lab in scans:
        d = np.loadtxt(path, delimiter=",", skiprows=1)
        ax.semilogx(d[:, 0] / L.OE, d[:, 1] / L.KG, "-", lw=1.0, alpha=0.7,
                    label=lab)
    t = np.loadtxt(curated, delimiter=",")          # columns B [T], H [A/m]
    ax.semilogx(t[1:, 1] / L.OE, t[1:, 0] / L.KG, "-", color="#333333",
                lw=2.4, alpha=0.9, label=clab)
    ax.semilogx(hline / L.OE, bfam(AC, AMN, 5, hline) / L.KG, "k-", lw=1.6,
                label="regressed at C 0.010 / Mn 0.100, " + COND[5])
    ax.semilogx(hline / L.OE, bfam(AC, AMN, 4, hline) / L.KG, "k--", lw=1.4,
                label="regressed at C 0.010 / Mn 0.100, " + COND[4])
    ax.set_title(ttl, fontsize=11)
    ax.set_xlim(0.1, 5000)
    ax.set_ylim(0, 24)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_xlabel("H [Oe]")
    ax.legend(fontsize=7.5, loc="upper left")
axes[0].set_ylabel("B [kG]")
fig.suptitle("ARMCO scans and the curated measured entries, against the "
             "fitted family evaluated at Armco composition.\nThe family is "
             "an extrapolation of shop-annealed carbon steel, not a model "
             "of pure iron: use the measured entries for real Armco.",
             fontsize=11)
fig.text(0.5, 0.005, STAMP, ha="center", fontsize=8, color="#555555")
fig.tight_layout(rect=[0, 0.018, 1, 0.91])
fig.savefig("out/cmp_armco%s.png" % TAG, dpi=110)
print("wrote out/cmp_armco%s.png" % TAG)
