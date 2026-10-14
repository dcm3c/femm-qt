# SPDX-License-Identifier: MIT
"""measured.py -- the digitized Armco entries, and per-entry conductivity.

Shared by emit_matlib.py and emit_fem.py so the two outputs cannot drift.

CONDUCTIVITY. The family previously emitted Sigma = 5.8 MS/m for all 14
entries, the stock FEMM value for plain carbon steel. The carbon grades now
take a per-grade value from composition (see GRADE_COMP below; bhcurves.tex,
section Electrical Conductivity). High-purity iron was the worst case: AK Steel Table 5 gives
10.7 uohm-cm at room temperature (9.6 at 0 C, Table 4), i.e. 9.35 MS/m -- 60%
higher. It only matters for eddy-current / AC work, where it scales loss
directly.

The Telar bulletin gives NO resistivity, so Telar's is computed from its
stated analysis with the U.S. Steel resistivity formula (Nonoriented Sheet
Steel for Magnetic Applications, 1978, p.116; Eq. (1) of sicurves), which is
linear in each element:

    rho [uOhm cm] = 10.45 + 6.0 C + 4.5 Mn + 13.7 P - 10.4 S + 11.6 Si
                    + 3.1 Cu + 2.4 Ni + 4.7 Cr + 2.8 Mo + 13.2 Al

At ARMCO grade 2's specification limits (C 0.010, Mn 0.100 -- bounds, so
the bound is used) it gives 11.0, within the formula's stated +/-0.7 of the
bulletin's own room-temperature 10.7, which is the check that it applies
here.  At
Telar 57S (C 0.02, Mn 0.425, P 0.02, S 0.0225, Si 0.08, Cu 0.07, Al 0.075;
middles of stated ranges, stated values otherwise) it gives 14.66 uOhm cm,
6.8 MS/m.  The single values are probably maxima, so the true figure may be
a little lower, 14.0-14.7 uOhm cm (6.8-7.1 MS/m).  Supplier web pages
quoting 0.24 Ohm mm^2/m (24 uOhm cm) are not credible for this analysis:
that is the resistivity of ~1% silicon steel, and pure iron itself only
reaches it near 200 C (Table 4).
"""
import numpy as np

SIGMA_CARBON = 5.8     # MS/m -- the old flat stock value; no longer used
SIGMA_PURE_FE = 9.35   # MS/m -- AK Steel Table 5, 10.7 uohm-cm at RT


def uss_resistivity(C=0.0, Mn=0.0, P=0.0, S=0.0, Si=0.0, Cu=0.0, Ni=0.0,
                    Cr=0.0, Mo=0.0, Al=0.0):
    """Resistivity [uOhm cm] from composition [wt%], US Steel 1978 p.116."""
    return (10.45 + 6.0 * C + 4.5 * Mn + 13.7 * P - 10.4 * S + 11.6 * Si
            + 3.1 * Cu + 2.4 * Ni + 4.7 * Cr + 2.8 * Mo + 13.2 * Al)


# CARBON GRADES. The flat 5.8 MS/m was FEMM's old stock value. ASM Handbook
# Vol. 1 (10th ed.), "Physical Properties of Carbon and Low-Alloy Steels",
# table "Electrical resistivities of carbon and low-alloy steels" (PDF
# p. 332), gives at 20 C: 1008 annealed 0.142, 1010 0.143 and 1030 annealed
# 0.166 uOhm m; its 1015-1029 rows (0.159-0.160) are at 0 C (footnote a) and
# come out near 17.1 uOhm cm at 20 C, above 1030 -- a different data set.
# The grades' resistivity is therefore taken from their composition by the
# USS formula, plus one offset fitted to the two annealed 20 C points
# (1008, 1030), which stands for residual Si, Cu etc. that the AISI grades
# do not specify and ARMCO iron lacks (the formula with no offset gives ARMCO
# grade 2's own 10.7). The offset is 1.26 uOhm cm, the fit is +-0.25 at both
# points, and it reproduces 1010's 14.3 exactly without being fitted to it.
# C and Mn are the middles used by the B-H fit (bh_fit_v10.py); P 0.02 and
# S 0.03 throughout (0.105 for resulfurized 1117).
GRADE_COMP = {"1004": (0.040, 0.35, 0.03), "1006": (0.060, 0.325, 0.03),
              "1008": (0.080, 0.40, 0.03), "1010": (0.100, 0.45, 0.03),
              "1018": (0.175, 0.75, 0.03), "1117": (0.180, 1.15, 0.105),
              "1020": (0.200, 0.45, 0.03), "1030": (0.300, 0.75, 0.03)}
ASM_20C = {"1008": 14.2, "1030": 16.6}      # uOhm cm, annealed, at 20 C


def _uss_grade(g):
    c, mn, s = GRADE_COMP[g]
    return uss_resistivity(C=c, Mn=mn, P=0.02, S=s)


RHO_OFFSET = sum(ASM_20C[g] - _uss_grade(g) for g in ASM_20C) / len(ASM_20C)


def rho_grade(g):
    """Room-temperature resistivity [uOhm cm] of an AISI carbon grade."""
    return _uss_grade(g) + RHO_OFFSET


def sigma_grade(g):
    """Conductivity [MS/m] of an AISI carbon grade, to 2 decimals."""
    return round(100.0 / rho_grade(g), 2)


RHO_TELAR = uss_resistivity(C=0.02, Mn=0.425, P=0.02, S=0.0225, Si=0.08,
                            Cu=0.07, Al=0.075)         # 14.66 uOhm cm
SIGMA_TELAR = round(100.0 / RHO_TELAR, 1)              # 6.8 MS/m

MEASURED = [
    dict(
        name="ARMCO Pure Iron (AK Steel data)",
        csv="../iron/femm_ArmcoPureIron.csv",
        sigma=SIGMA_PURE_FE,
        note=("From the AK Steel ARMCO Pure Iron bulletin, two figures. "
              "Figure 7 (after recrystallization anneal), digitized "
              "5-17186 A/m to B=2.00 T, is the curve itself over that "
              "whole range. Above it, Figure 8 (high-induction intrinsic "
              "curve, specimen condition not stated, digitized to "
              "786 kA/m), which runs 47.6 mT lower at 17 kA/m and "
              "converges from there -- a difference in wall pinning -- so "
              "that gap is added back decaying as 1/H (Bozorth's a0/H). "
              "No Akulov extension is needed below the table's 4e5 A/m; "
              "Figure 8 reaches J=2.153 T against the 2.15 T the bulletin "
              "states. Figure 7 is corroborated by Figure 15 curve C "
              "(820 C, 3 mm hot rolled, ASTM A341) to RMS ln H 0.041 "
              "over 0.01-1.49 T. "
              "Composition Gr.2: C<=0.010, Mn<=0.100, min 99.85% Fe."),
    ),
    dict(
        name="ARMCO Telar 57 (AK Steel data, 3-bar mean)",
        csv="../iron/femm_Telar57.csv",
        sigma=SIGMA_TELAR,
        note=("Digitized from AK Steel ARMCO Telar 57 bulletin Figures "
              "10/11/12 (8/10/12 mm cold drawn bar after magnetic anneal, "
              "815 C / 1.5 h / 100% dry H2), IEC 60404-4, 2-sample average; "
              "geometric mean in H of the three diameters, which scatter "
              "9% median / 14% max. The 4 mm bar is excluded as the "
              "bulletin says it is not representative. The mean covers "
              "1-9551 A/m to B=1.83 T. Above it, the Figure 5 "
              "high-induction curve (to 68 kA/m), whose gap to the mean "
              "there (-2.5 mT) is added back decaying as 1/H. Above "
              "68 kA/m, the top ten table points, the Akulov 1/H^2 "
              "approach to Js=2.137 T from this project's composition "
              "law, with Si 0.08 and Al 0.075 -- extrapolated, not "
              "measured. Composition 57S: "
              "C<=0.02, Mn 0.35-0.5, Al 0.05-0.1."),
    ),
]


def sigma_for_raw(name):
    """Conductivity for a raw digitized bulletin curve, by its name."""
    return SIGMA_TELAR if "Telar" in name else SIGMA_PURE_FE


def sigma_for(grade_name):
    """Conductivity for a v10 family entry, by grade name."""
    if "Armco" in grade_name:
        return SIGMA_PURE_FE
    return sigma_grade(grade_name.strip()[:4])


def load_table(csv):
    """(B, H) columns of an emitted FEMM table."""
    d = np.loadtxt(csv, delimiter=",")
    return d[:, 0], d[:, 1]
