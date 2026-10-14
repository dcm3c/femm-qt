# SPDX-License-Identifier: MIT
"""si_crosscheck.py -- the silicon family's three laws against Metals Handbook.

Everything this project derives about silicon steel composition rests on two
formulas printed in one book (US Steel, ADUSS 31-7103-01, May 1978, pp.115
and 116) plus one saturation coefficient taken from the retained-austenite
literature (Exner via Ouda 2018).  None of that had been tested against an
independent source that states composition and properties together.

Metals Handbook 8th ed. Vol. 1 (ASM), p.788, does exactly that.  Its
Table 2, "Silicon Contents, Properties and Applications of Electrical Steel
Sheets", gives for each AISI M-grade a nominal silicon content, a density
RANGE and an electrical resistivity RANGE; its Table 1 gives, for the same
grades, a saturation induction.  So it pins silicon against resistivity,
against density, and against saturation, in one place, from a source that
knows nothing of this project.

Scanned by the user 2026-09-23 as SiliconSteel/Scan - 2026-09-23
00_18_43.pdf (Microsoft Lens, one page); SHA-256 in SOURCES.md.  It is the
same volume as Fig. 17 p.792 (bhcurves.jpg), which is where FEMM's bare
M-xx entries come from -- see compare_femm_si.py.

CAUTION, AND IT IS THE HANDBOOK'S OWN.  Fig. 17's code list and Table 2
disagree about silicon for the same grades: the figure says M-19 3.75%,
M-27 2.75%, M-36 1.5%, M-43 0.5%; the table says 3.20, 2.35, 1.40, 0.95.
Table 2 is preferred here -- it is a properties table carrying density and
resistivity alongside, and its numbers are the ones that have to be
consistent with the rest of the page -- but the disagreement is real and is
printed below, because it bounds how precisely any of this can be known.

Usage:  python si_crosscheck.py
"""
import numpy as np

import bhlib as L
import silib as S

# --- Metals Handbook 8th ed. Vol. 1 p.788 ---------------------------------
# Table 2: grade -> (nominal Si %, density hi, density lo, rho lo, rho hi)
MH2 = {
    "M-50": (0.40, 7.85, 7.85, 18, 18),
    "M-43": (0.95, 7.82, 7.78, 20, 28),
    "M-36": (1.40, 7.80, 7.75, 24, 33),
    "M-27": (2.35, 7.76, 7.67, 32, 47),
    "M-22": (3.00, 7.70, 7.64, 41, 52),
    "M-19": (3.20, 7.70, 7.62, 41, 56),
    "M-17": (3.40, 7.68, 7.60, 45, 58),
    "M-15": (4.00, 7.68, 7.54, 45, 69),
    "M-14": (4.50, 7.60, 7.54, 58, 69),
}
# Table 1: grade -> saturation induction, B-H or 4 pi Is, gausses
MH1_JS = {"M-43": 21300, "M-36": 21100, "M-27": 20600, "M-22": 20300,
          "M-19": 20000, "M-17": 19900, "M-15": 19500, "M-14": 19200}
# Fig. 17 code list, the same volume, for the disagreement note
FIG17_SI = {"M-43": 0.50, "M-36": 1.50, "M-27": 2.75, "M-22": 3.25,
            "M-19": 3.75, "M-14": 4.50}

ORDER = ["M-50", "M-43", "M-36", "M-27", "M-22", "M-19", "M-17", "M-15",
         "M-14"]

print(__doc__.split("Usage:")[0].strip().split("\n\n")[0])
print("\n" + "=" * 76)
print("1. RESISTIVITY.  USS 1978 p.116 formula against Metals Handbook "
      "Table 2.")
print("   rho = %.2f + %.1f Si  (minor elements at the book's own worked "
      "example)\n" % (S.RHO0, S.RHO_SI))
print("   %-7s %7s %12s %14s %8s" % ("grade", "MH %Si", "MH rho range",
                                     "our rho(Si)", "verdict"))
nin = 0
for g in ORDER:
    si, _, _, lo, hi = MH2[g]
    ours = S.RHO0 + S.RHO_SI * si
    ok = lo <= ours <= hi
    nin += ok
    print("   %-7s %7.2f %6d to %-4d %14.1f %8s"
          % (g, si, lo, hi, ours, "inside" if ok else "OUTSIDE"))
print("   -> %d of %d inside the handbook's own stated range" % (nin, len(ORDER)))

print("\n" + "=" * 76)
print("2. DENSITY.  USS 1978 p.115 formula against Metals Handbook Table 2.")
print("   density = 7.865 - 0.065 Si   (aluminium taken as zero)\n")
print("   %-7s %7s %16s %14s %8s" % ("grade", "MH %Si", "MH density range",
                                     "our density", "verdict"))
nin = 0
for g in ORDER:
    si, dhi, dlo, _, _ = MH2[g]
    ours = 7.865 - 0.065 * si
    ok = dlo - 5e-3 <= ours <= dhi + 5e-3
    nin += ok
    print("   %-7s %7.2f %8.2f to %-4.2f %14.3f %8s"
          % (g, si, dlo, dhi, ours, "inside" if ok else "OUTSIDE"))
print("   -> %d of %d inside" % (nin, len(ORDER)))

print("\n" + "=" * 76)
print("3. SATURATION.  Our Js law against Metals Handbook Table 1, using")
print("   the handbook's OWN silicon from its Table 2.")
print("   Js = %.4f - %.3f Si   (Sanford & Bennett less 0.005 C, 0.25 Mn;"
      " Exner via Ouda 2018)\n" % (S.JS_BASE, S.K_SI))
print("   %-7s %7s %12s %12s %8s" % ("grade", "MH %Si", "MH Js (T)",
                                     "our Js(Si)", "ratio"))
si_v, js_v = [], []
for g in ORDER:
    if g not in MH1_JS:
        continue
    si = MH2[g][0]
    js_mh = MH1_JS[g] * 1e-4
    si_v.append(si)
    js_v.append(js_mh)
    print("   %-7s %7.2f %12.4f %12.4f %8.4f"
          % (g, si, js_mh, S.js(si), S.js(si) / js_mh))
si_v, js_v = np.array(si_v), np.array(js_v)
k_free, b_free = np.polyfit(si_v, js_v, 1)
k_fix = float(np.sum(si_v * (S.JS_BASE - js_v)) / np.sum(si_v ** 2))
rms_ours = float(np.sqrt(np.mean((S.js(si_v) - js_v) ** 2)))
print("   -> our law sits within %.1f%% of every point, rms %.1f mT"
      % (100 * np.abs(S.js(si_v) / js_v - 1).max(), 1000 * rms_ours))
print("   -> a free line through the handbook's own points: "
      "Js = %.3f - %.4f Si" % (b_free, -k_free))
print("      its intercept %.3f T vs %.4f for iron with the minor elements"
      " (%.3f pure)" % (b_free, S.JS_BASE, L.JS_FE))
print("   -> forcing the %.4f intercept instead gives k_Si = %.4f T/wt%%; "
      % (S.JS_BASE, k_fix) +
      "published\n      Maurer 0.073, Exner %.3f (used here), Hoselitz 0.024"
      % S.K_SI)

print("\n" + "=" * 76)
print("4. THE GRADE NAMES.  USS 1978 resistivity against the handbook's")
print("   range for the SAME grade name.\n")
print("   %-9s %8s %14s %10s   %s"
      % ("grade", "USS rho", "MH rho range", "MH %Si", "verdict"))
for r in S.table():
    g = r["name"]
    if g not in MH2:
        continue
    si, _, _, lo, hi = MH2[g]
    ok = lo <= r["rho"] <= hi
    print("   %-9s %8d %6d to %-6d %10.2f   %s"
          % (g, r["rho"], lo, hi, si,
             "inside" if ok else "OUTSIDE -- not the same material"))
print("   (M-45, M-47, Stator B, Q-Core and Type 2-S have no handbook row)")

print("\n" + "=" * 76)
print("5. THE HANDBOOK DISAGREES WITH ITSELF about silicon, between its")
print("   Fig. 17 code list and its Table 2:\n")
print("   %-7s %10s %10s %8s" % ("grade", "Fig. 17", "Table 2", "ratio"))
for g in ("M-43", "M-36", "M-27", "M-22", "M-19", "M-14"):
    print("   %-7s %10.2f %10.2f %8.2f"
          % (g, FIG17_SI[g], MH2[g][0], FIG17_SI[g] / MH2[g][0]))
print("   Table 2 is preferred: it is the properties table, and only its")
print("   numbers are consistent with the density and resistivity beside "
      "them.")
