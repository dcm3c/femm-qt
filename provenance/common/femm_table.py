# SPDX-License-Identifier: MIT
"""femm_table.py -- turn a digitized (H, B) trace into a FEMM B-H table.

Shared by emit_measured.py (the two curated library entries) and emit_fem.py
(the full inspection set), so a curve cannot be built two different ways.

Three regions, and the joins are where the mistakes live:

  H below the first measured point : J linear in H from the origin, i.e. a
        constant initial permeability. Honest -- it is below the data -- and
        it keeps dB/dH >= mu0 at the bottom.  With mu_i given (the curated
        AK Steel entries), Rayleigh's mu = mu_i + nu H instead.
  measured range                   : interpolation in log H.
  above the last measured point    : Akulov law of approach, J = Js(1-a/H^2),
        with `a` fixed by continuity at the last measured point.

Js is ALWAYS GIVEN, never fitted from the trace. The law of approach only
holds for H >> Ha = 2K1/Js ~ 4.5e4 A/m and every one of these curves stops
below that; fitting Js there returned 1.93 T against a stated 2.15 T and then
broke monotonicity at the splice. Js depends on composition, not on heat
treatment, so all Pure Iron anneals share one value and all Telar bars share
another.
"""
import numpy as np

MU0 = 4e-7 * np.pi
HMAX = 4.0e5
NPTS = 61


def load_raw(fn):
    """(H, B) of a dig_trace/dig_fig7 CSV, forced strictly increasing in B."""
    d = np.loadtxt(fn, delimiter=",", skiprows=1)
    h, b = d[:, 0], d[:, 1]
    k = np.concatenate(([True], np.diff(b) > 0))
    return h[k], b[k]


def build(h, b, js, hmax=HMAX, npts=NPTS, extend_low=False, mu_i=None,
          h_floor=1.0):
    """Return (B, H) columns on the standard FEMM grid.

    extend_low=True continues the grid's log spacing below 10 A/m down to
    the first measured point, and adds that point, so data the trace has
    below 10 A/m reach the table instead of being replaced by the linear
    ramp (pure iron's Figure 7 starts at 5 A/m; without this the table read
    mu_r 5,500 at 8 A/m against the figure's own 3,400).

    mu_i (with extend_low) replaces the linear ramp below the first measured
    point by Rayleigh's relation mu = mu_i + nu H, nu matched so B is
    continuous there, on the same log spacing down to h_floor A/m.

    Raises ValueError if the result would violate the gates, so a curve that
    cannot make a valid table fails loudly instead of being written.
    """
    j = b - MU0 * h
    # clamp: a digitized plateau a few mT above the given Js must not
    # produce a decreasing-J "approach" (a < 0); constant J is the
    # honest limit there.
    a = max((1.0 - j[-1] / js) * h[-1] ** 2, 0.0)

    hg = np.logspace(1, np.log10(hmax), npts - 1)
    r = hg[1] / hg[0]               # the grid's own log step
    if extend_low and h[0] < hg[0]:
        low = [hg[0] / r ** k for k in range(1, 60) if hg[0] / r ** k > h[0] * 1.02]
        hg = np.concatenate(([h[0]], low[::-1], hg))
    if extend_low and mu_i is not None:
        below = [hg[0] / r ** k for k in range(1, 60) if hg[0] / r ** k >= h_floor]
        hg = np.concatenate((below[::-1], hg))
        nu = (b[0] / (MU0 * h[0]) - mu_i) / h[0]
        if nu < 0:
            raise ValueError("first point below mu_i: no Rayleigh toe")
    hg = np.concatenate(([0.0], hg))
    out = np.zeros_like(hg)
    for i, hh in enumerate(hg):
        if hh == 0:
            out[i] = 0.0
        elif hh < h[0] and mu_i is not None:
            out[i] = MU0 * (mu_i + nu * hh) * hh           # Rayleigh
        elif hh < h[0]:
            out[i] = j[0] * (hh / h[0]) + MU0 * hh      # constant initial mu
        elif hh <= h[-1]:
            out[i] = np.interp(np.log(hh), np.log(h), j) + MU0 * hh
        else:
            out[i] = js * (1 - a / hh ** 2) + MU0 * hh

    db = np.diff(out)
    if not np.all(db > 0):
        raise ValueError("B not strictly monotone")
    if not np.all(db / np.diff(hg) >= MU0 * 0.999):
        raise ValueError("dB/dH < mu0")
    return out, hg, a


def mu_init(bb, hh):
    """Relative permeability at the first non-zero grid point."""
    return float(bb[1] / (MU0 * hh[1]))
