# SPDX-License-Identifier: MIT
"""dig_trace.py -- extract B(H) from an AK Steel D-C magnetization figure.

Unlike the USS sheets (scans, needing an affine axis fit), these figures are
vector art rendered from the PDF: the frame rules land on exact decades and
exact 0.2 T lines, so the calibration is read off the frame rather than
optimized. dig_frame.py prints and checks those rules first.

Curve isolation is by colour, then by connected component: a curve is one long
stroke spanning most of the plot, while stray text pixels of a similar colour
are small blobs. Keeping only components whose x-extent is a large fraction of
the frame drops the callout boxes without having to mask them by hand.

    python dig_trace.py <upright.png> <colour> <out.csv>

colour is one of grey / red / blue. Dashed curves of the same colour as a
solid one cannot be separated this way and are not attempted here.
"""
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

SEL = {
    # name: (predicate on r,g,b arrays)
    "grey": lambda r, g, b, lum, sat: (sat <= 34) & (lum > 68) & (lum < 190),
    "red":  lambda r, g, b, lum, sat: (r > 130) & (g < 110) & (b < 130) & (r - g > 70),
    "blue": lambda r, g, b, lum, sat: (b > 55) & (b - r > 30) & (r < 110) & (lum < 170),
}


def main(src, colour, out_csv, mode="solid", decades=4.0, hmin=1.0, btop=2.0):
    # int16, not float: these pages are 4709x3339, and as float64 the RGB
    # array alone is 377 MB, which swapped and turned a 3-second extraction
    # into three minutes of almost no CPU. int16 is 94 MB and every test here
    # is an integer comparison anyway.
    a = np.asarray(Image.open(src).convert("RGB"), dtype=np.int16)
    g = a.sum(axis=2) // 3

    # --- frame (same detection as dig_frame.py) ---
    dark = g < 120
    cc, rc = dark.sum(axis=0), dark.sum(axis=1)

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

    gc = groups(np.nonzero(cc > 0.45 * cc.max())[0])
    gr = groups(np.nonzero(rc > 0.45 * rc.max())[0])
    x0, x1, y0, y1 = gc[0], gc[-1], gr[0], gr[-1]
    pxdec = (x1 - x0) / decades
    print("frame x %d..%d  y %d..%d   %.2f px/decade  %.2f px/T"
          % (x0, x1, y0, y1, pxdec, (y1 - y0) / btop))

    # --- colour mask inside the frame ---
    r, gg, b = a[..., 0], a[..., 1], a[..., 2]
    lum = g
    sat = a.max(axis=2) - a.min(axis=2)
    m = SEL[colour](r, gg, b, lum, sat)
    box = np.zeros_like(m)
    box[y0 + 3:y1 - 2, x0 + 3:x1 - 2] = True
    m &= box

    # --- mask the callout boxes -------------------------------------------
    # The legend draws SHORT SAMPLES of every curve, in the right colours and
    # the right dash pattern, and the info panel sits on the plot too. In
    # solid mode the span filter throws those away; in dashed mode it keeps
    # them, and they land at a B the real curve never has there -- the anneal
    # ordering check in dig_summary.py caught exactly that (950 C reading
    # H = 1 A/m at B = 1.0 T).
    #
    # Both panels are opaque WHITE, which nothing else inside the plot is:
    # the background is a green grid -- so the near-white regions locate them.
    # Masked by BOUNDING BOX rather than by dilating the white region: the
    # panels are axis-aligned rectangles, so the box is the honest shape, and
    # a margin covers the border rule and any text touching it. Dilation gave
    # the same answer but took ~40 s per figure on a 4709x3339 mask.
    # Sizes via bincount, and bounding boxes only for the few big labels. The
    # obvious ndimage.sum(...)/find_objects(...) over every label is what made
    # this take three minutes: the white mask inside a green grid breaks into
    # ~27 000 components, of which exactly two are panels.
    white = (a.min(axis=2) > 243) & box
    wl, wn = ndimage.label(white, structure=np.ones((3, 3)))
    panel = np.zeros_like(m)
    npan = 0
    if wn:
        pad = 14
        sizes = np.bincount(wl.ravel())
        for i in np.nonzero(sizes > 0.004 * box.sum())[0]:
            if i == 0:
                continue
            ys_, xs_ = np.nonzero(wl == i)
            panel[max(ys_.min() - pad, 0):ys_.max() + pad,
                  max(xs_.min() - pad, 0):xs_.max() + pad] = True
            npan += 1
    m &= ~panel
    print("colour pixels in frame: %d  (%d callout panel(s) masked)"
          % (m.sum(), npan))

    # --- separate solid from dashed by connected-component span ---
    #
    # On the Pure Iron sheets each colour carries BOTH a solid and a dashed
    # curve, so colour alone is not enough. A solid stroke is one component
    # spanning most of the plot; a dashed one is dozens of short components at
    # the same colour. Selecting on span therefore picks one or the other,
    # and it is also what drops stray text of a similar colour in either mode.
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    keep = np.zeros_like(m)
    span_min = 0.25 * (x1 - x0)
    for sl, i in zip(ndimage.find_objects(lab), range(1, n + 1)):
        if sl is None:
            continue
        w = sl[1].stop - sl[1].start
        h_ = sl[0].stop - sl[0].start
        if mode == "solid":
            take = w >= span_min
        else:                       # dashed: the fragments, not the long one
            take = (w < span_min) and (w * h_ >= 25)
        if take:
            keep |= (lab == i)
    print("components kept (%s): %d of %d"
          % (mode, len(np.unique(lab[keep])) - (0 in np.unique(lab[keep])), n))

    # --- one B per column ---
    rows = []
    for x in range(x0 + 3, x1 - 2):
        ys = np.nonzero(keep[:, x])[0]
        if ys.size == 0:
            continue
        # A near-vertical rise puts many pixels in one column; the median is
        # the stroke centre either way, and the spread is reported so a
        # column straddling two curves would be visible.
        yc = float(np.median(ys))
        h = 10.0 ** ((x - x0) / pxdec) * hmin
        bb = btop * (y1 - yc) / (y1 - y0)
        rows.append((h, bb, ys.size))

    rows = np.array(rows)
    print("columns traced: %d   H %.2f..%.0f A/m   B %.4f..%.4f T"
          % (len(rows), rows[0, 0], rows[-1, 0], rows[:, 1].min(), rows[:, 1].max()))
    np.savetxt(out_csv, rows[:, :2], delimiter=",", header="H_A_per_m,B_T",
               comments="", fmt="%.6g")
    print("wrote", out_csv)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3],
         sys.argv[4] if len(sys.argv) > 4 else "solid")
