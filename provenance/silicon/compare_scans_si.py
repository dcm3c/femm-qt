# SPDX-License-Identifier: MIT
"""compare_scans_si.py -- the ten scanned US Steel catalog curves against
the silicon family regressed from them, on one page.

Sibling of compare_scans.py, same house style and the same units as the
book itself: oersteds and kilogauss.  Nothing is re-fitted here -- the
curves are read back from out/si_params.json through simodel.SiFamily, so
what is drawn is exactly what the emitters write.

Three figures:

  out/cmp_si.png          all ten, grouped by product form, with the error
                          in H at fixed B underneath.
  out/cmp_si_grade.png    one panel per grade, same content per grade.
  out/cmp_si_pairs.png    the four alloy pairs -- two grades from one melt,
                          so what separates them is processing alone.

WHAT THE MARKERS ARE.  The filled markers are the trace the fit actually
saw: B blended from BOTH curves the page draws, the magnetization curve
and mu*H, weighted by which one the chart resolves at that field (see
silib.load_raw; they change over at 2.00 Oe exactly).  The faint dotted
line is the magnetization curve ALONE, as drawn.  Below 2 Oe the two part
company, because there the magnetization stroke is drawn on the axis and a
pen width is the whole value -- that gap is the reason for the blend, and
it is worth being able to see.

WHAT THE LINE IS.  The emitted entry for that grade.  Note that its level
is the composition law's, not the page's: above about 1.3 T the page runs
0.4-2.7% low at its top and the line deliberately sits above it.  That is
the same decision the measured entries make, and it is why the error row
is drawn only where the comparison is about SHAPE -- the ceiling per grade
is marked.
"""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
import silib as S
import simodel as M

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/si_params.json"
TAG = ("_" + sys.argv[2]) if len(sys.argv) > 2 else ""
F = M.SiFamily(SRC)
rows = S.table()
CMAP = plt.get_cmap("viridis")
# Colour by RANK in silicon, not by silicon itself: the four alloy pairs
# have identical resistivity, so colouring by composition gives the two
# members of a pair the same colour and makes the one figure that matters
# for them unreadable.  Rank keeps the ordering monotone and still
# separates twins, which then also differ in marker and line style.
RANK = {r["key"]: i for i, r in
        enumerate(sorted(S.table(), key=lambda x: (x["sieq"], x["key"])))}
TWIN = {"M22", "M36", "StatorB", "Type2S_018in"}    # second of each pair
HLINE = np.logspace(np.log10(0.08 * L.OE), np.log10(2000 * L.OE), 500)
SIG_LEVEL = 0.015           # spread of the pages' top-end shortfall
DEC = 60                    # marker decimation; the trace has ~5370 points

STAMP = ("%s:  Js = %.3f - %.3f (Si + 1.14 Al);  "
         "K1 = %.2fe4 (1 - Si/%.1f) J/m3 (Bozorth Fig. 12-16);  "
         "hR = Ha sqrt((2/105)/fr) x %.3f;  RMS H-error over the "
         "shape range = "
         % (SRC, S.JS_BASE, S.K_SI, L.K1 / 1e4, S.SI0,
            float(np.exp(F.par["lrs"][0]))))


def herr(key, h, j, eps=1e-3):
    """Fractional error in H at fixed B.  Positive = the regressed curve
    needs MORE field than the page did to reach the same induction."""
    jf = F.j(key, h)
    dj = (F.j(key, h * (1 + eps)) - jf) / eps
    return (j - jf) / dj


def shape_ceiling(key):
    """Field above which a one-sigma level error moves H by more than 10%,
    i.e. above which the comparison stops being about shape."""
    j = F.b(key, HLINE) - L.MU0 * HLINE
    slope = np.gradient(j, np.log(HLINE))
    # a level error eps scales J everywhere, so the field shift it costs is
    # eps * J / (dJ/dlnH) -- big only where the curve has gone flat
    good = np.where(SIG_LEVEL * j / np.maximum(slope, 1e-9) <= 0.10)[0]
    return HLINE[good[-1]] if len(good) else HLINE[0]


def scan(key):
    """Blended trace (what was fitted), and the magnetization curve alone."""
    h, b, _ = S.load_raw(key)
    d = np.loadtxt(S.raw_csv(key), delimiter=",", skiprows=1)
    return h, b, d[:, 1] * L.KG


def panel(ax, axr, keys, title, legend_fs=7.5, dec=DEC,
          fitcol=None):
    stats = []
    for k in keys:
        r = next(x for x in rows if x["key"] == k)
        col = CMAP(RANK[k] / (len(RANK) - 1.0))
        mk, ls = ("s", "--") if k in TWIN else ("o", "-")
        h, b, braw = scan(k)
        j = b - L.MU0 * h
        fc = fitcol or col
        ax.semilogx(h[::dec] / L.OE, b[::dec] / L.KG, mk, ms=3.4,
                    color=col, mec=col,
                    label="%s  (%.2f%% Si-eq)" % (r["name"], r["sieq"]))
        ax.semilogx(h / L.OE, braw / L.KG, ":", lw=0.9, color=col, alpha=0.75)
        ax.semilogx(HLINE / L.OE, F.b(k, HLINE) / L.KG, ls, lw=1.6,
                    color=fc, alpha=0.95,
                    label="regressed entry" if fitcol else "_nolegend_")
        e = herr(k, h, j)
        hc = shape_ceiling(k)
        sel = h <= hc
        axr.semilogx(h[sel] / L.OE, 100 * e[sel], ls, lw=1.3, color=fc)
        axr.semilogx(h[~sel] / L.OE, 100 * e[~sel], ":", lw=1.1, color=fc,
                     alpha=0.6)
        axr.plot([hc / L.OE], [0], "|", ms=11, color=fc)
        stats.append((r["name"], L.rms(e[sel]), L.rms(e), hc))
    ax.set_title(title, fontsize=11)
    ax.set_xlim(0.08, 1500)
    ax.set_ylim(0, 22)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=legend_fs, loc="upper left")
    axr.axhline(0, color="k", lw=0.8)
    for y in (-10, 10):
        axr.axhline(y, color="k", lw=0.6, ls=":", alpha=0.5)
    axr.set_xlim(0.08, 1500)
    axr.set_ylim(-32, 32)
    axr.grid(True, which="both", alpha=0.3)
    axr.set_xlabel("H  [Oe]")
    return stats


# ------------------------------------------------- 1. by product form
ASH = [r["key"] for r in rows if r["regime"] == 0]
DEC_ = [r["key"] for r in rows if r["regime"] == 1]
fig, ax = plt.subplots(2, 2, figsize=(15.5, 9.5),
                       gridspec_kw=dict(height_ratios=[2.4, 1]))
st = panel(ax[0, 0], ax[1, 0], ASH,
           "fully processed silicon sheet, tested as sheared")
st += panel(ax[0, 1], ax[1, 1], DEC_,
            "lamination sheet, tested after a 1450 F decarburizing anneal")
ax[0, 0].set_ylabel("B  [kG]")
ax[1, 0].set_ylabel("H error  [%]")
rms_all = L.rms([s[1] for s in st])
fig.suptitle("US Steel, Nonoriented Sheet Steel for Magnetic Applications "
             "(ADUSS 31-7103-01, May 1978): the ten catalog curves and the "
             "family regressed from them.\nMarkers: the blended page trace "
             "the fit saw.  Dotted: the magnetization curve alone -- below "
             "2 Oe it is drawn on the axis.  Line: the emitted entry.\n"
             "Lower row: error in H at fixed B, 100 x (H_fit - H_page)/"
             "H_page; dotted past the field where the page's own level "
             "uncertainty dominates (tick).", fontsize=10.5)
fig.text(0.5, 0.004, STAMP + "%.1f%%" % (100 * rms_all), ha="center",
         fontsize=8, color="#555555")
fig.tight_layout(rect=[0, 0.014, 1, 0.925])
fig.savefig("out/cmp_si%s.png" % TAG, dpi=115)
print("wrote out/cmp_si%s.png" % TAG)

# ------------------------------------------------- 2. one panel per grade
NR = -(-len(rows) // 5)                  # rows of grades, five to a row
fig, ax = plt.subplots(2 * NR, 5, figsize=(20, 5.75 * NR),
                       gridspec_kw=dict(height_ratios=[2.2, 1] * NR))
for k in range(len(rows), 5 * NR):       # blank the unused panels
    ax[2 * (k // 5), k % 5].axis("off")
    ax[2 * (k // 5) + 1, k % 5].axis("off")
for i, r in enumerate(rows):
    rr, cc = 2 * (i // 5), i % 5
    panel(ax[rr, cc], ax[rr + 1, cc], [r["key"]],
          "%s   %.0f mil, p.%d" % (r["name"], r["mils"], r["page"]),
          legend_fs=7, dec=110, fitcol="k")
    if cc == 0:
        ax[rr, cc].set_ylabel("B  [kG]")
        ax[rr + 1, cc].set_ylabel("H error  [%]")
fig.suptitle("Each catalog page against its own regressed entry -- "
             "RMS error in H over the shape range = %.1f%% overall"
             % (100 * rms_all), fontsize=12)
fig.text(0.5, 0.004, STAMP + "%.1f%%" % (100 * rms_all), ha="center",
         fontsize=8, color="#555555")
fig.tight_layout(rect=[0, 0.012, 1, 0.965])
fig.savefig("out/cmp_si_grade%s.png" % TAG, dpi=110)
print("wrote out/cmp_si_grade%s.png" % TAG)

# ------------------------------------------------- 3. the alloy pairs
#
# The lower row here is NOT model-vs-page like the other two figures.  It is
# the ratio of the two grades' fields at the same induction, for the pages
# and for the model -- i.e. the processing axis itself, plotted.  These two
# grades came from one melt, so every part of that curve is something the
# book did to the steel after casting and never wrote down.
PAIRS = [("M19", "M22"), ("M27", "M36"), ("M47", "StatorB"),
         ("QCore_018in", "Type2S_018in")]
PC = ("#1f3f8f", "#d95f02")      # contrasting, since twins are adjacent in Si
BB = np.linspace(0.2, 2.05, 200)

fig, ax = plt.subplots(2, 4, figsize=(19, 8.5),
                       gridspec_kw=dict(height_ratios=[2.2, 1.15]))
for i, (ka, kb) in enumerate(PAIRS):
    ra = next(x for x in rows if x["key"] == ka)
    rb = next(x for x in rows if x["key"] == kb)
    a, r_ = ax[0, i], ax[1, i]
    hp_ = {}
    for k, rr, col in ((ka, ra, PC[0]), (kb, rb, PC[1])):
        h, b, _ = scan(k)
        a.semilogx(h[::70] / L.OE, b[::70] / L.KG, "o", ms=3.4, color=col,
                   mec=col, label="%s page" % rr["name"])
        a.semilogx(HLINE / L.OE, F.b(k, HLINE) / L.KG, "-", lw=1.5,
                   color=col, alpha=0.95, label="%s regressed" % rr["name"])
        hp_[k] = (np.interp(BB, b, h, left=np.nan, right=np.nan),
                  np.interp(BB, F.b(k, HLINE), HLINE))
        hp_[k][0][(BB < b[0]) | (BB > b[-1])] = np.nan
    a.set_title("%s / %s   --  %.2f%% Si-eq, one alloy"
                % (ra["name"], rb["name"], ra["sieq"]), fontsize=11)
    a.set_xlim(0.08, 1500)
    a.set_ylim(0, 22)
    a.grid(True, which="both", alpha=0.3)
    a.legend(fontsize=8, loc="upper left")

    # Beyond the shape ceiling the two pages differ mostly in LEVEL, and the
    # model gives alloy twins one level by construction -- so the model's
    # ratio must fall to zero there while the pages' need not.  That is the
    # level decision made visible, not a misfit; shade it rather than let it
    # be read as one.
    bcut = min(float(F.b(k, shape_ceiling(k))) for k in (ka, kb))
    r_.axvspan(bcut, 2.1, color="0.85", alpha=0.55, lw=0)
    r_.text(bcut + 0.03, -34, "levels differ here,\nnot shapes", fontsize=7,
            color="0.35")
    r_.plot(BB, 100 * (hp_[kb][0] / hp_[ka][0] - 1), "o", ms=2.6,
            color="0.35", label="pages")
    r_.plot(BB, 100 * (hp_[kb][1] / hp_[ka][1] - 1), "-", lw=1.7,
            color="crimson", label="model")
    r_.axhline(0, color="k", lw=0.8)
    r_.set_xlim(0, 2.1)
    r_.set_ylim(-40, 40)
    r_.grid(alpha=0.3)
    r_.set_xlabel("B  [T]")
    r_.legend(fontsize=8, loc="upper left")
    if i == 0:
        a.set_ylabel("B  [kG]")
        r_.set_ylabel("extra field the 2nd needs\nover its twin  [%]",
                      fontsize=9)

fig.suptitle("The four alloy pairs: equal resistivity, so equal silicon and "
             "equal saturation.  Everything separating the two curves in a "
             "panel is PROCESSING,\nwhich the book never describes.  Lower "
             "row: the extra field the second grade needs at the same "
             "induction -- the processing axis itself, from the pages "
             "(dots) and\nfrom the model (line), where it is carried "
             "entirely by the per-grade residuals eW and eS.", fontsize=10.5)
fig.text(0.5, 0.004, STAMP + "%.1f%%" % (100 * rms_all), ha="center",
         fontsize=8, color="#555555")
fig.tight_layout(rect=[0, 0.014, 1, 0.915])
fig.savefig("out/cmp_si_pairs%s.png" % TAG, dpi=115)
print("wrote out/cmp_si_pairs%s.png" % TAG)

# ------------------------------------------------------------- the table
print("\n" + STAMP + "%.4f" % rms_all)
print("\nRMS error in H at fixed B, regressed entry vs its catalog page:")
print("  %-9s %12s %12s %12s" % ("grade", "shape range", "whole page",
                                 "shape ceiling"))
for name, rs, ra, hc in st:
    print("  %-9s %11.1f %% %11.1f %% %8.0f A/m  (%.0f Oe)"
          % (name, 100 * rs, 100 * ra, hc, hc / L.OE))
print("  %-9s %11.1f %% %11.1f %%"
      % ("overall", 100 * rms_all, 100 * L.rms([s[2] for s in st])))
