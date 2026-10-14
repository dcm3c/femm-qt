# SPDX-License-Identifier: MIT
"""dig_alnico.py -- typical demagnetization curves of cast alnico, read from
Arnold Magnetic Technologies' brochure "Cast Alnico Permanent Magnets"
(Magnets/Cast-Alnico-Permanent-Magnet-Brochure-101117.pdf, Rev. C, Feb. 2003).

The brochure draws its curves as PDF paths, so nothing here is traced from
pixels: the curves are the blue 1-pt strokes (the black strokes are the
energy-product hyperbolas and the grid), Bezier segments are sampled densely,
and the axes are placed from the chart's own tick labels (H along the foot,
B in kG up the right side; both linear).

  p. 3 (6.3): Alnico 2, 3, 4                       0-1000 Oe by 0-14 kG
  p. 5 (6.5): Alnico 5, 5cc, 6, ArKomax 800,
              ArKomax 800 Hi-Hc                    0-1000 Oe by 0-14 kG
  p. 7 (6.7): Alnico 8B, 8HE, 8H, 9                0-2000 Oe by 0-14 kG

The curves carry no labels a program can attach (the grade names are rotated
text beside them), so each path is named by its end points, Br at H = 0 and
Hc at B = 0, against the typical-values table on the same page (TABLE below,
also printed there: (BH)max, Bd at (BH)max, recoil permeability).  Every
path must land within 2% of one table row in both, or the script stops.

Two warnings about this artwork:

  * Every curve is drawn arriving flat at H = 0: its slope over the last few
    per cent of Hc is mu_r 0.3-0.5, which no magnet has (dB/dH >= mu0 on a
    descending branch).  Treat the first ~5% of Hc as draughtsmanship.
  * The curves stop at the chart frame, B = 0.027 kG (2.7 mT) rather than 0;
    Hc is taken from the path's own end, extrapolated linearly to B = 0.

Usage:  python dig_alnico.py   -> out/alnico_arnold_curves.csv
"""
import csv
import numpy as np
import pymupdf

SRC = "Magnets/Cast-Alnico-Permanent-Magnet-Brochure-101117.pdf"
OUT = "out/alnico_arnold_curves.csv"
BLUE = (0.0, 0.6007782220840454, 0.8484473824501038)
OE = 1000.0 / (4.0 * np.pi)          # A/m per Oe

# Arnold's typical values, pp. 3, 5, 7:
# grade: (page, Br G, Hc Oe, (BH)max MGOe, Bd G, recoil permeability)
TABLE = {
    "Alnico 2": (3, 7200, 560, 1.60, 4400, 6.2),
    "Alnico 3": (3, 7000, 475, 1.40, 4450, 5.1),
    "Alnico 4": (3, 5500, 720, 1.35, 3100, 4.1),
    "Alnico 5": (5, 12500, 640, 5.50, 10000, 3.7),
    "Alnico 5cc": (5, 13200, 675, 6.50, 11000, 2.4),
    "Alnico 6": (5, 10800, 750, 3.90, 7400, 5.6),
    "ArKomax 800": (5, 13700, 740, 8.10, 12000, 2.0),
    "ArKomax 800 Hi-Hc": (5, 13200, 810, 8.10, 11200, 2.0),
    "Alnico 8B": (7, 8300, 1650, 5.50, 5000, 2.0),
    "Alnico 8HE": (7, 9300, 1550, 6.00, 5750, 2.0),
    "Alnico 8H": (7, 7400, 1900, 5.50, 4400, 2.0),
    "Alnico 9": (7, 11200, 1375, 10.50, 8900, 1.3),
}


def bez(p0, p1, p2, p3, n=24):
    t = np.linspace(0, 1, n)[:, None]
    P = [np.array([q.x, q.y]) for q in (p0, p1, p2, p3)]
    return ((1 - t) ** 3 * P[0] + 3 * (1 - t) ** 2 * t * P[1]
            + 3 * (1 - t) * t ** 2 * P[2] + t ** 3 * P[3])


def axes(page):
    """x -> -H (Oe) and y -> B (kG), from the tick labels."""
    nums = [(w, float(w[4])) for w in page.get_text("words")
            if w[4].replace(".", "").isdigit()]
    zero = max((w for w, v in nums if w[4] == "0"), key=lambda w: w[1])
    ylab = zero[1]
    hl = [(0.5 * (w[0] + w[2]), v) for w, v in nums if abs(w[1] - ylab) < 2]
    cand = [(w, v) for w, v in nums if w[0] > 400 and ylab - 400 < w[1] < ylab - 5
            and v in (2, 4, 6, 8, 10, 12, 14)]
    xr = max(w[0] for w, v in cand)
    bl = [(0.5 * (w[1] + w[3]), v) for w, v in cand if abs(w[0] - xr) < 8]
    assert len(hl) == 11 and len(bl) == 7, (len(hl), len(bl))
    return (np.polyfit(*zip(*hl), 1), np.polyfit(*zip(*bl), 1))


def paths(page):
    out = []
    for d in page.get_drawings():
        if d.get("color") != BLUE or abs((d.get("width") or 0) - 1.0) > 0.01:
            continue
        cur, last = [], None
        for it in d["items"]:
            if it[0] == "l":
                seg = np.array([[it[1].x, it[1].y], [it[2].x, it[2].y]])
            elif it[0] == "c":
                seg = bez(*it[1:5])
            else:
                continue
            if last is not None and np.hypot(*(seg[0] - last)) > 0.5:
                out.append(np.array(cur))
                cur = []
            cur += list(seg if not cur else seg[1:])
            last = seg[-1]
        if cur:
            out.append(np.array(cur))
    return out


def main():
    doc = pymupdf.open(SRC)
    rows, seen = [], set()
    for pno in (3, 5, 7):
        page = doc[pno - 1]
        hx, by = axes(page)
        for p in paths(page):
            H = -np.polyval(hx, p[:, 0])                 # Oe, negative
            B = np.polyval(by, p[:, 1])                  # kG
            o = np.argsort(H)
            H, B = H[o], B[o]
            br = np.interp(0, H, B)
            hc = -(H[0] - B[0] * (H[1] - H[0]) / (B[1] - B[0]))
            name = [g for g, t in TABLE.items() if t[0] == pno
                    and abs(br * 1e3 / t[1] - 1) < 0.02 and abs(hc / t[2] - 1) < 0.02]
            assert len(name) == 1, "p.%d path Br %.0f G Hc %.0f Oe: %s" % (pno, br * 1e3, hc, name)
            name = name[0]
            assert name not in seen, name
            seen.add(name)
            t = TABLE[name]
            print("p.%d  %-18s Br %5.0f G (table %5d)  Hc %4.0f Oe (table %4d)  %3d points"
                  % (pno, name, br * 1e3, t[1], hc, t[2], len(H)))
            rows += [(name, "p. %d" % pno, "%.6g" % (h * OE), "%.6g" % (b / 10))
                     for h, b in zip(H, B)]
    assert seen == set(TABLE), set(TABLE) - seen
    with open(OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["grade", "page", "H_Am", "B_T"])
        w.writerows(rows)
    print("wrote %s (%d rows, %d curves)" % (OUT, len(rows), len(seen)))


if __name__ == "__main__":
    main()
