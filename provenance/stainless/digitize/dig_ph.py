# SPDX-License-Identifier: MIT
"""dig_ph.py -- DC normal induction curves of the precipitation-hardening
stainless steels, read from the Cleveland-Cliffs (ARMCO) product bulletins.

The three bulletins that carry magnetic curves draw them as PDF paths, so
nothing here is traced from pixels; each curve is picked out by its stroke
colour (navy, red, grey) and by whether the path is dashed, and the axes are
placed from the chart's own tick marks.  All three charts are linear in both
B and H.

  17-4 PH (S17400), clf_datasheet_armco_17-4_ph_pdb_euro_102022_89.pdf p. 15,
    Figure 3: four conditions, 0-140 Oe by 0-14 kG.
  15-5 PH (S15500), clf_datasheet_armco_15-5_ph_pdb_euro_072022_93.pdf p. 19,
    Figure 7: the same four conditions and the same axes, dual-labelled in
    A/m and T.
  PH 13-8 Mo (S13800), clf_productdata__armco_ph_13-8_mo_pdb_euro_final_
    102022_95.pdf p. 36, Figure 42: Condition H 950 only, 0-200 Oe by
    0-1.3 T, and the only one of the three that still shows its measured
    points (small open circles, extracted here as well).

Two warnings about this artwork, both visible in the output:

  * The bulletins are modern redraws of old charts.  On the 15-5 chart the
    H 900 and Condition A curves are simply not drawn below about 13 and 10
    Oe; on the 17-4 chart all four are drawn to the origin but the two
    hardest ones stand nearly vertical between 11 and 13 Oe, which no
    magnetization curve does.  Below ~15 Oe (~1200 A/m) these curves are
    draughtsmanship, not data, and ph_build.py does not fit them there.
  * The 13-8 Mo curve runs to 432 pt where its frame ends at 485 pt, i.e. it
    stops at about 168 Oe, not at the 200 Oe the axis is drawn for.

The hysteresis figures (17-4 Figure 4, 15-5 Figure 8) are read too, for the
coercive force alone -- a check on the normal curves, not an entry.

Usage:  python dig_ph.py   -> out/ph_curves.csv   (normal induction)
                              out/ph_hyst.csv     (Hc, Br from the loops)
"""
import csv
import numpy as np
import pymupdf

from dig_nife import path_points, resample

OE = 1000 / (4 * np.pi)          # A/m per oersted
NAVY = (0.0, 0.24, 0.38)
RED_174 = (0.77, 0.06, 0.23)
RED_155 = (0.76, 0.12, 0.2)
GREY = (0.43, 0.43, 0.44)

PH174 = "Stainless/clf_datasheet_armco_17-4_ph_pdb_euro_102022_89.pdf"
PH155 = "Stainless/clf_datasheet_armco_15-5_ph_pdb_euro_072022_93.pdf"
PH138 = "Stainless/clf_productdata__armco_ph_13-8_mo_pdb_euro_final_102022_95.pdf"

# Tick positions in PDF points, read off the tick marks drawn with each
# chart, against the values their labels carry.
FIG3_X = ([94.0, 125.1, 156.8, 188.3, 219.7, 250.8, 282.6, 313.6],
          [0, 20, 40, 60, 80, 100, 120, 140])                    # Oe
FIG3_Y = ([479.8, 449.0, 417.6, 386.2, 354.8, 323.4, 292.0, 260.8],
          [0, 2, 4, 6, 8, 10, 12, 14])                           # kG
FIG4_X = ([108.7, 133.9, 163.5, 196.1, 227.9, 259.7, 290.9],
          [-40, -20, 0, 20, 40, 60, 80])
FIG4_Y = ([754.5, 724.1, 694.0, 663.1, 632.3, 601.5, 570.8],
          [-2, 0, 2, 4, 6, 8, 10])
FIG7_X = ([389.7, 418.1, 446.9, 475.7, 504.8, 533.0, 560.7],
          [20, 40, 60, 80, 100, 120, 140])
FIG7_Y = ([447.7, 418.9, 390.1, 361.3, 333.0, 304.0, 274.9],
          [2, 4, 6, 8, 10, 12, 14])
FIG8_X = ([389.8, 418.8, 447.9, 476.4, 505.5, 534.9],
          [-20, 0, 20, 40, 60, 80])
FIG8_Y = ([708.7, 680.0, 651.1, 621.9, 591.6, 564.3],
          [0, 2, 4, 6, 8, 10])
FIG42_X = ([169.9, 186.8, 203.2, 219.4, 236.3, 252.9, 269.4, 286.0, 302.5,
            319.8, 336.4, 352.9, 369.6, 386.1, 402.3, 418.8, 436.1, 452.3,
            469.2], list(range(10, 200, 10)))
FIG42_Y = ([384.4, 367.8, 351.2, 334.7, 317.8, 301.7, 285.4, 268.4, 251.5,
            235.3, 218.8, 201.8], [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12])


def scale(ticks, name):
    """Least-squares page-coordinate -> value line through the tick marks."""
    c, v = np.array(ticks[0], float), np.array(ticks[1], float)
    a, b = np.polyfit(c, v, 1)
    r = np.abs(a * c + b - v).max() / abs(a)
    print("  %-8s %7.4f pt per unit, worst tick off by %.2f pt" % (name, 1 / a, r))
    return lambda x: a * np.asarray(x) + b


def groups(f, page, frame, colour, broken=None):
    """Stroked paths of one colour whose bounding box lies in the frame.
    broken True/False selects the dashed/continuous curve, None takes both.
    A curve counts as broken if the path carries a dash pattern or if it is
    drawn as several disjoint pieces; flat legend samples are dropped."""
    out = []
    for q in pymupdf.open(f)[page - 1].get_drawings():
        if tuple(round(x, 2) for x in q.get("color") or ()) != colour:
            continue
        r = q["rect"]
        if not (frame[0] - 2 <= r.x0 and r.x1 <= frame[2] + 2
                and frame[1] - 2 <= r.y0 and r.y1 <= frame[3] + 2):
            continue
        if r.height < 1.0:                       # legend line sample
            continue
        p = path_points(q)
        if p.ndim != 2 or len(p) < 2:
            continue
        gap = np.diff(np.sort(p[:, 0])).max()
        cut = (q.get("dashes") or "[] 0").strip() != "[] 0" or gap > 10
        if broken is not None and cut != broken:
            continue
        out.append(p)
    return np.concatenate(out)


# (file, page, frame, xticks, yticks, B unit, [(colour, dashed, curve, cond)])
NORMAL = [
    (PH174, 15, (93.9, 260.0, 315.4, 481.1), FIG3_X, FIG3_Y, 0.1, "Fig. 3", [
        (NAVY, False, "H1075 & H1100", "H 1075 / H 1100 aged"),
        (RED_174, None, "H900", "H 900 aged"),
        (GREY, None, "Condition A", "solution annealed"),
        (NAVY, True, "H1150", "H 1150 aged")]),
    (PH155, 19, (360.6, 273.9, 560.4, 476.8), FIG7_X, FIG7_Y, 0.1, "Fig. 7", [
        (NAVY, False, "H1075 & H1100", "H 1075 / H 1100 aged"),
        (RED_155, None, "H900", "H 900 aged"),
        (GREY, None, "Condition A", "solution annealed"),
        (NAVY, True, "H1150", "H 1150 aged")]),
    (PH138, 36, (148.0, 186.8, 484.9, 407.0), FIG42_X, FIG42_Y, 0.1, "Fig. 42", [
        (NAVY, False, "H950", "H 950 aged")]),
]
NAME = {PH174: "Cleveland-Cliffs ARMCO 17-4 PH",
        PH155: "Cleveland-Cliffs ARMCO 15-5 PH VAC CE",
        PH138: "Cleveland-Cliffs ARMCO PH 13-8 Mo"}

# (file, page, frame, xticks, yticks, [(colour, dashed, curve)])
HYST = [
    (PH174, 15, (102.3, 551.9, 319.6, 776.0), FIG4_X, FIG4_Y, "Fig. 4", [
        (NAVY, False, "H1075"), (RED_174, None, "H1100 & H900"),
        (GREY, None, "Condition A"), (NAVY, True, "H1150")]),
    # Figure 8's legend pairs the conditions differently from Figure 7's,
    # and reverses the two navy curves: here the dashes are H 1100 & H 900
    # and the solid navy line is H 1150.
    (PH155, 19, (359.8, 549.7, 547.8, 758.3), FIG8_X, FIG8_Y, "Fig. 8", [
        (NAVY, True, "H1100 & H900"), (RED_155, None, "H1075"),
        (GREY, None, "Condition A"), (NAVY, False, "H1150")]),
]


def crossings(u, v, jump=2000.0):
    """v interpolated where u changes sign, walking the path in drawing
    order.  Steps longer than jump are joins between dashes, not curve."""
    out = []
    for i in range(len(u) - 1):
        a, c = u[i], u[i + 1]
        if (a > 0) == (c > 0) or a == c or abs(v[i + 1] - v[i]) > jump:
            continue
        out.append(v[i] + (v[i + 1] - v[i]) * (0 - a) / (c - a))
    return np.array(out)


def markers(f, page, frame):
    """Centres of the small open circles that carry the 13-8 Mo points."""
    c = []
    for q in pymupdf.open(f)[page - 1].get_drawings():
        r = q["rect"]
        if q["type"] == "fs" and 2 < r.width < 5 and 2 < r.height < 5 \
                and frame[0] <= r.x0 and r.x1 <= frame[2] \
                and frame[1] <= r.y0 and r.y1 <= frame[3]:
            c.append(((r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2))
    return np.array(sorted(c))


def read_normal():
    rows, curves = [], []
    for f, page, fr, xt, yt, unit, fig, want in NORMAL:
        print("%s %s" % (NAME[f], fig))
        fx, fy = scale(xt, "H, Oe"), scale(yt, "B, kG")
        for colour, dashed, name, cond in want:
            p = groups(f, page, fr, colour, dashed)
            h = fx(p[:, 0]) * OE
            b = fy(p[:, 1]) * unit           # the fit absorbs y growing down
            o = np.argsort(h)
            h, b = h[o], np.maximum.accumulate(b[o])
            h, b = np.maximum(h, 0.0), np.maximum(b, 0.0)
            print("    %-14s %-22s %3d pts  H %5.0f-%5.0f A/m  B %.3f-%.3f T"
                  % (name, cond, len(h), h[0], h[-1], b[0], b[-1]))
            curves.append((NAME[f], fig, name, cond, h, b))
    for src, fig, name, cond, h, b in curves:
        hs, bs = resample(np.maximum(h, 1e-3), np.maximum(b, 1e-4), 90)
        for hh, bb in zip(hs, bs):
            rows.append([src, fig, name, cond, "%.5g" % hh, "%.5g" % bb])
    # the 13-8 Mo measured points, as drawn
    fx, fy = scale(FIG42_X, "H, Oe"), scale(FIG42_Y, "B, kG")
    m = markers(PH138, 36, (148.0, 186.8, 484.9, 401.0))
    h, b = fx(m[:, 0]) * OE, fy(m[:, 1]) * 0.1
    print("  %-28s %3d plotted points, H %5.0f-%5.0f A/m"
          % ("PH 13-8 Mo Fig. 42 markers", len(h), h[0], h[-1]))
    for hh, bb in zip(h, b):
        rows.append([NAME[PH138], "Fig. 42 points", "H950 (plotted points)",
                     "H 950 aged", "%.5g" % hh, "%.5g" % bb])
    return rows


def read_hyst():
    rows = []
    for f, page, fr, xt, yt, fig, want in HYST:
        print("%s %s" % (NAME[f], fig))
        fx, fy = scale(xt, "H, Oe"), scale(yt, "B, kG")
        for colour, dashed, name in want:
            p = groups(f, page, fr, colour, dashed)
            h, b = fx(p[:, 0]) * OE, fy(p[:, 1]) * 0.1
            hc = np.mean(np.abs(crossings(b, h)))   # |H| where B = 0
            br = np.mean(np.abs(crossings(h, b)))   # |B| where H = 0
            print("    %-14s Hc %5.0f A/m (%4.1f Oe)   Br %.2f T   "
                  "(%d, %d crossings)" % (name, hc, hc / OE, br,
                                          len(crossings(b, h)),
                                          len(crossings(h, b))))
            rows.append([NAME[f], fig, name, "%.4g" % hc, "%.4g" % (hc / OE),
                         "%.3g" % br])
    return rows


def main():
    rows = read_normal()
    with open("out/ph_curves.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["source", "fig", "curve", "condition", "H_Am", "B_T"])
        w.writerows(rows)
    print("wrote out/ph_curves.csv, %d rows" % len(rows))
    hy = read_hyst()
    with open("out/ph_hyst.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["source", "fig", "curve", "Hc_Am", "Hc_Oe", "Br_T"])
        w.writerows(hy)
    print("wrote out/ph_hyst.csv, %d rows" % len(hy))


if __name__ == "__main__":
    main()
