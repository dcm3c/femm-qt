# SPDX-License-Identifier: MIT
"""dig_metglas.py -- the DC hysteresis loops of Metglas 2605SA1 and 2605HB1M,
read from the VECTOR artwork of Metglas/2605SA1-Magnetic-Alloy-Updated.pdf
("Amorphous Alloys for Transformer Cores", 29 April 2011): p. 5 (2605SA1) and
p. 10 (2605HB1M), "DC Hysteresis Curves, Sample: Straight Strip (25.4 mm wide,
200 mm long)", single-strip test after a 2-hour field anneal (SA1 370 C, HB1M
340 C, 2400 A/m along the ribbon; p. 1).  Each page charts the upper half
(B >= 0) of two loops, Hm = 8 A/m (red) and Hm = 80 A/m (blue).

The charts are drawn sideways: B runs along page x, H along page y.  The axes
are calibrated from the chart's own gridlines (axis-parallel strokes), each
matched to the nearest tick label, and fitted by least squares, so a label's
position is never used directly.  Every coloured stroke is kept, sampled
every 0.5 pt along its length (curves by their chord, which at these stroke
lengths is within the line width).

Usage:  python dig_metglas.py   -> out/metglas_loops.csv (alloy, loop, H_Am, B_T)
"""
import csv
import numpy as np
import pymupdf

PDF = "Metglas/2605SA1-Magnetic-Alloy-Updated.pdf"
PAGES = {"2605SA1": 5, "2605HB1M": 10}
COLOURS = {(0.0, 0.0, 1.0): "Hm80", (1.0, 0.0, 0.0): "Hm8"}


def axis_lines(page):
    """Positions of axis-parallel grid strokes: x of strokes running along
    page y (B gridlines), y of strokes running along page x (H gridlines)."""
    xs, ys = [], []
    for dr in page.get_drawings():
        if dr.get("color") is None or dr["color"] in COLOURS:
            continue
        for it in dr["items"]:
            if it[0] != "l":
                continue
            a, b = it[1], it[2]
            if abs(a.x - b.x) < 0.05 and abs(a.y - b.y) > 20:
                xs.append(round(a.x, 2))
            elif abs(a.y - b.y) < 0.05 and abs(a.x - b.x) > 20:
                ys.append(round(a.y, 2))
    return sorted(set(xs)), sorted(set(ys))


def labels(page):
    """Numeric tick labels: B labels sit in a row (same y band), H labels in a
    column; return (value, centre) lists."""
    words = [(w[4], (w[0] + w[2]) / 2, (w[1] + w[3]) / 2) for w in page.get_text("words")]
    num = []
    for t, x, y in words:
        try:
            num.append((float(t), x, y))
        except ValueError:
            pass
    # B labels: the row with most members whose values are 0.0 .. 1.6
    yb = max({round(y) for v, x, y in num},
             key=lambda yy: sum(1 for v, x, y in num if abs(y - yy) < 3 and 0 <= v <= 2))
    bl = sorted({(v, round(x, 1)) for v, x, y in num if abs(y - yb) < 3})
    xh = max({round(x) for v, x, y in num},
             key=lambda xx: sum(1 for v, x, y in num if abs(x - xx) < 3 and -10 <= v <= 80))
    hl = sorted({(v, round(y, 1)) for v, x, y in num if abs(x - xh) < 3})
    return bl, hl


def calibrate(lab, lines):
    """Snap each label to the nearest gridline, then fit value = a + b*pos."""
    pos = np.array(lines)
    v, p = [], []
    for val, c in lab:
        k = np.argmin(np.abs(pos - c))
        if abs(pos[k] - c) < 6:
            v.append(val)
            p.append(pos[k])
    fit = np.polyfit(p, v, 1)
    resid = np.array(v) - np.polyval(fit, p)
    return fit, len(v), float(np.abs(resid).max())


def main():
    doc = pymupdf.open(PDF)
    rows = []
    for alloy, pno in PAGES.items():
        page = doc[pno - 1]
        xs, ys = axis_lines(page)
        bl, hl = labels(page)
        fb, nb, rb = calibrate(bl, xs)
        fh, nh, rh = calibrate(hl, ys)
        print("%s p.%d: B axis from %d gridlines (max resid %.4f T), "
              "H axis from %d (max resid %.3f A/m)" % (alloy, pno, nb, rb, nh, rh))
        for dr in page.get_drawings():
            name = COLOURS.get(dr.get("color"))
            if not name:
                continue
            for it in dr["items"]:
                if it[0] not in ("l", "c"):
                    continue
                a, b = it[1], it[-1]
                # densify each stroke to 0.5 pt steps (0.002 T along B), so steep runs drawn as
                # one long segment still yield points at every B
                n = max(1, int(np.hypot(b.x - a.x, b.y - a.y) / 0.5))
                for t in np.linspace(0.0, 1.0, n + 1):
                    x, y = a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)
                    rows.append((alloy, name, float(np.polyval(fh, y)), float(np.polyval(fb, x))))
    with open("out/metglas_loops.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["alloy", "loop", "H_Am", "B_T"])
        for r in rows:
            w.writerow([r[0], r[1], "%.4f" % r[2], "%.5f" % r[3]])
    for alloy in PAGES:
        for loop in ("Hm8", "Hm80"):
            v = np.array([(r[2], r[3]) for r in rows if r[0] == alloy and r[1] == loop])
            print("  %s %s: %d vertices, H %.2f .. %.2f A/m, B %.4f .. %.4f T"
                  % (alloy, loop, len(v), v[:, 0].min(), v[:, 0].max(), v[:, 1].min(), v[:, 1].max()))
    print("wrote out/metglas_loops.csv (%d rows)" % len(rows))


if __name__ == "__main__":
    main()
