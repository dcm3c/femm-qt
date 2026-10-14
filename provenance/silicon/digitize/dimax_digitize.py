# SPDX-License-Identifier: MIT
"""dimax_digitize.py -- extract B(H) from the ARMCO DI-MAX curve sheets.

Source: ARMCO, *ARMCO Nonoriented Electrical Steels*, ARMCO Specialty
Steels Division, Middletown, OH, 1984; seven pages scanned by the user
2026-09-23 as SiliconSteel/DI-MAX.pdf, each one "D-C Magnetization Curve
at High Induction" for a single DI-MAX grade.  Catalog pages 20 to 56 in
steps of 6, curve numbers 8288 to 8324 in steps of 6.  The same
design-manual series is Source 51 of NBS Special Publication 396-4
(1976), which describes it as covering "hot and cold rolled fully
processed and semiprocessed sheet steel" -- the FP/SP distinction these
sheets carry on their own faces.

These are a SECOND MANUFACTURER, which is the thing this family has been
short of.  Armco (American Rolling Mill Co, later AK Steel, now Cleveland-
Cliffs, whose modern DI-MAX bulletin is also in this directory) was a
competitor of US Steel, not its successor, so these curves are an
independent measurement of the same AISI grade names -- exactly the role
SLAC played against the US Steel charts in the low-carbon family.

Much easier to digitize than the USS pages: one black curve on a red grid,
no permeability curve to separate, no leader line.  The page is landscape
and must be rotated.

AXES, read from the sheet: magnetizing force 0.1 to 1000 oersteds over four
decades, induction 6 to 20 kilogausses (0.6 to 2.0 tesla) linear.  Note the
bottom of the chart is 6 kG, not zero -- these resolve the knee and the
approach to saturation and say nothing about the toe, which is the
complement of what the USS pages do well.

Each sheet states its own test condition, and they are not all the same:
"AS" (as sheared) on the fully processed grades, "QA" (quality annealed) on
the semi-processed ones, which is what those products are sold to have done
to them.  That is carried into the output.

    python dimax_digitize.py            -> out/dimax_<grade>_raw.csv (H_Oe, B_kG)
                                           out/dimax_overlay.png
"""
import os
import numpy as np
from PIL import Image
from scipy import ndimage

Image.MAX_IMAGE_PIXELS = None
PDF = "SiliconSteel/DI-MAX.pdf"
PPM = os.path.expanduser(
    r"~\AppData\Local\Programs\MiKTeX\miktex\bin\x64\pdftoppm.exe")
WORK = "out/_dimax"
DPI = 300

# page index (1-based in the PDF) -> (grade, processing, test, curve no,
#                                     catalog page)
PAGES = {
    1: ("M-47", "CR SP", "QA", 8324, 56),
    2: ("M-45", "CR FP", "AS", 8318, 50),
    3: ("M-43", "CR SP", "QA", 8312, 44),
    4: ("M-36", "CR FP", "AS", 8306, 38),
    5: ("M-22", "CR FP", "AS", 8300, 32),
    6: ("M-19", "CR FP", "AS", 8294, 26),
    7: ("M-15", "CR FP", "AS", 8288, 20),
}
H_LO, H_HI = 0.1, 1000.0        # oersteds, the frame's left and right edges
B_TOP, B_BOT = 20.0, 6.0        # kilogausses, the frame's top and bottom


def render():
    if not os.path.exists("%s/q-1.png" % WORK):
        os.makedirs(WORK, exist_ok=True)
        os.system('"%s" -r %d -png "%s" %s/q' % (PPM, DPI, PDF, WORK))


def frame(a):
    """Bounding box of the printed grid: (x0, x1, y0, y1).

    The grid is red, the curve and lettering black, the paper white.  Red
    is the only thing that fills the plot area, so its extent IS the frame,
    and no rule-finding is needed -- the axis values at the four edges are
    printed on the sheet and are exact by construction.
    """
    r, g, b = a[:, :, 0].astype(int), a[:, :, 1].astype(int), a[:, :, 2]
    red = (r > 90) & (r - g > 35) & (r - b.astype(int) > 25)
    cols, rows = red.sum(axis=0), red.sum(axis=1)
    cx = np.where(cols > 0.30 * cols.max())[0]
    cy = np.where(rows > 0.30 * rows.max())[0]
    return int(cx[0]), int(cx[-1]), int(cy[0]), int(cy[-1])


def curve_points(a, box):
    """(column, row) of the black curve inside the frame."""
    x0, x1, y0, y1 = box
    sub = a[y0:y1 + 1, x0:x1 + 1].astype(int)
    dark = (sub[:, :, 0] < 110) & (sub[:, :, 1] < 110) & (sub[:, :, 2] < 110)
    # Lettering inside the plot (the title block) is black too.  The curve
    # is one long thin component spanning most of the width; text is many
    # small blobs.  Keep only the largest component.
    lab, n = ndimage.label(dark, structure=np.ones((3, 3)))
    if n == 0:
        raise RuntimeError("no ink found")
    sizes = ndimage.sum(dark, lab, range(1, n + 1))
    keep = int(np.argmax(sizes)) + 1
    ink = lab == keep
    cols = []
    for c in range(ink.shape[1]):
        rr = np.where(ink[:, c])[0]
        if len(rr):
            cols.append((c, rr.mean()))
    return np.array(cols), ink


def main():
    render()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(9, 6))
    cmap = plt.get_cmap("viridis")
    print("%-6s %-6s %-4s %6s  %8s %8s  %9s %9s %9s"
          % ("grade", "proc", "test", "curve", "npts", "span dec",
             "B(1 Oe)", "B(100 Oe)", "B(1000 Oe)"))
    for i, (pg, (grade, proc, test, cno, cat)) in enumerate(
            sorted(PAGES.items())):
        im = Image.open("%s/q-%d.png" % (WORK, pg)).transpose(
            Image.ROTATE_270).convert("RGB")
        a = np.asarray(im)
        box = frame(a)
        pts, ink = curve_points(a, box)
        x0, x1, y0, y1 = box
        w, h = x1 - x0, y1 - y0
        hoe = H_LO * (H_HI / H_LO) ** (pts[:, 0] / w)
        bkg = B_TOP - (B_TOP - B_BOT) * (pts[:, 1] / h)
        k = np.argsort(hoe)
        hoe, bkg = hoe[k], bkg[k]
        # force strictly increasing in B, as the USS loader does
        bkg = np.maximum.accumulate(
            ndimage.median_filter(bkg, size=15, mode="nearest"))
        fn = "out/dimax_%s_raw.csv" % grade.replace("-", "")
        np.savetxt(fn, np.column_stack([hoe, bkg]), delimiter=",",
                   header="H_Oe,B_kG", comments="", fmt="%.6g")
        print("%-6s %-6s %-4s %6d  %8d %8.2f  %9s %9s %9s"
              % (grade, proc, test, cno, len(hoe),
                 np.log10(hoe[-1] / hoe[0]),
                 "%.2f" % np.interp(1.0, hoe, bkg) if hoe[0] <= 1 else "-",
                 "%.2f" % np.interp(100.0, hoe, bkg),
                 "%.2f" % np.interp(1000.0, hoe, bkg)
                 if hoe[-1] >= 1000 else "off chart"))
        ax.semilogx(hoe, bkg, "-", lw=1.4, color=cmap(i / 6.0),
                    label="%s %s (%s)" % (grade, proc, test))
    ax.set_xlim(0.1, 1000)
    ax.set_ylim(6, 21)
    ax.grid(True, which="both", alpha=0.3)
    ax.set_xlabel("H  [Oe]")
    ax.set_ylabel("B  [kG]")
    ax.legend(fontsize=8, loc="lower right")
    ax.set_title("ARMCO DI-MAX D-C magnetization curves at high induction\n"
                 "digitized from SiliconSteel/DI-MAX.pdf", fontsize=10)
    fig.tight_layout()
    fig.savefig("out/dimax_overlay.png", dpi=130)
    print("\nwrote out/dimax_*_raw.csv and out/dimax_overlay.png")


if __name__ == "__main__":
    main()
