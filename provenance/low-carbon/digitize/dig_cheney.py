# SPDX-License-Identifier: MIT
"""dig_cheney.py -- digitize Cheney's Fig. 10, the four low-carbon curves.

Source: W. L. Cheney, Bureau of Standards Scientific Paper No. 463,
vol. 18 p. 609 (1922), Fig. 10 on p. 629: "Normal induction curves for
specimens annealed in vacuo."  Iron-carbon alloys, manganese-free,
annealed in vacuo (760-930 C, most at 820 C).  Linear axes, 0-20 Oe and
0-20,000 G, gridlines every 5 Oe and 5000 G.

Only the four curves inside this project's carbon range are traced:
0.018, 0.14, 0.23 and 0.32 % C.  They are the four uppermost curves at the
right-hand edge, in that order, and they do not cross.

Method.  The page is rendered from the PDF at 400 dpi (pdftoppm).  The
axes are calibrated from the gridlines found in the image, not assumed.
Each curve is seeded at H = 12 Oe, where none of the four sits on a
gridline (at the right-hand edge the 0.14 % curve runs along the 15,000 G
line), followed rightward to 19.5 Oe, and leftward column by column
while it is shallow, then downward row by row once it is steep, always
taking the darkest pixel run within a few pixels of where the curve's own
slope predicts it.  Gridline columns and rows are stepped over.  A curve
ends where its ink ends: Cheney's curves begin at 1-3.5 Oe, not at zero.

Outputs:
  out/cheney_fig10.csv     C_pct, H_Oe, B_G   (tracked: digitized data)
  out/cheney_fig10_check.png   traced points over the scan (not tracked)

Usage:  python dig_cheney.py
"""
import os
import subprocess

import numpy as np
from PIL import Image, ImageDraw

PDF, PAGE, DPI = "cheney-sp463.pdf", 21, 400
PNG = "out/cheney/p21-21.png"
CURVES = (0.018, 0.14, 0.23, 0.32)

if not os.path.exists(PNG):
    os.makedirs("out/cheney", exist_ok=True)
    subprocess.run(["pdftoppm", "-f", str(PAGE), "-l", str(PAGE), "-r", str(DPI),
                    "-gray", "-png", PDF, "out/cheney/p21"], check=True)
g = np.asarray(Image.open(PNG).convert("L")).astype(float)
dark = 255.0 - g

# ---------------------------------------------------------------- frame
# the plot sits in the middle of the page; find long straight dark lines
Y0, Y1, X0, X1 = 900, 2600, 350, 2000
sub = dark[Y0:Y1, X0:X1] > 90
colrun = sub.sum(0)
rowrun = sub.sum(1)


def lines(counts, frac, offset):
    idx = np.where(counts > frac * counts.max())[0]
    out, cur = [], []
    for i in idx:
        if cur and i - cur[-1] > 3:
            out.append(np.mean(cur) + offset)
            cur = []
        cur.append(i)
    if cur:
        out.append(np.mean(cur) + offset)
    return out


vx = lines(colrun, 0.5, X0)
hy = lines(rowrun, 0.5, Y0)
assert len(vx) == 5 and len(hy) == 5, (vx, hy)
# vertical: H = 0, 5, 10, 15, 20 left to right; horizontal: B = 20000..0 top down
ax, bx = np.polyfit(vx, [0, 5, 10, 15, 20], 1)
ay, by = np.polyfit(hy, [20000, 15000, 10000, 5000, 0], 1)
H = lambda x: ax * x + bx
B = lambda y: ay * y + by
X = lambda h: (h - bx) / ax
Y = lambda b: (b - by) / ay
print("gridlines x:", [round(v, 1) for v in vx], " y:", [round(v, 1) for v in hy])
print("scale: %.2f px/Oe, %.4f px/G" % (1 / ax, -1 / ay))

bg = np.median(dark[int(Y(12000)):int(Y(8000)), int(X(16)):int(X(19))])
THR = bg + 60
GRIDX = [int(round(v)) for v in vx]
GRIDY = [int(round(v)) for v in hy]
# Blank the inner gridlines to paper colour.  A curve that runs along a
# gridline (0.14 % along 15,000 G at the right; several cross 10,000 G at a
# shallow angle) otherwise pulls the trace onto the line and off to a
# neighbouring curve.  The blanked bands are 11 px, about 0.15 Oe or 160 G,
# and each trace bridges them on its own slope.
for q in GRIDX[1:4]:
    dark[:, q - 5:q + 6] = bg
for q in GRIDY[1:4]:
    dark[q - 5:q + 6, :] = bg


def on_grid(v, grid, pad=5):
    return any(abs(v - q) <= pad for q in grid)


def darkest(profile, centre, half):
    """Centroid of the darkest run within +/- half of centre, or None."""
    lo, hi = int(centre - half), int(centre + half) + 1
    seg = profile[lo:hi]
    if seg.size == 0 or seg.max() < THR:
        return None
    i = int(np.argmax(seg))
    j0 = i
    while j0 > 0 and seg[j0 - 1] >= THR:
        j0 -= 1
    j1 = i
    while j1 < len(seg) - 1 and seg[j1 + 1] >= THR:
        j1 += 1
    w = seg[j0:j1 + 1] - bg
    return lo + j0 + float((np.arange(j1 - j0 + 1) * w).sum() / w.sum())


# --------------------------------------------------------------- seeds
xs = int(X(12.0))
col = dark[:, xs]
seeds = []
y = int(Y(17500))
while y < Y(10500):
    if col[y] >= THR and not on_grid(y, GRIDY):
        c = darkest(col, y + 3, 6)
        seeds.append(c)
        y = int(c) + 8
    else:
        y += 1
assert len(seeds) >= 4, seeds
seeds = seeds[:4]
print("seeds at H=12 Oe, B =", [round(B(s)) for s in seeds])

# --------------------------------------------------------------- trace
traces = {}
for cpct, ys in zip(CURVES, seeds):
    right = []
    # rightward to the edge of the frame (the curves are shallow there)
    x, yv, slope = xs, ys, 0.0
    while x < X(19.6):
        x += 1
        if on_grid(x, GRIDX):
            yv += slope
            continue
        c = darkest(dark[:, x], yv + slope, 4)
        if c is None:
            yv += slope
            continue
        slope = 0.7 * slope + 0.3 * (c - yv)
        yv = c
        right.append((x, yv))
    # the trace is kept in drawing order: right-hand end, leftward, downward
    pts = right[::-1] + [(xs, ys)]
    x, yv, slope, lost = xs, ys, 0.0, 0
    # column pass, leftward, while shallow
    while x > X(0.3):
        x -= 1
        if on_grid(x, GRIDX):
            yv += slope
            continue
        # widen the window the longer the trace has coasted, so a slope
        # that drifted while bridging a blanked band can be recovered
        c = darkest(dark[:, x], yv + slope, 5 + min(lost // 2, 25))
        if c is None:
            lost += 1
            yv += slope
            if lost > 60:     # a shallow crossing of a blanked band
                break
            continue
        lost = 0
        slope = 0.7 * slope + 0.3 * (c - yv)
        yv = c
        pts.append((x, yv))
        if abs(slope) > 2.5:
            break
    # row pass, downward, once steep
    xv = pts[-1][0]
    yv = pts[-1][1]
    dxdy = 1.0 / slope if abs(slope) > 1e-6 else 0.0
    lost = 0
    while yv < Y(0) - 3:
        yv += 1
        if on_grid(yv, GRIDY):
            xv += dxdy
            continue
        c = darkest(dark[int(round(yv)), :], xv + dxdy, 4 + min(lost // 2, 10))
        if c is None:
            lost += 1
            xv += dxdy
            if lost > 20:     # past the band, or the curve's own start
                break
            continue
        lost = 0
        dxdy = 0.7 * dxdy + 0.3 * (c - xv)
        xv = c
        pts.append((xv, yv))
    traces[cpct] = pts

# The 0.018 % curve runs inside the blanked 15,000 G band from about 6.5 to
# 8 Oe, so the trace from the right cannot cross it.  Its steep lower part
# gets its own seed on the 12,000 G row, where it is the leftmost curve, and
# is followed down to its start and up until it turns shallow.  The short
# stretch inside the band is left as a gap; the curve is nearly flat there.
yq = int(round(Y(12000)))
row = dark[yq, :]
xq = next(x for x in range(int(X(0.3)), int(X(5))) if row[x] >= THR)
x0 = darkest(row, xq + 3, 5)
seg = {}
for direction in (+1, -1):          # +1: down the page (lower B); -1: up
    seg[direction] = []
    xv, yv, dxdy, lost = x0, float(yq), 0.0, 0
    while Y(20000) < yv < Y(0) - 3:
        yv += direction
        c = darkest(dark[int(round(yv)), :], xv + dxdy, 4 + min(lost // 2, 10))
        if c is None:
            lost += 1
            xv += dxdy
            if lost > 20:
                break
            continue
        lost = 0
        dxdy = 0.7 * dxdy + 0.3 * (c - xv)
        xv = c
        seg[direction].append((xv, yv))
        if direction == -1 and abs(dxdy) > 2.5:
            break
# ... and from where it turns shallow, rightward by columns until it runs
# into the blanked band, so only the stretch inside the band is missing.
xv, yv = seg[-1][-1]
slope = (seg[-1][-1][1] - seg[-1][-6][1]) / max(seg[-1][-1][0] - seg[-1][-6][0], 1e-6)
x, lost, ext = int(round(xv)), 0, []
main_left = min(q[0] for q in traces[0.018])
while x < main_left - 2:
    x += 1
    c = darkest(dark[:, x], yv + slope, 4 + min(lost // 2, 10))
    if c is None:
        lost += 1
        yv += slope
        if lost > 15:
            break
        continue
    lost = 0
    slope = 0.7 * slope + 0.3 * (c - yv)
    yv = c
    ext.append((x, yv))
traces[0.018] += ext[::-1] + seg[-1][::-1] + [(x0, float(yq))] + seg[+1]

# A normal induction curve is monotone, so following a trace in drawing
# order (right end, leftward, downward) neither H nor B may rise.  Where
# one does, the trace has jumped to a neighbouring curve (0.23 % onto
# 0.32 % below about 1500 G, where their starts nearly meet); cut it there.
# Tolerances are about two pixels each way.
for cpct in traces:
    pts = traces[cpct]
    keep, hmin, bmin = [], np.inf, np.inf
    for xq, yq in pts:
        if H(xq) > hmin + 0.03 or B(yq) > bmin + 30:
            break
        hmin, bmin = min(hmin, H(xq)), min(bmin, B(yq))
        keep.append((xq, yq))
    if len(keep) < len(pts):
        print("C=%.3f%%: cut %d points below B = %.0f G (trace jumped)"
              % (cpct, len(pts) - len(keep), B(keep[-1][1])))
    traces[cpct] = keep

# --------------------------------------------------------------- output
os.makedirs("out", exist_ok=True)
with open("out/cheney_fig10.csv", "w", newline="\n") as f:
    f.write("# Cheney, BS Sci. Paper 463 (1922), Fig. 10 p. 629: normal induction,\n"
            "# iron-carbon alloys annealed in vacuo (most at 820 C), Mn-free.\n"
            "# Traced by dig_cheney.py from the PDF page rendered at 400 dpi;\n"
            "# axes calibrated from the gridlines.  Every 4th traced pixel kept.\n"
            "C_pct,H_Oe,B_G\n")
    for cpct, pts in traces.items():
        p = sorted(pts, key=lambda q: H(q[0]))[::4]
        for xq, yq in p:
            f.write("%.3f,%.3f,%.0f\n" % (cpct, H(xq), B(yq)))
        hs = [H(q[0]) for q in pts]
        bs = [B(q[1]) for q in pts]
        print("C=%.3f%%: %4d points, H %.2f-%.2f Oe, B %5.0f-%5.0f G"
              % (cpct, len(pts), min(hs), max(hs), min(bs), max(bs)))

im = Image.open(PNG).convert("RGB")
dr = ImageDraw.Draw(im)
colours = {0.018: (230, 0, 0), 0.14: (0, 120, 255), 0.23: (0, 170, 0), 0.32: (230, 140, 0)}
for cpct, pts in traces.items():
    for xq, yq in pts[::3]:
        dr.ellipse([xq - 2, yq - 2, xq + 2, yq + 2], fill=colours[cpct])
im.crop((int(X(0)) - 40, int(Y(20000)) - 40, int(X(20)) + 260, int(Y(0)) + 60)).save(
    "out/cheney_fig10_check.png")
print("wrote out/cheney_fig10.csv, out/cheney_fig10_check.png")
