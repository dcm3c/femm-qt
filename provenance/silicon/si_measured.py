# SPDX-License-Identifier: MIT
"""si_measured.py -- FEMM tables straight from the ten US Steel 1978 pages.

These are the measurement, extended, not a model: each grade's digitized
curve as drawn, carried above the chart's right-hand edge (989 Oe) by the
Akulov law of approach to the saturation its own resistivity implies.  They
are the audit trail the regressed silicon family (bh_fit_si.py) is checked
against, exactly as the two curated ARMCO entries are for the low-carbon
family.

Three decisions, each of which the audit in si_composition.py forced:

LEVEL.  Js is never read off the chart.  It comes from the book's own
resistivity through silib.js(), and every page is rescaled by a single
factor in J so that its top meets that level on the Akulov curve.  This is
uniform treatment, not a repair of three bad pages: si_composition.py's
audit puts seven grades within 1% and three outside it, but ALL TEN sit
low, by 0.4 to 2.7%, with only Stator B (-0.4%, i.e. high) on the other
side.  A threshold would have corrected the tail of a distribution and left
its body alone.  Correcting in J, uniformly, is also what the fitted family
does with its per-curve dJs nuisance.  The factors are printed.

TAIL.  Above the last digitized point the table follows J = Js(1 - a/H^2)
with a = (2/105) Ha^2 built from Bozorth's K1(Si) -- and because the level
correction is defined to put the last point on exactly that curve, the
splice is continuous in value and in slope, with nothing fitted at the
seam.  The price is that the continuity-fitted `a` used everywhere else in
this project is no longer an independent check here: fitted blind it comes
out 2-4 times the theoretical value, which is simply the statement above
that the drawn curves stop 0.4-2.7% short.

GAUGE.  The tables are B against H for the SOLID material.  Lamination
stacking is a separate FEMM concern and is not folded in.

Usage:  python si_measured.py      -> femm_simeas_<grade>.csv, out/si_measured.png
"""
import numpy as np

import bhlib as L
import femm_table as T
import silib as S

PLOT = "out/si_measured.png"


def gates(b, h):
    """min dB/dH in units of mu0, and the number of interior mu maxima."""
    db = np.diff(b)
    slope = db / np.diff(h) / L.MU0
    mu = b[1:] / (L.MU0 * h[1:])
    dmu = np.diff(mu)
    dmu[np.abs(dmu) < 1e-9 * mu.max()] = 0.0
    sg = np.sign(dmu)
    peaks = int(np.sum((sg[:-1] > 0) & (sg[1:] < 0)))
    return bool(np.all(db > 0)), float(slope.min()), peaks


print("grade       rho  Si-eq  Js(rho)  page low  Hend/Ha    a        "
      "mu_max   B(4e5)  mono  dB/dH  mu-peaks")
rows = S.table()
out = {}
nfail = 0
for r in rows:
    h, b, _ = S.load_raw(r["key"])
    j = b - L.MU0 * h
    scale = r["js_rho"] / r["js_curve"]
    j = j * scale

    bb, hg, a = T.build(h, j + L.MU0 * h, r["js_rho"])
    a_th = (2.0 / 105.0) * r["ha"] ** 2
    mono, dmin, peaks = gates(bb[1:], hg[1:])
    ok = mono and dmin >= 0.999 and peaks <= 1
    nfail += not ok
    mumax = float(np.max(bb[1:] / (L.MU0 * hg[1:])))

    fn = "femm_simeas_%s.csv" % r["key"]
    np.savetxt(fn, np.column_stack([bb, hg]), delimiter=",", fmt="%.9f")
    out[r["key"]] = (hg, bb, h, b, scale, a, a_th)

    assert abs(a / a_th - 1) < 1e-6, "%s: seam is not the Akulov curve" % r["name"]
    print("%-9s %4d %5.2f  %7.3f  %+7.2f%%  %6.2f  %9.3g  %6.0f  %6.3f  "
          "%-4s  %5.2f  %d%s"
          % (r["name"], r["rho"], r["sieq"], r["js_rho"], 100 * (scale - 1),
             r["h_last"] / r["ha"], a, mumax, bb[-1],
             "ok" if mono else "NO", dmin, peaks,
             "" if ok else "   <== GATE FAIL"))

print("\n%d tables, %d gate failures" % (len(rows), nfail))
sc = np.array([100 * (out[r["key"]][4] - 1) for r in rows])
print("page low at the top: %+.2f%% median, %+.2f to %+.2f%%; %d of %d pages "
      "low.  Level from the book's resistivity, tail from Bozorth's K1(Si) "
      "-- nothing fitted.  Correlation of the shortfall with Si-eq: %+.2f"
      % (np.median(sc), sc.min(), sc.max(), int(np.sum(sc > 0)), len(sc),
         np.corrcoef([r["sieq"] for r in rows], sc)[0, 1]))

# ------------------------------------------------------------------ figure
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

fig, ax = plt.subplots(1, 2, figsize=(12.5, 5.6))
cmap = plt.get_cmap("viridis")
for i, r in enumerate(rows):
    hg, bb, h, b, scale, a, a_th = out[r["key"]]
    col = cmap(i / (len(rows) - 1.0))
    lab = "%s  (%.2f%% Si-eq)" % (r["name"], r["sieq"])
    ax[0].plot(hg[1:], bb[1:], "-", color=col, lw=1.4, label=lab)
    ax[0].plot(h, b, ":", color=col, lw=2.2)
    ax[1].plot(hg[1:], bb[1:] - L.MU0 * hg[1:], "-", color=col, lw=1.4)
    ax[1].plot(h, b - L.MU0 * h, ":", color=col, lw=2.2)
    ax[1].axhline(r["js_rho"], color=col, lw=0.6, ls="--")
ax[0].set_xscale("log")
ax[0].set_xlim(10, 4e5)
ax[0].set_ylim(0, 2.3)
ax[0].set_xlabel("H  (A/m)")
ax[0].set_ylabel("B  (T)")
ax[0].set_title("dotted = page as digitized, solid = emitted table")
ax[0].grid(alpha=0.3, which="both")
ax[0].legend(fontsize=7, loc="upper left")
ax[1].set_xscale("log")
ax[1].set_xlim(3e3, 4e5)
ax[1].set_ylim(1.55, 2.2)
ax[1].set_xlabel("H  (A/m)")
ax[1].set_ylabel("J = B - $\\mu_0$H  (T)")
ax[1].set_title("approach to saturation: Akulov above the chart edge\n"
                "(dashed = Js from the book's resistivity)", fontsize=10)
ax[1].grid(alpha=0.3, which="both")
fig.suptitle("US Steel, Nonoriented Sheet Steel for Magnetic Applications, "
             "ADUSS 31-7103-01 (May 1978): measured pages extended to "
             "saturation\nJs from the book's own resistivity via "
             "Js = %.3f - %.3f (Si + 1.14 Al);  K1(Si) from Bozorth "
             "Fig. 12-16" % (S.JS_BASE, S.K_SI), fontsize=9)
fig.tight_layout(rect=(0, 0, 1, 0.93))
fig.savefig(PLOT, dpi=130)
print("wrote %s" % PLOT)
