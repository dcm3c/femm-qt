# SPDX-License-Identifier: MIT
"""dig_frame.py -- locate the plot frame and sample the real curve colours.

The frame is the only pair of full-height / full-width black rules, so it is
found by counting dark pixels per row and per column. Everything after this
(calibration, tracing) keys off those four numbers, so they are printed and
checked against the decade gridlines before anything is extracted.
"""
import sys
import numpy as np
from PIL import Image

src = sys.argv[1]
im = Image.open(src).convert("RGB")
a = np.asarray(im, float)
H, W, _ = a.shape
g = a.mean(axis=2)
dark = g < 120                      # black rules only, not the green grid

colcount = dark.sum(axis=0)
rowcount = dark.sum(axis=1)

# The frame rules run essentially the full box; take the strongest few.
cth = 0.45 * colcount.max()
rth = 0.45 * rowcount.max()
cs = np.nonzero(colcount > cth)[0]
rs = np.nonzero(rowcount > rth)[0]


def groups(idx, gap=8):
    out, cur = [], [idx[0]]
    for v in idx[1:]:
        if v - cur[-1] <= gap:
            cur.append(v)
        else:
            out.append(cur)
            cur = [v]
    out.append(cur)
    return [int(np.mean(c)) for c in out]


gc, gr = groups(cs), groups(rs)
print("candidate vertical rules  x:", gc[:12])
print("candidate horizontal rules y:", gr[:12])

x0, x1 = gc[0], gc[-1]
y0, y1 = gr[0], gr[-1]
print("FRAME  x0=%d x1=%d  y0=%d y1=%d   (w=%d h=%d)"
      % (x0, x1, y0, y1, x1 - x0, y1 - y0))

# Colour census strictly inside the frame, well away from the two white
# callout boxes, so the sampled colours are curve ink and nothing else.
sub = a[y0 + 5:y1 - 5, x0 + 5:x1 - 5]
mx, mn = sub.max(axis=2), sub.min(axis=2)
lum = sub.mean(axis=2)
sat = mx - mn

print("\n-- saturated ink (curves in colour) --")
sel = (lum < 235) & (sat > 40)
q = (sub[sel] // 24 * 24).astype(int)
uq, cnt = np.unique(q.reshape(-1, 3), axis=0, return_counts=True)
for i in np.argsort(-cnt)[:8]:
    print("   %3d %3d %3d  %7d" % (*uq[i], cnt[i]))

print("\n-- neutral ink (grey curve + black rules/text) --")
sel = (sat <= 40) & (lum < 220)
q = (sub[sel] // 16 * 16).astype(int)
uq, cnt = np.unique(q.reshape(-1, 3), axis=0, return_counts=True)
for i in np.argsort(-cnt)[:10]:
    print("   %3d %3d %3d  %7d" % (*uq[i], cnt[i]))
