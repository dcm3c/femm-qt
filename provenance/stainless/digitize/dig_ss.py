# SPDX-License-Identifier: MIT
"""dig_ss.py -- DC magnetization curves of ferritic stainless steels, read
from the held vendor documents.

Carpenter, Chrome Core 12-FM datasheet, p. 5: "Typical DC normal induction
curves for bar product -- vs. 430FR stainless", fully annealed 0.250-0.625
in bars, Fahy permeameter.  Vector artwork, read as dig_nife.py reads the
nickel-iron sheets: each curve is its PDF path (dashed = Chrome Core 12-FM,
solid = 430FR Solenoid Quality); the axes are placed by the chart's own tick
marks, B linear 0-2.0 T, H logarithmic with decades at 0.1, 1, 10, 100 Oe.

Acciaierie Valbruna, "Soft Magnetic Steels" brochure, p. 4: MG2 (430F class,
1.2-1.5 Si) as cold drawn and as cold drawn plus magnetic annealed, on
linear axes 0-5000 A/m by 0-1.84 T in ten divisions.  A scanned page
(2481 x 1752 px at 150 dpi): each curve is picked out by its colour (green,
cold drawn; orange, annealed), and its centre line is the median of those
pixels by column where the curve is flat and by row where it is steep.
Panels, from their pale-green backgrounds and gridlines: x 506-1119 px for
0-5000 A/m (gridlines at 2000, 2500 and 3500 A/m fall at 751, 812 and 935
px, as they should); top panel y 393.5-746.5 px, middle panel y
815.5-1167.5 px for 1.84-0 T.  One pixel is 8.2 A/m and 5 mT.
The brochure's red marker line at 500 A/m (x 560-572 px) is anti-aliased
into orange at its edges and is masked; the curve on either side of it
covers the gap.

Usage:  python dig_ss.py   -> out/ss_curves.csv
        columns: source, fig, curve, condition, H_Am, B_T
"""
import csv
import io
import numpy as np
import pymupdf
from PIL import Image

from dig_nife import path_points, resample

OE = 1000 / (4 * np.pi)
BLUE = (0.0, 0.68, 0.94)

# Chrome Core 12-FM p. 5 axes, from the tick marks (PDF points)
CC_X = ([96.4, 164.9, 233.5, 301.3], [-1.0, 0.0, 1.0, 2.0])   # log10 Oe
CC_Y = ([436.0, 419.8, 403.0, 386.5, 370.0, 353.0, 336.7, 319.8, 303.5,
         286.8, 270.5], list(np.arange(0.0, 2.01, 0.2)))        # T


def read_carpenter():
    dr = pymupdf.open("Stainless/Chrome Core 12-FM.pdf")[4].get_drawings()
    out = []
    for dashed, name, cond in [
            (True, "Chrome Core 12-FM", "fully annealed bar, Fahy permeameter"),
            (False, "430FR Solenoid Quality",
             "fully annealed bar, Fahy permeameter")]:
        sel = [q for q in dr if tuple(round(x, 2) for x in q.get("color") or ())
               == BLUE and len(q["items"]) > 2
               and (q.get("dashes") not in (None, "[] 0")) == dashed]
        p = np.concatenate([path_points(q) for q in sel])
        h = 10 ** np.interp(p[:, 0], *CC_X) * OE
        b = np.interp(-p[:, 1], [-y for y in CC_Y[0]], CC_Y[1])
        o = np.argsort(h)
        out.append(("Carpenter Chrome Core 12-FM", "p5", name, cond,
                    h[o], b[o]))
    return out


def read_valbruna():
    doc = pymupdf.open("Stainless/Soft_Magnetic_Steels.pdf")
    xref = doc[3].get_images()[0][0]
    im = np.array(Image.open(io.BytesIO(doc.extract_image(xref)["image"]))
                  .convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    green = (g - r > 40) & (g - b > 30) & (g > 100)
    orange = (r > 190) & (r - b > 120) & (g > 80) & (g < 190)
    out = []
    for mask, y0, y1, top, bot, name, cond in [
            (green, 394, 747, 393.5, 746.5, "MG2 cold drawn",
             "annealed, cold drawn, ground"),
            (orange, 816, 1168, 815.5, 1167.5, "MG2 magnetic annealed",
             "annealed, cold drawn, magnetic annealed, ground")]:
        m = mask[y0:y1, 506:1120].copy()
        m[:, 560 - 506:573 - 506] = False     # red marker line at 500 A/m
        ys, xs = np.where(m)
        ys, xs = ys + y0, xs + 506
        pts = []
        for x in np.unique(xs):
            yy = ys[xs == x]
            if yy.max() - yy.min() < 10:
                pts.append((x, np.median(yy)))
        for y in np.unique(ys):
            xx = xs[ys == y]
            if xx.max() - xx.min() < 10:
                pts.append((np.median(xx), y))
        p = np.array(pts, float)
        h = (p[:, 0] - 506) / (1119 - 506) * 5000
        j = (bot - p[:, 1]) / (bot - top) * 1.84
        o = np.argsort(h)
        h, j = h[o], j[o]
        k = h > 0
        out.append(("Valbruna Soft Magnetic Steels", "p4", name, cond,
                    h[k], j[k]))
    return out


def main():
    rows = []
    for src, fig, name, cond, h, b in read_carpenter() + read_valbruna():
        h, b = resample(h, np.maximum.accumulate(b), 80)
        print("%-30s %-3s %-24s %3d pts  H %6.1f-%6.0f A/m  B %.3f-%.3f T"
              % (src, fig, name, len(h), h[0], h[-1], b[0], b[-1]))
        for hh, bb in zip(h, b):
            rows.append([src, fig, name, cond, "%.5g" % hh, "%.5g" % bb])
    with open("out/ss_curves.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["source", "fig", "curve", "condition", "H_Am", "B_T"])
        w.writerows(rows)
    print("wrote out/ss_curves.csv, %d rows" % len(rows))


if __name__ == "__main__":
    main()
