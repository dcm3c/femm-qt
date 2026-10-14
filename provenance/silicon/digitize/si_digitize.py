# SPDX-License-Identifier: MIT
"""si_digitize.py -- extract B(H), mu(H) and the marked Bs from the US Steel
electrical-sheet catalog pages in SiliconSteel/.

Each page is a 600 dpi scan of a log-H (0.1..1000 Oe) vs linear-B (kG)
chart on an orange grid, carrying two black curves, "Magnetization" and
"Permeability" (normal permeability B/H, right axis), plus a leader line
labelled "Saturation Induction, Bs" that ends on the magnetization curve.

Calibration is read off the grid, not fitted: the five darkest vertical
rules are the decades, and the major horizontal rules are 2 kG apart
(and 1000 mu apart on the right axis, which is what makes the two curves
share one y calibration).  Each set is fitted by a line so the scan's
small rotation and scale are absorbed; the residual of that fit is the
calibration error and is printed.

The two curves cross once.  They are separated by tracking, column by
column, from the left edge, where the magnetization curve is the lower
of the two, each track following the ink cluster nearest its own linear
extrapolation.  Text inside the plot is removed as small connected
components; the Bs leader is found as the long, one-pixel-thin,
horizontal component and its right-hand end is reported.

Self-check: the permeability curve is B/H, so mu(H)*H/1000 must equal
B(H) in kG at every column.  The RMS of that comparison is printed; it
is a check on the axis calibration that needs no second source.

    python si_digitize.py [img001 ...]        (default: all seven)

Writes out/si_<grade>_raw.csv  (H_Oe, B_kG, mu) and
       out/si_overlay_<grade>.png  (extracted points redrawn on the page)
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None

PAGES = {  # image -> (grade, gage, catalog page)
    # non-oriented silicon steel, fully processed, tested as-sheared
    "img001": ("M19", 29, 19), "img002": ("M22", 29, 27),
    "img003": ("M27", 29, 35), "img004": ("M36", 29, 43),
    "img005": ("M43", 26, 59), "img006": ("M45", 26, 69),
    "img007": ("M47", 26, 79), "img010": ("StatorB", 26, 85),
    # cold-rolled lamination sheet, .018 in, tested after a
    # decarburizing anneal at 1450 F -- low-carbon, not silicon, steel
    "img008": ("QCore_018in", 26, 95), "img009": ("Type2S_018in", 26, 101),
    # the same section ("4 USS Cold Rolled Lamination Sheet"), scanned
    # 2026-09-27: Stator B at 24 gage both ways -- fully processed and
    # tested as sheared (p.87), semiprocessed and tested after the 1450 F
    # decarburizing anneal (p.89) -- and the .024 in Q-Core and Type 2-S
    "img015": ("StatorB_24FP", 24, 87), "img016": ("StatorB_24SP", 24, 89),
    "img017": ("QCore_024in", 24, 97), "img018": ("Type2S_024in", 24, 103),
}


def source_path(name):
    import glob
    hits = glob.glob(os.path.join("SiliconSteel", name + ".*"))
    hits = [h for h in hits if h.lower().endswith((".png", ".bmp", ".tif",
                                                     ".tiff"))]
    if not hits:
        raise FileNotFoundError(name)
    return hits[0]
HDEC = [0.1, 1.0, 10.0, 100.0, 1000.0]   # the five decade rules
KG_PER_MAJOR = 2.0                       # major horizontal rule spacing
MU_PER_MAJOR = 1000.0


def groups(idx, gap):
    """Cluster sorted pixel indices closer than gap into their means."""
    out, cur = [], [idx[0]]
    for v in idx[1:]:
        if v - cur[-1] <= gap:
            cur.append(v)
        else:
            out.append(cur)
            cur = [v]
    out.append(cur)
    return np.array([np.mean(c) for c in out])


def runs(profile, thr):
    """(centre, width) of each contiguous run of profile > thr."""
    idx = np.nonzero(profile > thr)[0]
    if idx.size == 0:
        return []
    out, cur = [], [idx[0]]
    for v in idx[1:]:
        if v - cur[-1] <= 1:
            cur.append(v)
        else:
            out.append((np.mean(cur), len(cur)))
            cur = [v]
    out.append((np.mean(cur), len(cur)))
    return out


def calibrate(orange):
    """Return (x of decade rules, y of major horizontal rules, box).

    The rules are 4-10 px wide on ~20 px centres and the scan is slightly
    tilted, so a line is found from its run WIDTH on a narrow band of the
    image, where tilt does not smear it: the decades are the five widest
    vertical runs (and equally spaced in x), the 2 kG rules the widest
    horizontal ones (equally spaced in y).  The page's own orange border
    is wider still and is excluded as the outermost run on each side.
    """
    H, W = orange.shape

    def grid_only(rr):
        # a grid rule always has another rule within ~60 px (the finest
        # subdivision); the page border and the page edge do not
        # -- and at least two of them within 150 px (the 8 and 9 lines
        # sit 62 and 130 px inside the 1000 Oe rule), which a page edge
        # with one stray companion run does not
        rr = [t for t in rr if t[1] <= 16]
        keep = []
        for i, t in enumerate(rr):
            near = [abs(t[0] - u[0]) for j, u in enumerate(rr) if j != i]
            if sum(1 for d in near if d < 150) >= 2:
                keep.append(t)
        return keep

    # vertical rules: mean orange fraction over a 200-row band mid-chart
    band = orange[int(0.45 * H):int(0.45 * H) + 200, :]
    vr = grid_only(runs(band.mean(axis=0), 0.35))
    x0, x1 = vr[0][0], vr[-1][0]
    # The grid's left and right rules ARE 0.1 and 1000 Oe, so the interior
    # decades are predicted from them and the nearest actual rule taken;
    # the deviation is the check that the prediction found a real rule.
    xdec, xdev = [], []
    for k in range(5):
        xp = x0 + k * (x1 - x0) / 4.0
        near = min(vr, key=lambda t: abs(t[0] - xp))
        xdec.append(near[0])
        xdev.append(near[0] - xp)
    # horizontal rules likewise: bottom rule is 0 kG, top is 28 kG, the
    # 2 kG rules every (y1-y0)/14
    bandc = orange[:, int(0.5 * W):int(0.5 * W) + 200]
    hr = grid_only(runs(bandc.mean(axis=1), 0.35))
    y0, y1 = hr[0][0], hr[-1][0]
    ymaj, ydev = [], []
    for k in range(15):
        yp = y1 - k * (y1 - y0) / 14.0
        near = min(hr, key=lambda t: abs(t[0] - yp))
        ymaj.append(near[0])
        ydev.append(near[0] - yp)
    # the nearest rule to a decade or a 2 kG rule is a minor ~55 px off, so
    # a prediction within 20 px cannot pick the wrong one
    if max(map(abs, xdev)) > 20 or max(map(abs, ydev)) > 20:
        raise RuntimeError("grid rule not where predicted: xdev %s ydev %s"
                           % (np.round(xdev, 1), np.round(ydev, 1)))
    print("  rule prediction dev: x max %.1f px, y max %.1f px"
          % (max(map(abs, xdev)), max(map(abs, ydev))))
    return np.array(xdec), np.array(ymaj), (x0, x1, y0, y1)


def fit_line(px, vals):
    """vals = a + b*px, least squares; returns (a, b, rms residual in px)."""
    b, a = np.polyfit(px, vals, 1)
    pred = a + b * px
    return a, b, float(np.sqrt(np.mean(((pred - vals) / b) ** 2)))


def run(name):
    grade, gage, page = PAGES[name]
    src = source_path(name)
    im = Image.open(src).convert("RGB")
    a = np.asarray(im, dtype=np.uint8)
    r, g, b = a[..., 0].astype(np.int16), a[..., 1].astype(np.int16), \
        a[..., 2].astype(np.int16)
    lum = (r + g + b) // 3
    sat = a.max(axis=2).astype(np.int16) - a.min(axis=2).astype(np.int16)
    # grid ink is about (254,114,25); the tan of a page edge (211,184,151)
    # must not pass, hence the r-b and g limits
    orange = (r > 150) & (r - b > 90) & (g > 60) & (g < 170) & (sat > 90)
    black = (lum < 95) & (sat < 60)

    xdec, cy, (x0, x1, y0, y1) = calibrate(orange)
    ax, bx, rx = fit_line(xdec, np.log10(HDEC))   # log10 H = ax + bx*x
    print("  decade rules at x = %s  (spacings %s)"
          % (np.round(xdec).astype(int).tolist(),
             np.round(np.diff(xdec)).astype(int).tolist()))
    # horizontal majors: bottom one is B = 0; count upward
    cy = np.sort(cy)[::-1]                          # bottom first (largest y)
    ky = np.arange(len(cy)) * KG_PER_MAJOR
    ay, by, ry = fit_line(cy, ky)                   # B_kG = ay + by*y
    pxdec = 1.0 / bx
    print("%s %s (%d ga, p.%d): grid x %d..%d y %d..%d  %.1f px/decade  "
          "%.1f px/kG  %d major y-rules  calib rms x %.2f px, y %.2f px"
          % (name, grade, gage, page, x0, x1, y0, y1, pxdec, -1 / by,
             len(cy), rx, ry))

    # --- ink inside the grid, text removed by component size ---------------
    box = np.zeros_like(black)
    box[int(y0) + 4:int(y1) - 3, int(x0) + 4:int(x1) - 3] = True
    ink = black & box
    lab, n = ndimage.label(ink, structure=np.ones((3, 3)))
    sizes = np.bincount(lab.ravel())
    objs = ndimage.find_objects(lab)
    keep = np.zeros_like(ink)
    leader = None
    for i, sl in enumerate(objs, start=1):
        if sl is None:
            continue
        h_ = sl[0].stop - sl[0].start
        w_ = sl[1].stop - sl[1].start
        if w_ >= 0.12 * (x1 - x0) or (w_ >= 0.04 * (x1 - x0) and h_ >= 80):
            # a curve, or a piece of one: Type 2-S .024 in (p.103) prints
            # the top of its permeability peak as a separate stroke 9% of
            # the grid wide, which the width test alone threw away as text.
            # Text is letters, each narrow and under 80 px tall.
            keep |= (lab == i)
        elif w_ >= 40 and h_ <= 14 and w_ >= 6 * h_:
            # thin horizontal stroke: the Bs leader
            ys_, xs_ = np.nonzero(lab == i)
            cand = (xs_.max(), float(np.median(ys_)), w_)
            if leader is None or w_ > leader[2]:
                leader = cand
    ncurve = len(np.unique(lab[keep])) - 1
    print("  ink components: %d; curve components kept: %d" % (n, ncurve))

    # --- track the two curves column by column -----------------------------
    cols = range(int(x0) + 4, int(x1) - 3)
    tracks = {"mag": [], "mu": []}
    pred = {"mag": None, "mu": None}

    def clusters(col):
        ys = np.nonzero(keep[:, col])[0]
        if ys.size == 0:
            return []
        return [float(np.median(c)) for c in
                np.split(ys, np.nonzero(np.diff(ys) > 6)[0] + 1)]

    # The two curves cross exactly once.  Left of the crossing the
    # magnetization curve is the LOWER ink cluster (larger y), right of it
    # the UPPER; while they overlap there is one cluster, which both take.
    # Past the right end of the permeability curve there is again one
    # cluster, which only the magnetization track takes.
    # Each track takes the ink cluster nearest its own last position, and
    # only if that is within REACH pixels -- so a stroke that splits into
    # two clusters (a hole in the ink) cannot hand its second half to the
    # other track, and a gap in one curve's ink just leaves that track
    # waiting.  The topology is used only to break the one ambiguity: when
    # the merged crossing splits again, magnetization is the upper curve.
    # Each track predicts its next y by a straight line through its last
    # MEM columns and takes the cluster nearest that prediction, if within
    # REACH.  Through the crossing the two curves have very different
    # slopes (magnetization rising steeply, permeability near its peak),
    # so the predictions diverge with the curves and each track stays on
    # its own even where the strokes overlap and the ink is one cluster.
    # Only the magnetization curve is tracked: it is monotone, it spans the
    # whole plot, and through the crossing its slope in this log-linear
    # frame is steady, so a straight line through its last MEM columns
    # predicts it well and the nearest ink cluster within REACH is taken.
    # The permeability curve is then simply the OTHER ink: above the
    # magnetization curve before the crossing, below it after.  Ink below
    # the magnetization curve cannot exist before the crossing (it is the
    # lowest stroke on the page), so its first appearance IS the crossing.
    REACH, MEM = 30, 40
    started, crossed = False, False
    for col in cols:
        cl = sorted(clusters(col))
        if not cl:
            continue
        if not started:
            if len(cl) < 2:
                continue
            tracks["mag"].append((col, cl[-1]))  # lower = magnetization
            tracks["mu"].append((col, cl[0]))
            started = True
            continue
        t = tracks["mag"]
        if len(t) >= 8:
            xs_ = np.array([p[0] for p in t[-MEM:]], float)
            ys_ = np.array([p[1] for p in t[-MEM:]], float)
            sl, ic = np.polyfit(xs_, ys_, 1)
            pm = ic + sl * col
        else:
            pm = t[-1][1]
        cm = min(cl, key=lambda y: abs(y - pm))
        if abs(cm - pm) >= REACH:
            continue                             # gap in the ink
        t.append((col, cm))
        below = [y for y in cl if y > cm + 6]
        above = [y for y in cl if y < cm - 6]
        # Past the crossing the permeability ink lies below the magnetization
        # curve and none lies above it.  Ink below with ink still above is a
        # hole splitting the magnetization stroke (Type 2-S .024 in, p.103,
        # at 0.73 Oe), not the crossing.
        if below and not above and not crossed:
            crossed = True
            if os.environ.get("SIDBG"):
                print("  [dbg] crossing at col %d: clusters %s, mag %.0f"
                      % (col, [round(c) for c in cl], cm))
        if crossed:
            if below:
                tracks["mu"].append((col, max(below)))
        else:
            if above:
                tracks["mu"].append((col, min(above)))
            else:
                tracks["mu"].append((col, cm))   # overlapping strokes
    for k in tracks:
        tracks[k] = np.array(tracks[k])
    print("  crossing found: %s; permeability curve ends at col %d"
          % (crossed, tracks["mu"][-1][0]))

    def to_h(x):
        return 10.0 ** (ax + bx * x)

    def to_kg(y):
        return ay + by * y

    mag = tracks["mag"]
    mu = tracks["mu"]
    h_mag, b_mag = to_h(mag[:, 0]), to_kg(mag[:, 1])
    h_mu, mu_v = to_h(mu[:, 0]), to_kg(mu[:, 1]) * (MU_PER_MAJOR / KG_PER_MAJOR)
    # permeability self-check: B = mu * H / 1000  (kG, Oe)
    mu_at = np.interp(h_mag, h_mu, mu_v)
    b_from_mu = mu_at * h_mag / 1000.0
    sel = (h_mag >= h_mu.min()) & (h_mag <= h_mu.max()) & (b_mag > 1.0)
    dev = (b_from_mu[sel] - b_mag[sel]) / b_mag[sel]
    print("  magnetization: %d cols, H %.3f..%.0f Oe, B %.2f..%.2f kG"
          % (len(mag), h_mag[0], h_mag[-1], b_mag.min(), b_mag.max()))
    print("  permeability:  %d cols, H %.3f..%.0f Oe, mu max %.0f at %.2f Oe"
          % (len(mu), h_mu[0], h_mu[-1], mu_v.max(), h_mu[np.argmax(mu_v)]))
    print("  self-check mu*H vs B over %d cols: mean %+.2f%%  rms %.2f%%"
          % (sel.sum(), 100 * dev.mean(), 100 * np.sqrt((dev ** 2).mean())))
    bs = None
    if leader is not None:
        bs = to_kg(leader[1])
        print("  Bs leader: ends at x=%d (H %.0f Oe), y -> %.2f kG"
              % (leader[0], to_h(leader[0]), bs))
    else:
        print("  Bs leader: not found")

    out = np.column_stack([h_mag, b_mag, mu_at])
    fn = "out/si_%s_raw.csv" % grade
    np.savetxt(fn, out, delimiter=",", header="H_Oe,B_kG,mu", comments="",
               fmt="%.6g")
    # --- overlay for eyeballing --------------------------------------------
    s = 0.25
    ov = im.resize((int(im.width * s), int(im.height * s)), Image.LANCZOS)
    d = ImageDraw.Draw(ov)
    for col, y in mag[::6]:
        d.ellipse([col * s - 2, y * s - 2, col * s + 2, y * s + 2], outline="blue")
    for col, y in mu[::6]:
        d.ellipse([col * s - 2, y * s - 2, col * s + 2, y * s + 2], outline="green")
    for x in xdec:
        d.line([x * s, y0 * s, x * s, y1 * s], fill="cyan")
    for y in cy:
        d.line([x0 * s, y * s, x1 * s, y * s], fill="magenta")
    if leader is not None:
        d.ellipse([leader[0] * s - 5, leader[1] * s - 5, leader[0] * s + 5,
                   leader[1] * s + 5], outline="red", width=2)
    ov.save("out/si_overlay_%s.png" % grade)
    print("  wrote %s, out/si_overlay_%s.png" % (fn, grade))
    return dict(grade=grade, bs=bs, n=len(mag))


if __name__ == "__main__":
    names = sys.argv[1:] or sorted(PAGES)
    for nm in names:
        run(nm)
