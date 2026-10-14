# SPDX-License-Identifier: MIT
"""dig_fig7.py -- digitize AK Steel Figure 7 (single annealed Pure Iron curve).

Figure 7 is upright and has no grid box, so the frame detector used for the
D-C sheets does not apply. It is calibrated off the TICK MARKS instead: the
short rules just outside each axis. That is self-checking -- the x ticks must
come out evenly spaced (one per decade) and the y ticks must come out evenly
spaced (one per 0.2 T), so a misdetected tick shows up immediately as an
uneven spacing rather than as a quietly wrong curve.
"""
import numpy as np
from PIL import Image
from scipy import ndimage

SRC = "fig/pi_f7_up.png"
a = np.asarray(Image.open(SRC).convert("RGB"), float)
g = a.mean(axis=2)
dark = g < 120

# The two axis rules are the longest runs.
cc, rc = dark.sum(axis=0), dark.sum(axis=1)
xax = int(np.argmax(cc))          # vertical rule  = y axis
yax = int(np.argmax(rc))          # horizontal rule = x axis
print("axes: y-axis at x=%d, x-axis at y=%d" % (xax, yax))


def runs(idx, gap=6):
    out, cur = [], [idx[0]]
    for v in idx[1:]:
        if v - cur[-1] <= gap:
            cur.append(v)
        else:
            out.append(cur)
            cur = [v]
    out.append(cur)
    return [float(np.mean(c)) for c in out]


# y ticks: dark pixels in the strip just LEFT of the y axis
strip = dark[:, xax - 22:xax - 4]
ys = np.nonzero(strip.sum(axis=1) > 6)[0]
yt = runs(ys)
# x ticks: dark pixels in the strip just BELOW the x axis
strip = dark[yax + 5:yax + 24, :]
xs = np.nonzero(strip.sum(axis=0) > 6)[0]
xt = runs(xs)


def longest_ladder(pos, tol=0.03, need=5):
    """Longest evenly-spaced run in a list of candidate tick positions.

    The strips also catch axis-label text, which is irregular; real ticks are
    a ladder. Requiring regularity is what makes this self-checking -- a
    missed or spurious tick breaks the ladder instead of silently rescaling
    the axis.
    """
    best = []
    pos = sorted(pos)
    for i in range(len(pos)):
        for j in range(i + 1, len(pos)):
            step = pos[j] - pos[i]
            if step <= 0:
                continue
            run = [pos[i], pos[j]]
            nxt = pos[j] + step
            for v in pos[j + 1:]:
                if abs(v - nxt) <= tol * step:
                    run.append(v)
                    nxt = v + step
            if len(run) > len(best):
                best = run
    return best if len(best) >= need else sorted(pos)


yt = longest_ladder(yt)
xt = longest_ladder(xt)
print("y ticks (%d):" % len(yt), [round(v) for v in yt])
print("x ticks (%d):" % len(xt), [round(v) for v in xt])

dy = np.diff(yt)
dx = np.diff(xt)
print("y tick spacing: mean %.1f  spread %.1f px" % (dy.mean(), dy.max() - dy.min()))
print("x tick spacing: mean %.1f  spread %.1f px" % (dx.mean(), dx.max() - dx.min()))

# 11 y ticks = 0.0..2.0 by 0.2 ; 5 x ticks = 1,10,100,1000,10000
assert len(yt) == 11, "expected 11 y ticks, got %d" % len(yt)
assert len(xt) == 5, "expected 5 x ticks, got %d" % len(xt)
y_top, y_bot = yt[0], yt[-1]        # 2.0 T and 0.0 T
x_1, x_1e4 = xt[0], xt[-1]
pxdec = (x_1e4 - x_1) / 4.0
print("calibration: %.2f px/decade, %.2f px/T"
      % (pxdec, (y_bot - y_top) / 2.0))

# the curve: dark blue, saturated, away from the axes and the caption text
r, gg, b = a[..., 0], a[..., 1], a[..., 2]
m = (b > 55) & (b - r > 28) & (r < 115) & (g < 170)
box = np.zeros_like(m)
box[int(y_top) - 20:int(y_bot) - 2, int(x_1) + 2:] = True
m &= box
lab, n = ndimage.label(m, structure=np.ones((3, 3)))
if n:
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    m = (lab == (1 + int(np.argmax(sizes))))
print("curve pixels: %d" % m.sum())

rows = []
for x in range(int(x_1), a.shape[1]):
    yy = np.nonzero(m[:, x])[0]
    if yy.size:
        h = 10.0 ** ((x - x_1) / pxdec)
        bb = 2.0 * (y_bot - float(np.median(yy))) / (y_bot - y_top)
        rows.append((h, bb))
rows = np.array(rows)
print("traced %d columns: H %.2f..%.0f A/m, B %.4f..%.4f T"
      % (len(rows), rows[0, 0], rows[-1, 0], rows[:, 1].min(), rows[:, 1].max()))
np.savetxt("out/pi_fig7_annealed_raw.csv", rows, delimiter=",",
           header="H_A_per_m,B_T", comments="", fmt="%.6g")
print("wrote out/pi_fig7_annealed_raw.csv")
