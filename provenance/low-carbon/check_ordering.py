# SPDX-License-Identifier: MIT
"""check_ordering.py -- does more annealing actually give a softer curve?

The three existing gates (B monotone, dB/dH >= mu0, single mu peak) all
act on one curve at a time.  Nothing checks the curves against each
other, and nothing in the v10 model ties the four heat-treatment
conditions together, so the fit is free to return a 1400 F curve that
needs more field at 15 kG than the 1300 F curve of the same grade --
which is what it did.

This is the missing gate.  For each grade it reads off the field needed
to reach a few fixed inductions and checks that it falls as the anneal
temperature rises.  Low field is dominated by the toe and knee, high
field by the wall-term tail, so an inversion that appears only at 1.5 T
and above is a tail problem, not a knee problem.

Two verdicts are printed.  "library" counts inversions only between the
two conditions the library actually carries (as-rolled and 1400 F);
"all" counts every adjacent pair.  When the fit splits the anneals at A1
(727 C, 1341 F), the pair that straddles it, 1300 F -> 1400 F, is counted
separately: above A1 pearlite re-forms on furnace cooling and pins harder,
so there the hotter anneal can be harder, as 1030's own data are (25.5 vs
30.6 Oe at 1.5 T).  Those crossings are reported, not failed.
Note that the US Steel charts
themselves show 1030 getting harder at 1.5 T from 1300 F to 1500 F, so
a perfect "all" score is not necessarily the right target for the
higher-carbon grades; "library" is the one that must be zero.

Usage:  python check_ordering.py [out/fit_params.json]
"""
import sys
import numpy as np
import bhlib as L
import bhmodel as MDL

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/fit_params.json"
F = MDL.Family(SRC)
NAM = {2: "as-rolled", 3: "1300F", 4: "1400F", 5: "1500F"}
ORDER = [2, 3, 4, 5]                    # increasing anneal temperature
LIBRARY_PAIR = (2, 4)
GRADES = [("1004", 0.040, 0.35), ("1010", 0.100, 0.45),
          ("1020", 0.200, 0.45), ("1030", 0.300, 0.75)]
LEVELS = [0.5, 1.0, 1.5, 1.8, 1.9]      # tesla

hg = np.logspace(-1, np.log10(4e5), 6000)
print("source: %s  (model %s)" % (SRC, F.version))
print("")
hdr = "  ".join("H@%.1fT" % t for t in LEVELS)
print("%-6s %-10s %s   %8s   %s" % ("grade", "cond", hdr, "mu_max",
                                    "tail kW*mW"))

bad_all, bad_lib, bad_a1 = 0, 0, 0
A1_SPLIT = len(F.par["lkW"]) == 3
for g, c, mn in GRADES:
    table = {}
    for d in ORDER:
        b = F.b(c, mn, d, hg)
        table[d] = [np.interp(t, b, hg) / L.OE for t in LEVELS]
        mu = (b / (L.MU0 * hg)).max()
        flag = ""
        k = ORDER.index(d)
        if k:
            prev = table[ORDER[k - 1]]
            inv = [i for i, (a, b_) in enumerate(zip(prev, table[d])) if b_ > a]
            if inv:
                across_a1 = A1_SPLIT and ORDER[k - 1] == 3 and d == 4
                flag = ("  <== harder than " + NAM[ORDER[k - 1]] + " at "
                        + ", ".join("%.1fT" % LEVELS[i] for i in inv)
                        + ("  (across A1: expected)" if across_a1 else ""))
                if across_a1:
                    bad_a1 += len(inv)
                else:
                    bad_all += len(inv)
        print("%-6s %-10s %s   %8.0f   %8.3f%s"
              % (g, NAM[d], "  ".join("%7.2f" % v for v in table[d]), mu,
                 F.tail(c, d), flag))
    inv = [i for i, (a, b_) in enumerate(zip(table[LIBRARY_PAIR[0]],
                                             table[LIBRARY_PAIR[1]])) if b_ > a]
    if inv:
        bad_lib += len(inv)
        print("       LIBRARY PAIR: 1400F harder than as-rolled at "
              + ", ".join("%.1fT" % LEVELS[i] for i in inv))
    print("")

print("inversions: %d (all adjacent pairs), %d (library pair as-rolled vs "
      "1400F)" % (bad_all, bad_lib)
      + ("; %d more across A1 (1300F -> 1400F), expected" % bad_a1
         if A1_SPLIT else ""))
sys.exit(1 if bad_lib else 0)
