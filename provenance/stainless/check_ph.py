# SPDX-License-Identifier: MIT
"""check_ph.py -- four checks on the PH stainless set.

1. The digitization against itself.  PH 13-8 Mo Figure 42 is the only one
   of the three charts that still shows its measured points; the drawn
   curve should pass through them.

2. The digitization against the bulletin's own number.  The same figure is
   annotated "Maximum Permeability = 130".

3. The two bulletins against each other.  17-4 PH Figure 3 and 15-5 PH
   Figure 7 draw the same four conditions; ph_build.py pools them, which
   is only honest if they agree.

4. The set against the annealed ferritic family (ss_build.py).  That
   family predicts a whole curve from a saturation and a maximum
   permeability, which these charts both give, so it can be asked to
   predict these five curves -- outside the range it was fitted over
   (mu_max 755-4064 against 138-445 here, and annealed ferrite against
   aged martensite).  At the knee it does no worse than it does on
   Carpenter's 1991 430 curves (RMS 0.12-0.26 in ln H here, 0.08-0.38
   there, 0.04-0.16 for its own members), but above the knee it misses:
   at 8 kA/m it is 0.17 T high on 13-8 Mo H 950 and 0.08 T high on the
   two H 1150 curves while running 0.03-0.08 T low on H 900 and H 1075,
   and the shape constant c_M = mu_max mu0 H50 / Js, 0.507 for the
   annealed family to within 3%, is 0.55-0.67 on these.  Since every PH
   condition here has a charted curve of its own, nothing is gained by
   borrowing a shape from annealed ferrite, and ph_build.py does not.

5. The entries against each other.  Nothing in the build ties the four
   conditions of an alloy together, so check that the field each needs
   for a given induction orders the way the charts do: H 1075/H 1100
   easiest, then H 900, then Condition A, then H 1150.

6. FEMM's stock "455 Stainless Steel", for information only.  Custom 455
   (S45500) has no magnetic data in anything held, and the standing
   decision is to leave that entry alone; PH 13-8 Mo is its nearest
   relative by chemistry (12.25-13.25 Cr, 7.5-8.5 Ni against 455's
   11-12.5 and 7.5-9.5), so the new H 950 curve is the first thing in
   this project that brackets it.  Printed, not acted on.

Usage:  python check_ph.py
"""
import csv
import json
import numpy as np
from collections import defaultdict

import ph_build as P

MU0 = 4e-7 * np.pi


def read():
    d = defaultdict(list)
    for r in csv.DictReader(open("out/ph_curves.csv")):
        d[(r["source"], r["curve"])].append((float(r["H_Am"]),
                                             float(r["B_T"])))
    return {k: np.array(sorted(v)).T for k, v in d.items()}


def main():
    d = read()
    n138 = "Cleveland-Cliffs ARMCO PH 13-8 Mo"

    print("1. PH 13-8 Mo Fig. 42, the drawn curve at its own plotted points")
    h, b = d[(n138, "H950")]
    hm, bm = d[(n138, "H950 (plotted points)")]
    k = (hm >= h[0]) & (hm <= h[-1])
    dev = np.interp(hm[k], h, b) - bm[k]
    print("   %d points, curve - point mean %+.4f T, max |%.4f| T"
          % (k.sum(), dev.mean(), np.abs(dev).max()))

    print("2. PH 13-8 Mo Fig. 42, maximum permeability")
    print("   drawn curve %.0f, the figure's annotation 130, "
          "the entry %.0f" % ((b / (MU0 * np.maximum(h, 1e-9))).max(),
                              json.load(open("out/ph_fit.json"))
                              ["138Mo_H950"]["mu_max"]))

    print("3. 17-4 PH Fig. 3 against 15-5 PH Fig. 7, condition by condition")
    for cond in P.CONDS:
        ha, ba = d[(P.SRC["17-4"], cond)]
        hb, bb = d[(P.SRC["15-5"], cond)]
        hq = np.logspace(np.log10(max(ha[0], hb[0])),
                         np.log10(min(ha[-1], hb[-1])), 400)
        db = np.interp(hq, ha, ba) - np.interp(hq, hb, bb)
        # above 0.6 T: below that the pair stands nearly vertical, where a
        # few mT in B is a large fraction in H and says nothing
        bq = np.linspace(max(ba[0], bb[0], 0.6), min(ba[-1], bb[-1]), 400)
        dh = np.log(np.interp(bq, ba, ha)) - np.log(np.interp(bq, bb, hb))
        print("   %-14s H %5.0f-%5.0f A/m: dB mean %+.4f max %.4f T, "
              "d ln H above 0.6 T mean %+.3f RMS %.3f"
              % (cond, hq[0], hq[-1], db.mean(), np.abs(db).max(),
                 dh.mean(), np.sqrt((dh ** 2).mean())))

    print("4. the annealed ferritic family against these curves")
    mst = json.load(open("out/ss_fit.json"))["master"]
    u, x, cm = np.array(mst["u"]), np.array(mst["x"]), mst["c_M"]
    for alloy, cond in ([("17-4", c) for c in P.CONDS]
                        + [("15-5", c) for c in P.CONDS]
                        + [("13-8", "H950")]):
        h, b = d[(P.SRC[alloy], cond)]
        j = b - MU0 * h
        js, _ = P.js_of(P.COMP[alloy])
        mumax = (b / (MU0 * np.maximum(h, 1e-9)))[b >= P.J_MIN].max()
        h50 = cm * js / (MU0 * (mumax - 1))
        k = (b >= P.J_MIN) & (j < 0.8 * js)
        e = np.log(h[k]) - np.log(h50 * np.exp(np.interp(j[k] / js, x,
                                                         np.log(u))))
        fam = js * np.interp(np.log(8e3 / h50), np.log(u), x)
        print("   %-5s %-14s Js %.3f mu_max %4.0f H50 %5.0f A/m | "
              "J at 8 kA/m %.3f vs family %.3f | knee ln H mean %+.2f "
              "RMS %.2f" % (alloy, cond, js, mumax, h50,
                            np.interp(8e3, h, j), fam, e.mean(),
                            np.sqrt((e ** 2).mean())))
    print("   (the family's c_M = %.3f; the same constant measured on these"
          % cm)
    print("    curves, mu_max mu0 H50 / Js with H50 read off the curve:")
    for alloy, cond in [("17-4", c) for c in P.CONDS] + [("13-8", "H950")]:
        h, b = d[(P.SRC[alloy], cond)]
        j = b - MU0 * h
        js, _ = P.js_of(P.COMP[alloy])
        mumax = (b / (MU0 * np.maximum(h, 1e-9)))[b >= P.J_MIN].max()
        h50 = float(np.interp(0.5 * js, j, h))
        print("    %-5s %-14s H50 %5.0f A/m, c = %.3f"
              % (alloy, cond, h50, mumax * MU0 * h50 / js))

    print("5. the entries of one alloy against each other, H for a given B")
    ent = json.load(open("out/ph_fit.json"))
    for alloy in ("17-4", "15-5"):
        tab = {}
        for tag, e in ent.items():
            if e["alloy"] != alloy:
                continue
            t = np.loadtxt("femm_ph_%s.csv" % tag, delimiter=",")
            tab[e["cond"]] = (t[:, 1], t[:, 0])
        bad = 0
        line = []
        for bq in (0.4, 0.8, 1.0, 1.2, 1.3):
            hh = [np.interp(bq, tab[c][1], tab[c][0]) for c in
                  ("H1075 & H1100", "H900", "Condition A", "H1150")]
            bad += int(not all(np.diff(hh) > 0))
            line.append("%.1f T: " % bq + "/".join("%.0f" % v for v in hh))
        print("   %-5s %s  inversions %d" % (alloy, "  ".join(line), bad))

    print("6. FEMM's stock 455 Stainless Steel against PH 13-8 Mo H 950 "
          "(information only)")
    try:
        import re
        txt = open(r"matlibs\matlib_femm42.dat",
                   encoding="utf-8", errors="replace").read()
        body = next(m.group(1) for m in
                    re.finditer(r"<BeginBlock>(.*?)<EndBlock>", txt, re.S)
                    if '"455 Stainless Steel"' in m.group(1))
        z = np.array([[float(v) for v in ln.split()] for ln in
                      body.split("<BHPoints>")[1].splitlines()[1:]
                      if ln.strip()])
        t = np.loadtxt("femm_ph_138Mo_H950.csv", delimiter=",")
        for hq in (2e3, 4e3, 8e3, 1.6e4, 2.9e4):
            print("   H %6.0f A/m: stock 455 %.3f T, 13-8 Mo H 950 %.3f T"
                  % (hq, np.interp(hq, z[:, 1], z[:, 0]),
                     np.interp(hq, t[:, 1], t[:, 0])))
    except (OSError, StopIteration):
        print("   (matlib.dat not readable here)")


if __name__ == "__main__":
    main()
