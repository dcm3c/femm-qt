# SPDX-License-Identifier: MIT
"""si_composition.py -- composition and saturation level of each grade from
the resistivity and density the US Steel book states for it.

The book (Nonoriented Sheet Steel for Magnetic Applications, USS,
ADUSS 31-7103-01, May 1978) gives no chemistry.  It gives, per grade, a
resistivity to 1 uOhm-cm and a density to 0.05 g/cm3 (transcribed by
the user into SiliconSteel/density.xlsx), and in Section 5 the two
formulas relating them to composition.  Both formulas, the coefficients
and the grade table now live in silib.py; this script is the audit that
reads them out.

In principle the pair of formulas is a 2x2 system in (Si, Al).  In practice
the book's densities are the ASTM assumed values of the p.115 table, quoted
by silicon band in 0.05 steps, so they carry no information beyond the band
and the system collapses.  Resistivity alone still gives the one combination
the magnetics need: Si + 1.14 Al, which is within 1% of the saturation-
equivalent silicon Si + 1.15 Al on Exner's coefficients.  The density band
then bounds Al from above.

The level each grade's own curve reaches (J at the last digitized point,
divided by the Akulov factor for that grade's own anisotropy field) is
compared with the level its resistivity implies.  Where the two agree the
book is self-consistent; where the curve falls short the page runs low at
the top, which is the same audit the low-carbon work applied.

Usage:  python si_composition.py
"""
import numpy as np

import bhlib as L
import silib as S

rows = S.table()

print("minor elements fixed at C %.3f, Mn %.2f, S %.3f -> base %.2f uOhm-cm; "
      "k_Si %.3f, k_Al %.3f T/wt%%" % (S.C, S.MN, S.S, S.RHO0, S.K_SI, S.K_AL))
print("%-9s %4s %5s   %-13s %-14s %-9s %8s %9s %6s"
      % ("grade", "rho", "dens", "Si+1.14Al", "band Si+1.7Al", "Al <=",
         "Js(rho)", "Js(curve)", "ratio"))
for r in rows:
    flag = "  <== curve low" if r["low"] else ""
    print("%-9s %4d %5.2f   %4.2f +/- %.2f   %4.2f - %4.2f     %4.2f     "
          "%.3f     %.3f  %.3f%s"
          % (r["name"], r["rho"], r["dens"], r["sieq"], r["sig_sieq"],
             r["band"][0], r["band"][1], S.al_bound(r["sieq"], r["band"]),
             r["js_rho"], r["js_curve"], r["js_curve"] / r["js_rho"], flag))

good = [r for r in rows if not r["low"]]
x = np.array([r["sieq"] for r in good])
y = np.array([S.JS_BASE - r["js_curve"] for r in good])
k = float(np.sum(x * y) / np.sum(x * x))
res = y - k * x
print("\nk_Si fitted through the origin over the %d self-consistent grades: "
      "%.4f T/wt%% (rms residual %.3f T); published Maurer 0.073, Exner 0.048, "
      "Hoselitz 0.024" % (len(good), k, float(np.sqrt(np.mean(res ** 2)))))

print("\nanisotropy from Bozorth Fig. 12-16, K1 = K1_Fe (1 - Si/%.1f):" % S.SI0)
print("%-9s %9s %9s %9s %9s" % ("grade", "K1 J/m3", "Ha A/m", "Ha Oe",
                                "Hend/Ha"))
for r in rows:
    print("%-9s %9.0f %9.0f %9.0f %9.2f"
          % (r["name"], r["k1"], r["ha"], r["ha"] / L.OE, r["h_last"] / r["ha"]))
