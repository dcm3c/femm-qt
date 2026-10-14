# SPDX-License-Identifier: MIT
"""dig_highfield.py -- digitize the HIGH-INDUCTION magnetization curves:

  ARMCO Telar 57 bulletin Figure 5  (fig/tel_f5-09.png, page 9 @300dpi)
  ARMCO Pure Iron bulletin Figure 8 (fig/pi_f8-10.png, page 10 @300dpi)

Both plot INTRINSIC induction J = B - mu0*H against H on a log axis
(x ticks 500 / 5000 / 50 000 / 500 000 A/m), J linear (0.1 T ticks:
Telar 1.5..2.1, Pure Iron 1.50..2.20). Same tick-ladder calibration and
self-checks as dig_fig7.py: ticks must come out evenly spaced.

Output CSVs store (H_A_per_m, B_T) with B = J + mu0*H, matching every
other *_raw.csv so femm_table.load_raw applies unchanged.
"""
import numpy as np
from PIL import Image
from scipy import ndimage

MU0 = 4e-7 * np.pi


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


def longest_ladder(pos, tol=0.03, need=4):
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


def digitize(src, jtop, jbot, ny, out_csv):
    """jtop/jbot: J at first/last y tick; ny: expected y-tick count."""
    a = np.asarray(Image.open(src).convert("RGB"), float)
    g = a.mean(axis=2)
    dark = g < 120

    cc, rc = dark.sum(axis=0), dark.sum(axis=1)
    xax = int(np.argmax(cc))
    yax = int(np.argmax(rc))
    print(f"{src}: y-axis at x={xax}, x-axis at y={yax}")

    strip = dark[:, xax - 22:xax - 4]
    yt = longest_ladder(runs(np.nonzero(strip.sum(axis=1) > 6)[0]),
                        need=5)
    strip = dark[yax + 5:yax + 24, :]
    xt = longest_ladder(runs(np.nonzero(strip.sum(axis=0) > 6)[0]),
                        need=3)
    print(f"  y ladder ({len(yt)}): {[round(v) for v in yt]}")
    print(f"  x ladder ({len(xt)}): {[round(v) for v in xt]}")

    # Both axes start ON their first tick, so the bottom J tick merges
    # with the x-axis rule and the H=500 tick merges with the y-axis
    # rule. Self-checks: the extrapolated bottom tick must land on the
    # x-axis rule, and the extrapolated H=500 tick must land on the
    # y-axis rule (each within 12 px; the rules are several px thick).
    dy = float(np.mean(np.diff(yt)))
    nfound = len(yt)
    ybot_extrap = yt[-1] + (ny - nfound) * dy
    assert abs(ybot_extrap - yax) < 12, \
        f"y ladder does not extrapolate to the x-axis rule " \
        f"({ybot_extrap:.0f} vs {yax})"
    dxm = float(np.mean(np.diff(xt)))
    x500_extrap = xt[0] if abs(xt[0] - xax) < 12 else xt[0] - dxm
    assert abs(x500_extrap - xax) < 12, \
        f"x ladder does not reach the y-axis rule " \
        f"({x500_extrap:.0f} vs {xax})"
    print(f"  self-checks: bottom-tick extrapolation {ybot_extrap:.0f} vs "
          f"axis {yax}; H=500 extrapolation {x500_extrap:.0f} vs {xax}")

    y0 = yt[0]                             # J = jtop
    y1 = ybot_extrap                       # J = jbot (on the axis rule)
    x0 = x500_extrap                       # H = 500
    pxdec = dxm                            # px per decade

    r, gg, b = a[..., 0], a[..., 1], a[..., 2]
    m = (b > 55) & (b - r > 28) & (r < 115) & (gg < 170)
    box = np.zeros_like(m)
    box[int(y0) - 20:int(y1) + 20, int(x0) - 40:] = True
    m &= box
    lab, n = ndimage.label(m, structure=np.ones((3, 3)))
    if n:
        sizes = ndimage.sum(m, lab, range(1, n + 1))
        m = lab == (1 + int(np.argmax(sizes)))
    print(f"  curve pixels: {int(m.sum())}")

    rows = []
    for x in range(int(x0) - 40, a.shape[1]):
        yy = np.nonzero(m[:, x])[0]
        if yy.size:
            h = 500.0 * 10.0 ** ((x - x0) / pxdec)
            j = jtop + (jbot - jtop) * (float(np.median(yy)) - y0) / (y1 - y0)
            rows.append((h, j + MU0 * h))
    rows = np.array(rows)
    print(f"  traced {len(rows)} cols: H {rows[0,0]:.0f}..{rows[-1,0]:.0f}"
          f" A/m, J {rows[0,1]-MU0*rows[0,0]:.3f}.."
          f"{rows[-1,1]-MU0*rows[-1,0]:.3f} T")
    np.savetxt(out_csv, rows, delimiter=",", header="H_A_per_m,B_T",
               comments="", fmt="%.6g")
    print(f"  wrote {out_csv}\n")


digitize("fig/tel_f5-09.png", 2.1, 1.5, 7, "out/telar_fig5_high_raw.csv")
digitize("fig/pi_f8-10.png", 2.20, 1.50, 8, "out/pi_fig8_high_raw.csv")
