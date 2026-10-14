# SPDX-License-Identifier: MIT
"""nife_build.py -- nickel-iron library entries from Carpenter's DC curves.

  NiFe 48Ni, hydrogen anneal          (High Permeability 49 class)
  NiFe 48Ni, mill anneal (as received)
  NiFe 80Ni-4Mo, hydrogen anneal      (HyMu 80 class, ASTM A753 Alloy 4)
  NiFe 80Ni-5Mo, hydrogen anneal      (HyMu 800 class)

Data: out/nife_curves.csv (dig_nife.py), the DC normal-induction curves of
the held Carpenter datasheets, read from their vector artwork (HyMu 800:
from its scanned figure).  Each curve is one source set with equal total
weight.

Selection.  48Ni annealed pools the 2050 F curve of the anneal comparison
(0.060 in ring) with both bounds of Carpenter's "typical DC permeability
range" (0.014-0.062 in, 2150 F), so the entry runs down the middle of that
range.  The rotor-grade (isotropic strip) DC curves reach 0.1 T less at
10 Oe and are shown, not fitted.  48Ni as received is the one "typical mill
process annealed" curve.  80Ni-4Mo pools the three DC curves (0.006 and
0.014 in rings, 0.004 in toroid); 80Ni-5Mo is the 0.004 in toroid.

Saturation is given, never fitted (Bozorth, Ferromagnetism):
  48Ni      1.60 T  Fig. 5-8, p. 109, the 20 C curve peaks at 16,000 G near
                    48-50% Ni; App. 4 Table 2, Hipernik (50 Ni) 16,000.
                    Carpenter's text says "about 16000 gauss".
  80Ni-4Mo  0.82 T  Fig. 5-41, p. 136: 78.5% Ni at 4.2% Mo, 8,200 G at
                    1500 Oe.  (App. 4 lists 4-79 Permalloy at 8,700.)
  80Ni-5Mo  0.79 T  App. 4 Table 2 and p. 139: Supermalloy (79 Ni, 5 Mo)
                    saturates at 7,900 G; Fig. 5-41 at 5% Mo reads 7,800.

Form.  These are dense curves, not sparse tables, so no parametric form
is fitted: the entry IS the vendor curve, the way the AK Steel entries are
(emit_measured.py).  Each curve is carried past its last charted point
(10 Oe) toward the given Js by the two-term law of approach, a0/H + b0/H^2
matched to the curve's value and end slope; a curve ending climbing more
slowly than 1/H has its exponent ramped up to 1 (see tail()).  The
as-received 48Ni entry instead follows the annealed one above 10 Oe, its
gap there decaying as 1/H (offset_tail()).  Pooled curves are
averaged in ln H at fixed J, which keeps the knee as sharp as the curves
it comes from.  The result is smoothed, ln J in log H, at the narrowest
Gaussian width that passes the shared gate.  Below the lowest charted
point the entry is a straight line from the origin at that point's
permeability.

Usage:  python nife_build.py   -> femm_nife_<tag>.csv, nife_fit.dat,
                                  out/nife_fit.png, out/nife_fit.json
"""
import csv
import os
import json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import bhlib as L
from cofe_build import block

MU0 = 4e-7 * np.pi
HGRID = np.concatenate([[0.0], np.logspace(np.log10(0.05), np.log10(4.0e5),
                                           90)])
# read at import, so located beside this module: other papers' scripts
# import it from their own working directories
R = list(csv.DictReader(open(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "out", "nife_curves.csv"))))


def key(r):
    return r["source"] + ", " + r["fig"] + ", " + r["curve"]


ENTRIES = [
    dict(tag="48Ni_H2", name="NiFe 48Ni, hydrogen anneal", js=1.60,
         rho=48.0,
         keep=lambda r: r["source"] == "Carpenter HP49"
         and (r["curve"] == "2050F" or r["curve"].startswith("range")),
         show=lambda r: r["source"] == "Carpenter HP49"
         and r["fig"].startswith(("p7", "p11"))),
    dict(tag="48Ni_mill", name="NiFe 48Ni, mill anneal (as received)",
         js=1.60, rho=48.0, tail_from="48Ni_H2",
         keep=lambda r: r["source"] == "Carpenter HP49"
         and r["curve"] == "mill",
         show=lambda r: r["source"] == "Carpenter HP49"
         and r["curve"] in ("1450F", "1600F", "1750F")),
    dict(tag="80Ni4Mo_H2", name="NiFe 80Ni-4Mo, hydrogen anneal", js=0.82,
         rho=58.0,
         keep=lambda r: r["source"] == "Carpenter HyMu 80",
         show=lambda r: False),
    dict(tag="80Ni5Mo_H2", name="NiFe 80Ni-5Mo, hydrogen anneal", js=0.79,
         rho=62.3,
         keep=lambda r: r["source"] == "Carpenter HyMu 800",
         show=lambda r: False),
]


def points(e, sel):
    sets = {}
    for r in R:
        if sel(r):
            h = float(r["H_Am"])
            sets.setdefault(key(r), []).append(
                (h, float(r["B_T"]) - MU0 * h))
    return sets


def tail(h, j, js, hmax=4.0e5):
    """The curve plus a tail to hmax, from the curve's last point (H_e, J_e)
    toward Js, with deficit De = Js - J_e there.

    Until 2026-10-03 the deficit fell as (H_e/H)^n with n the curve's own
    end exponent clipped to 0.5..2, so dB/dH did not rise at the join; but
    no saturation is approached more slowly than 1/H (Bozorth's two-term
    law, Js - a0/H - b0/H^2), and vendor curves still climbing slowly at
    10 Oe left tails at n 0.5-0.85 that ran on unphysically.  Now, with
    n_e = (dJ/dlnH over the last 0.3 decade) / De:

      1 <= n_e <= 2 : deficit = De [(2 - n_e) He/H + (n_e - 1) (He/H)^2],
                      the two-term law with a0 and b0 both >= 0, matched
                      in value AND slope -- the curve's own slope decides
                      how much of the deficit is the fast 1/H^2 term;
      n_e > 2       : De (He/H)^2 (b0 alone; the slope falls at the join);
      n_e < 1       : the local exponent ramps up from n_e to 1 with
                      dn/dlnH = KAPPA n (n + 1), then the deficit falls as
                      a0/H.  dJ/dH = n D / H, and it is non-increasing in H
                      exactly when dn/dlnH <= n (n + 1), so with KAPPA < 1
                      the join makes no second hump.

    The returned n is n_e, the end exponent the curve itself measured.
    """
    je = j[-1]
    m = h >= h[-1] / 2.0
    sl = np.polyfit(np.log(h[m]), j[m], 1)[0] if m.sum() >= 3 else 0.0
    de = max(js - je, 1e-6)
    n_e = float(max(sl / de, 0.05))
    ht = np.logspace(np.log10(h[-1]), np.log10(hmax), 600)[1:]
    u = h[-1] / ht
    if n_e >= 2.0:
        d = de * u ** 2
    elif n_e >= 1.0:
        d = de * ((2.0 - n_e) * u + (n_e - 1.0) * u ** 2)
    else:
        x = np.log(ht / h[-1])
        d, nn, dk, xk = np.empty_like(x), n_e, de, 0.0
        for i, xi in enumerate(x):
            dx = xi - xk
            n_next = min(1.0, nn + KAPPA * nn * (nn + 1.0) * dx)
            dk *= np.exp(-0.5 * (nn + n_next) * dx)
            nn, xk, d[i] = n_next, xi, dk
    return np.concatenate([h, ht]), np.concatenate([j, js - d]), n_e


KAPPA = 0.5     # ramp rate for tail(), a margin under the n (n + 1) bound


def smooth(h, j, sigma, step=0.002):
    """Gaussian smoothing of ln J in log10 H, width sigma decades, over the
    whole curve.  Still monotone: a rising curve convolved with a positive
    kernel.  The low end is first extended one decade along the power law
    of its first points, so the edge is not biased by padding.

    Why.  The charted curves carry small bumps in slope (the artwork's own,
    the digitizer's, the join to the tail), and the differential
    permeability turns at each, failing the single-hump gate."""
    lg = np.arange(np.log10(h[0]) - 1.0, np.log10(h[-1]), step)
    x0, y0 = np.log10(h[:6]), np.log(j[:6])
    sl = np.polyfit(x0, y0, 1)[0]
    ly = np.where(lg < x0[0], y0[0] + sl * (lg - x0[0]),
                  np.interp(lg, np.log10(h), np.log(j)))
    if sigma > 0:
        k = int(np.ceil(4 * sigma / step))
        x = np.arange(-k, k + 1) * step
        w = np.exp(-0.5 * (x / sigma) ** 2)
        w /= w.sum()
        ly = np.convolve(np.pad(ly, k, mode="edge"), w, mode="valid")
    m = lg >= x0[0]
    return 10.0 ** lg[m], np.exp(ly[m])


def pool(sets, js):
    """Each curve with its tail, then the mean of ln H at fixed J over the
    J range every curve covers."""
    cur, ns = [], []
    for v in sets.values():
        v = np.array(sorted(v))
        h, j = v[:, 0], np.maximum.accumulate(v[:, 1])
        ht, jt, n = tail(h, j, js)
        ns.append(n)
        # strictly rising for interpolation in J; a flat run keeps its
        # first point, and the tail keeps the curve's true last point
        k = np.concatenate([[True], np.diff(jt) > 1e-7])
        k[len(h) - 1] = True
        k[:len(h) - 1] &= jt[:len(h) - 1] < jt[len(h) - 1] - 1e-7
        cur.append((ht[k], jt[k]))
    jlo = max(c[1][0] for c in cur)
    jhi = min(c[1][-1] for c in cur)
    jq = np.linspace(jlo, jhi, 2000)
    lnh = np.mean([np.interp(jq, c[1], np.log(c[0])) for c in cur], axis=0)
    return np.exp(lnh), jq, ns


def on_grid(h, j):
    """J on HGRID: interpolated in log H; below the first point a straight
    line from the origin at that point's permeability."""
    jg = np.interp(np.log(HGRID[1:]), np.log(h), j)
    lo = HGRID[1:] < h[0]
    jg[lo] = j[0] * HGRID[1:][lo] / h[0]
    b = np.concatenate([[0.0], jg + MU0 * HGRID[1:]])
    return b


def offset_tail(sets, ref):
    """One curve continued above its last point (H_e, J_e) along the
    reference entry ref (J on HGRID) with the gap at H_e added back and
    decaying as 1/H, as emit_measured.splice_offset does for the AK Steel
    entries: the anneal changes wall pinning, whose term in the law of
    approach goes as a0/H, and not rotation.  So the as-received entry
    converges on the annealed one from below instead of crossing it, as
    its own 1/H^2 tail from 10 Oe would."""
    (v,) = sets.values()
    v = np.array(sorted(v))
    h, j = v[:, 0], np.maximum.accumulate(v[:, 1])
    lj = np.log(HGRID[1:])
    dj0 = j[-1] - np.interp(np.log(h[-1]), lj, ref)
    up = HGRID[1:] > h[-1] * 1.001
    ht = HGRID[1:][up]
    h, j = np.concatenate([h, ht]), np.concatenate([j, ref[up]
                                                    + dj0 * h[-1] / ht])
    k = np.concatenate([[True], j[1:] > np.maximum.accumulate(j)[:-1]])
    return h[k], j[k], dj0


def build(e, built):
    sets = points(e, e["keep"])
    if e.get("tail_from"):
        h, j, dj0 = offset_tail(sets, built[e["tail_from"]])
        ns = ["offset %.3f T" % dj0]
    else:
        h, j, ns = pool(sets, e["js"])
    for sigma in (0.0, 0.01, 0.02, 0.03, 0.05, 0.07, 0.1, 0.15):
        hs, jsm = smooth(h, j, sigma)
        b = on_grid(hs, jsm)
        dmin, turns, murat = L.gate(b, HGRID)
        ok = bool((np.diff(b) > 0).all() and dmin >= 0.999 and turns <= 1
                  and murat < 1)
        if ok:
            break
    # disagreement of each curve with the entry, in ln H at the curve's J
    jb = b[1:] - MU0 * HGRID[1:]
    # knee (J < 0.95 Js): ln H at the curve's J; top: J at the curve's H
    res, top = {}, []
    for s, v in sets.items():
        v = np.array(v)
        m = (v[:, 1] > jb[0]) & (v[:, 1] < 0.95 * e["js"])
        res[s] = (v[m, 0], np.log(v[m, 0]) - np.interp(v[m, 1], jb,
                                                       np.log(HGRID[1:])))
        t = v[:, 1] >= 0.95 * e["js"]
        top += list(v[t, 1] - np.interp(np.log(v[t, 0]),
                                        np.log(HGRID[1:]), jb))
    return b, sets, res, np.array(top), sigma, ok, ns


def main():
    out, lines, built = {}, [], {}
    fig, ax = plt.subplots(2, 4, figsize=(24, 9),
                           gridspec_kw=dict(height_ratios=[2.2, 1]))
    for c, e in enumerate(ENTRIES):
        b, sets, res, top, sig, ok, ns = build(e, built)
        built[e["tag"]] = b[1:] - MU0 * HGRID[1:]
        lnh = np.concatenate([r[1] for r in res.values()])
        mu = b[1:] / (MU0 * HGRID[1:])
        sigma = round(100.0 / e["rho"], 2)
        np.savetxt("femm_nife_%s.csv" % e["tag"], np.column_stack([b, HGRID]),
                   delimiter=",", fmt="%.9f")
        rms = float(np.sqrt(np.mean(lnh ** 2)))
        rtop = 1000 * float(np.sqrt(np.mean(top ** 2))) if len(top) else 0.0
        print("%-38s Js %.2f  %d curves: knee RMS lnH %.3f, top RMS %2.0f mT"
              "  tail n %s  smooth %.2f  "
              "mu_max %6.0f at %.2f A/m  B(10 Oe) %.3f  gate %s  Sigma %.2f"
              % (e["name"], e["js"], len(sets), rms, rtop,
                 "/".join(n if isinstance(n, str) else "%.2f" % n for n in ns), sig, mu.max(),
                 HGRID[1:][mu.argmax()], np.interp(795.8, HGRID, b),
                 "ok" if ok else "FAIL", sigma))
        out[e["tag"]] = dict(name=e["name"], js=e["js"], smooth_dec=sig,
                             rms_lnh_knee=rms, rms_top_mT=rtop, tail_n=ns,
                             curves=list(sets),
                             mu_max=float(mu.max()), gate_ok=ok,
                             sigma_MSm=sigma)
        lines += block(e["name"], float(mu[0]), list(zip(b, HGRID)), sigma)
        a, ar = ax[0, c], ax[1, c]
        a.semilogx(HGRID[1:], b[1:] - MU0 * HGRID[1:], "k-", lw=1.6,
                   label="entry")
        for s, v in sets.items():
            v = np.array(v)
            a.semilogx(v[:, 0], v[:, 1], ".", ms=4, label=s)
        for s, v in points(e, e["show"]).items():
            v = np.array(v)
            a.semilogx(v[:, 0], v[:, 1], "-", color="gray", lw=0.8,
                       label="not fitted: " + s)
        a.axhline(e["js"], color="gray", lw=0.6, ls=":")
        a.set_title("%s  (Js %.2f T; knee RMS lnH %.3f)"
                    % (e["name"], e["js"], rms), fontsize=10)
        a.set_ylim(0, 1.7)
        a.set_xlim(0.05, 4e5)
        a.grid(True, which="both", alpha=0.3)
        a.legend(fontsize=6, loc="upper left")
        a.set_ylabel("J = B - mu0 H [T]")
        for s_, (hr, rr) in res.items():
            ar.semilogx(hr, rr, ".", ms=3)
        ar.axhline(0, color="k", lw=0.6)
        ar.set_xlim(0.05, 4e5)
        ar.set_ylim(-0.5, 0.5)
        ar.set_ylabel("curve - entry, ln H (J < 0.95 Js)")
        ar.set_xlabel("H [A/m]")
        ar.grid(True, which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig("out/nife_fit.png", dpi=95)
    with open("nife_fit.dat", "w", newline="\r\n") as f:
        f.write("\n".join(["<BeginFolder>", '<FolderName> = "Nickel Iron (Carpenter data)"']
                          + lines + ["<EndFolder>"]) + "\n")
    json.dump(out, open("out/nife_fit.json", "w"), indent=1)
    print("wrote femm_nife_*.csv, nife_fit.dat, out/nife_fit.png, "
          "out/nife_fit.json")


if __name__ == "__main__":
    main()
