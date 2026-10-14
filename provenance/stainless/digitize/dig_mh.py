# SPDX-License-Identifier: MIT
"""dig_mh.py -- digitize Metals Handbook Fig. 17 curves (bhcurves.jpg).

Source: Metals Handbook, 8th ed., Vol. 1 (ASM), Fig. 17 p.792, "Direct-
current magnetization curves for various magnetic materials": intrinsic
induction B_i = B - H in kilogausses against H in oersteds on a log axis,
0.01 to 4000 Oe, 36 numbered curves.  FEMM's "Metals Handbook DC
Magnetization Curves" folder is a digitization of this figure; this
script makes our own, so the provenance is ours and FEMM's can be checked.

CALIBRATION from the grid, not assumed: vertical rules at 1, 2, 4, 6, 8
per decade, decades starting at x = 97, 538, 986, 1428, 1874, 2317 px for
0.01 ... 1000 Oe (445 px per decade); horizontal rules every kilogauss, 0
at y = 1950.5 px, 26 at 33.5 px.  Both are least-squares fits to the rule
positions found in the image.

TRACING.  The curves cross one another many times between 1 and 20 Oe,
so a curve is followed column by column inside a search window centred on
a GUIDE -- FEMM's table for the same curve, converted to B_i in kG.  The
guide only says where to look: within the window, the ink cluster nearest
the previous point is taken and its centroid recorded, so where the ink
is clear the trace is independent of the guide, and its departures from
it are the measure of FEMM's digitization.  Grid rules are masked first.
Columns with no ink in the window (a crossing that merges strokes, a
label) are skipped.

Usage:  python dig_mh.py      -> out/mh_<curve>_raw.csv (H_Oe, Bi_kG),
                                 out/mh_<curve>_overlay.png
"""
import re
import sys
import numpy as np
from PIL import Image, ImageDraw

IMG = "bhcurves.jpg"
MU0 = 4e-7 * np.pi
OE = 1000.0 / (4 * np.pi)

a = np.asarray(Image.open(IMG).convert("L")).astype(float)
NY, NX = a.shape
dark = a < 110

# ---- calibration ----------------------------------------------------------
def runs(c, thr):
    idx = np.nonzero(c > thr)[0]
    g = np.split(idx, np.nonzero(np.diff(idx) > 2)[0] + 1)
    return [(x[0] + x[-1]) / 2.0 for x in g if len(x)]

vx = runs(dark[150:1900, :].sum(0), 0.55 * 1750)
hy = runs(dark[:, 150:2550].sum(1), 0.55 * 2400)
DEC = [97.0, 538.5, 986.5, 1428.5, 1874.5, 2317.5]           # 0.01..1000 Oe
dec_x = [min(vx, key=lambda v: abs(v - d)) for d in DEC]
bx, ax_ = np.polyfit(np.arange(-2, 4), dec_x, 1)              # x = ax_ + bx*log10 H
kg_y = sorted(hy, reverse=True)[:27]                          # 0..26 kG
by, ay = np.polyfit(np.arange(len(kg_y)), kg_y, 1)            # y = ay + by*kG
X = lambda h: ax_ + bx * np.log10(h)
Hx = lambda x: 10 ** ((x - ax_) / bx)
Y = lambda k: ay + by * k
Ky = lambda y: (y - ay) / by
print("calibration: %.1f px/decade, %.2f px/kG; rules found %d vertical, %d horizontal"
      % (bx, -by, len(vx), len(hy)))

# mask grid rules (3 px either side of each found rule)
ink = dark.copy()
for v in vx:
    ink[:, int(v) - 3:int(v) + 4] = False
for h in hy:
    ink[int(h) - 3:int(h) + 4, :] = False


def stock_guide(name):
    t = open("out/matlib.dat", encoding="utf-8", errors="replace").read()
    m = re.search(r'<BlockName> = "%s"(.*?)<EndBlock>' % re.escape(name), t,
                  re.S).group(1)
    n = int(re.search(r"<BHPoints> = (\d+)", m).group(1))
    rows = [l.split() for l in m[m.index("<BHPoints>"):].splitlines()[1:]
            if l.strip()][:n]
    d = np.array(rows, float)[1:]
    b, h = d[:, 0], d[:, 1]
    return h / OE, (b - MU0 * h) * 10.0            # Oe, B_i in kG


def _nearest(line, lo, ref, maxw=14):
    """Centroids of the ink clusters in a 1-D slice, the one nearest ref."""
    idx = np.nonzero(line)[0]
    if idx.size == 0:
        return None
    cl = np.split(idx, np.nonzero(np.diff(idx) > 2)[0] + 1)
    cen = [lo + c.mean() for c in cl if 2 <= len(c) <= maxw]
    return min(cen, key=lambda c: abs(c - ref)) if cen else None


def trace(curve, stock_name, h_lo, h_hi, win=18):
    """Scan COLUMNS where the guide is shallower than 45 degrees in the
    image and ROWS where it is steeper, so each slice cuts the stroke
    across its width instead of along it."""
    gh, gk = stock_guide(stock_name)
    gx, gy = X(gh), Y(gk)                     # guide in pixels
    k = (gh >= h_lo) & (gh <= h_hi)
    gx, gy = gx[k], gy[k]
    xs = np.arange(int(gx[0]), int(gx[-1]))
    ys = np.interp(xs, gx, gy)                # guide y at every column
    slope = np.gradient(ys)
    pts = []
    for x, yg, s in zip(xs, ys, slope):
        if abs(s) <= 1.0:                     # shallow: scan the column
            lo = int(yg - win)
            y = _nearest(ink[lo:lo + 2 * win, x], lo, yg)
            if y is not None:
                pts.append((x, y, yg))
    # steep: scan rows between the guide's y at the ends of steep stretches
    steep = np.abs(slope) > 1.0
    if steep.any():
        yrow = np.arange(int(ys[steep].min()), int(ys[steep].max()) + 1)
        order = np.argsort(ys[steep])
        xg_of_y = np.interp(yrow, ys[steep][order], xs[steep][order])
        for y, xg in zip(yrow, xg_of_y):
            lo = int(xg - win)
            x = _nearest(ink[y, lo:lo + 2 * win], lo, xg)
            if x is not None:
                pts.append((x, y, np.interp(x, xs, ys)))
    pts = np.array(sorted(pts))
    p = np.column_stack([Hx(pts[:, 0]), Ky(pts[:, 1]), Ky(pts[:, 2])])
    fn = "out/mh_%s_raw.csv" % curve
    np.savetxt(fn, p[:, :2], delimiter=",", header="H_Oe,Bi_kG", comments="",
               fmt="%.6g")
    d = p[:, 1] - p[:, 2]
    print("%s: %d columns, H %.2f..%.0f Oe, Bi %.2f..%.2f kG; "
          "ours minus FEMM's: median %+.3f kG, max |%.3f| kG"
          % (curve, len(p), p[0, 0], p[-1, 0], p[:, 1].min(), p[:, 1].max(),
             np.median(d), np.abs(d).max()))
    im = Image.open(IMG).convert("RGB")
    dr = ImageDraw.Draw(im)
    for hh, k, kg in p[::3]:
        dr.ellipse([X(hh) - 3, Y(k) - 3, X(hh) + 3, Y(k) + 3], fill=(230, 0, 0))
        dr.point((X(hh), Y(kg)), fill=(0, 160, 255))
    im.save("out/mh_%s_overlay.png" % curve)
    return p


if __name__ == "__main__":
    trace("vpermendur", "Vanadium Permendur", 0.3, 3000)
