# SPDX-License-Identifier: MIT
"""dig_nife.py -- DC magnetization curves read from the Carpenter nickel-iron
datasheets' vector artwork.

Carpenter's current High Permeability 49 and HyMu 80 datasheets draw their
curves as PDF paths, so nothing here is traced from pixels: each curve is the
path itself, told apart from its neighbours by its dash pattern, and the axes
are calibrated from the grid lines drawn with it.  The grid is not a uniform
log scale (the artwork was redrawn by hand), so each gridline is snapped to
the nearest 1-9 x 10^n (or 1.5 x 10^n) and positions are interpolated
piecewise between them.

Usage:  python dig_nife.py   -> out/nife_curves.csv
        columns: source, fig, curve, condition, thick_in, H_Am, B_T
"""
import csv
import numpy as np
import pymupdf

OE = 1000 / (4 * np.pi)          # A/m per oersted
GRID = (0.78, 0.79, 0.78)
BLUE = (0.0, 0.68, 0.94)
HP49 = "NiFe/Carpenter-High-Permeability-49.pdf"
HYMU = "NiFe/Carpenter-HyMu-80.pdf"


def dash(q):
    """The dash pattern's first two numbers, rounded: a curve's signature."""
    d = (q.get("dashes") or "[] 0").strip("[] 0").split()
    return tuple(round(float(v), 1) for v in d[:2])


# (file, page, frame corner, [(signature, curve, condition, thickness)])
# Axes on every one of these charts: H 0.001-10 Oe, B 30-30,000 G.
FIGS = [
    (HP49, 4, (83.0, 327.9), "0.060 in ring", [
        ((), "2050F", "dry H2 2050 F (1121 C) 4 h", 0.060),
        ((23.8, 3.2), "1750F", "dry H2 1750 F (954 C) 2 h", 0.060),
        ((9.5, 3.2), "1600F", "dry H2 1600 F (871 C) 2 h", 0.060),
        ((2.4, 1.2), "1450F", "dry H2 1450 F (788 C) 2 h", 0.060),
        ((1.2, 1.6), "mill", "typical mill process annealed", 0.060)]),
    (HP49, 7, (81.2, 240.8), "rotor grade 0.014 in ring", [
        ((), "DC", "dry H2 2150 F (1177 C) 4 h", 0.014)]),
    (HP49, 11, (78.0, 301.1), "rotor grade 0.007 in ring", [
        ((), "DC", "dry H2 2150 F (1177 C) 4 h", 0.007)]),
    (HP49, 13, (77.2, 396.1), "typical DC range 0.014-0.062 in", [
        ("upper", "range upper", "H2 2150 F (1177 C)", None),
        ("lower", "range lower", "H2 2150 F (1177 C)", None)]),
    (HYMU, 4, (68.5, 251.1), "0.006 in ring", [
        ((), "DC", "dry H2 2150 F (1177 C) 4 h, 600 F/h", 0.006)]),
    (HYMU, 4, (347.5, 250.5), "0.014 in ring", [
        ((), "DC", "dry H2 2150 F (1177 C) 4 h, 600 F/h", 0.014)]),
    (HYMU, 4, (68.2, 561.4), "0.004 in toroid", [
        ((), "DC", "dry H2 2150 F (1177 C) 4 h, 600 F/h", 0.004)]),
]

NICE = np.log10(np.array([m * 10.0 ** n for n in range(-4, 6)
                          for m in (1, 1.5, 2, 3, 4, 5, 6, 7, 8, 9)]))


def axis(frame_lo, frame_hi, lines, v_lo, v_hi):
    """Piecewise map from page coordinate to log10 value.  Gridlines are
    snapped to the nearest nice value predicted by a uniform log scale."""
    s = (np.log10(v_hi) - np.log10(v_lo)) / (frame_hi - frame_lo)
    c, v = [frame_lo, frame_hi], [np.log10(v_lo), np.log10(v_hi)]
    for x in sorted(set(round(x, 1) for x in lines)):
        if not frame_lo + 1 < x < frame_hi - 1:
            continue
        g = np.log10(v_lo) + (x - frame_lo) * s
        n = NICE[np.argmin(abs(NICE - g))]
        if abs(n - g) < 0.06:
            c.append(x)
            v.append(n)
    o = np.argsort(c)
    c, v = np.array(c)[o], np.array(v)[o]
    keep = np.concatenate([[True], np.diff(v) > 0])   # drop mis-snaps
    return c[keep], v[keep]


def path_points(q):
    pts = []
    for it in q["items"]:
        if it[0] == "l":
            pts += [(it[1].x, it[1].y), (it[2].x, it[2].y)]
        elif it[0] == "c":
            p = np.array([(P.x, P.y) for P in it[1:5]])
            t = np.linspace(0, 1, 25)[:, None]
            b = ((1 - t) ** 3 * p[0] + 3 * (1 - t) ** 2 * t * p[1]
                 + 3 * (1 - t) * t ** 2 * p[2] + t ** 3 * p[3])
            pts += [tuple(r) for r in b]
    return np.array(pts)


def read_fig(f, page, corner, curves):
    dr = pymupdf.open(f)[page - 1].get_drawings()
    fr = [q["items"][0][1] for q in dr if q["type"] == "s"
          and tuple(round(x, 2) for x in q["color"] or ()) == GRID
          and q["items"][0][0] == "re"]
    fr = min(fr, key=lambda r: abs(r.x0 - corner[0]) + abs(r.y0 - corner[1]))
    vx, vy = [], []
    for q in dr:
        if tuple(round(x, 2) for x in q.get("color") or ()) != GRID:
            continue
        for it in q["items"]:
            if it[0] != "l":
                continue
            a, b = it[1], it[2]
            if abs(a.x - b.x) < 0.05 and fr.x0 <= a.x <= fr.x1:
                vx.append(a.x)
            if abs(a.y - b.y) < 0.05 and fr.y0 <= a.y <= fr.y1:
                vy.append(a.y)
    cx, lx = axis(fr.x0, fr.x1, vx, 1e-3, 10.0)
    cy, ly = axis(-fr.y1, -fr.y0, [-y for y in vy], 30.0, 3.0e4)  # y down
    blue = [q for q in dr if tuple(round(x, 2) for x in q.get("color") or ())
            == BLUE and fr.contains((q["rect"].tl + q["rect"].br) / 2)]
    out = []
    for sig, name, cond, t in curves:
        if sig in ("upper", "lower"):
            solid = sorted([q for q in blue if dash(q) == ()],
                           key=lambda q: q["rect"].x0)
            sel = [solid[0] if sig == "upper" else solid[-1]]
        else:
            sel = [q for q in blue if dash(q) == sig]
        p = np.concatenate([path_points(q) for q in sel])
        h = 10 ** np.interp(p[:, 0], cx, lx)
        b = 10 ** np.interp(-p[:, 1], cy, ly)
        o = np.argsort(h)
        out.append((name, cond, t, h[o], b[o]))
    return out, (len(lx), len(ly))


def resample(h, b, n=60):
    """Evenly spaced along the curve in (log H, log B)."""
    x, y = np.log10(h), np.log10(b)
    y = np.maximum.accumulate(y)
    s = np.concatenate([[0], np.cumsum(np.hypot(np.diff(x), np.diff(y)))])
    k = np.concatenate([[True], np.diff(s) > 1e-9])
    x, y, s = x[k], y[k], s[k]
    t = np.linspace(0, s[-1], n)
    return 10 ** np.interp(t, s, x), 10 ** np.interp(t, s, y)


def read_hymu800():
    """HyMu "800" DC curve, 0.004 in tape toroid (datasheet p. 4).  This
    older sheet is a scanned raster, 550 x 473 px.  Strokes are eroded 3x3,
    which removes the 1 px grid and leaves each curve's centre line; the two
    labels that sit left of or above the DC curve are masked; the DC curve
    is then the upper-left envelope of what remains.  Axes, from the labelled
    decades: H 10^-3 Oe at x = 53 px, 119.75 px per decade; B 10^4 G at
    y = 122 px, 122 px per decade.  From 1 Oe the curve is drawn flat at
    y = 135-136 px, 7,750 G; the trace stops where it reaches that line,
    because a ruled line one pixel (2% in B) wide says only that the curve
    is within 2% of 7,750 G out to 10 Oe, not that its slope is zero."""
    import io
    from PIL import Image, ImageFilter
    from scipy import ndimage
    doc = pymupdf.open("NiFe/HyMu 800.pdf")
    xref = doc[3].get_images()[0][0]
    im = Image.open(io.BytesIO(doc.extract_image(xref)["image"])).convert("L")
    a = np.array(im) < 128
    m = np.array(Image.fromarray((a * 255).astype("uint8"))
                 .filter(ImageFilter.MinFilter(3))) > 0
    for x0, y0, x1, y1 in [(130, 120, 185, 150), (90, 175, 150, 200)]:
        m[y0:y1, x0:x1] = False             # "D.C." and "60 Hz" labels
    m[:126, :] = False
    m[:, :55] = False
    m[:, 531:] = False
    m[433:, :] = False
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    sz = ndimage.sum(m, lab, range(1, n + 1))
    m = np.isin(lab, 1 + np.where(sz >= 10)[0])
    pts = []
    for y in range(126, 433):
        xs = np.where(m[y])[0]
        if len(xs):
            pts.append((xs[0], y))
    for x in range(55, 531):
        ys = np.where(m[:, x])[0]
        if len(ys):
            pts.append((x, ys[0]))
    p = np.array(pts, float)
    lh = (p[:, 0] - 53) / 119.75 - 3
    lb = 4 - (p[:, 1] - 122) / 122
    o = np.lexsort((-lb, lh))
    env, best = [], -9.0
    for x, y in zip(lh[o], lb[o]):
        if y > best + 1e-9:
            env.append((x, y))
            best = y
    env = np.array(env)
    top = np.log10(7750.0)
    env = env[env[:, 1] < top]
    return 10 ** env[:, 0], 10 ** env[:, 1]


def main():
    rows = []
    h, b = resample(*read_hymu800())
    print("Carpenter HyMu 800  p4  0.004 in tape toroid  DC  %d pts  "
          "H %.4f-%.2f Oe  B %6.0f-%6.0f G" % (len(h), h[0], h[-1], b[0], b[-1]))
    for hh, bb in zip(h, b):
        rows.append(["Carpenter HyMu 800", "p4 0.004 in tape toroid", "DC",
                     "dry H2 (sheet gives no temperature for this figure)",
                     0.004, "%.5g" % (hh * OE), "%.5g" % (bb / 1e4)])
    for f, page, corner, fig, curves in FIGS:
        got, ng = read_fig(f, page, corner, curves)
        src = "Carpenter HP49" if f == HP49 else "Carpenter HyMu 80"
        for name, cond, t, h, b in got:
            h, b = resample(h, b)
            print("%-18s p%-2d %-32s %-12s %3d pts  H %.4f-%.2f Oe  "
                  "B %6.0f-%6.0f G  (grid %d x, %d y)"
                  % (src, page, fig, name, len(h), h[0], h[-1], b[0], b[-1],
                     *ng))
            for hh, bb in zip(h, b):
                rows.append([src, "p%d %s" % (page, fig), name, cond,
                             "" if t is None else t,
                             "%.5g" % (hh * OE), "%.5g" % (bb / 1e4)])
    with open("out/nife_curves.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["source", "fig", "curve", "condition", "thick_in",
                    "H_Am", "B_T"])
        w.writerows(rows)
    print("wrote out/nife_curves.csv, %d rows" % len(rows))


if __name__ == "__main__":
    main()
