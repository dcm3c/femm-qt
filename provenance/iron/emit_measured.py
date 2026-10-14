# SPDX-License-Identifier: MIT
"""emit_measured.py -- FEMM tables for the two MEASURED Armco curves.

These are not model output. They are digitized from AK Steel product data
bulletins and must stay distinguishable from the fitted low-carbon family,
which is why they get their own files and their own library folder.

  ArmcoPureIron  -- Figure 7, "representative low and medium induction values
                    for annealed ARMCO Pure Iron" (recrystallization anneal).
                    Single curve, no occlusion, H = 5 -> 17 632 A/m.
                    Corroborated by Figure 15 curve C (820 C, 3 mm hot rolled
                    sheet, ASTM A341) to RMS ln-H 0.041 over 0.01-1.49 T.
  Telar57        -- Figures 10/11/12, 8/10/12 mm cold drawn bar after magnetic
                    anneal (815 C, 1.5 h, 100% dry H2), IEC 60404-4, 2-sample
                    average. GEOMETRIC MEAN in H of the three diameters. The
                    4 mm bar (Figure 9) is excluded: the bulletin itself says
                    4 mm stock comes from heavier cold drawing and is "not
                    regarded as representative of the material", and it
                    digitizes ~80% harder in H at 0.1 T, consistent with that.

Each entry is a SPLICE of two measured curves from its bulletin: the
medium-induction figure(s) above, plus the HIGH-INDUCTION intrinsic curve
(Telar Figure 5, Pure Iron Figure 8; digitized by dig_highfield.py), which
carries the measurement through and past the anisotropy field
Ha = 2K1/Js ~ 4.5e4 A/m. The medium curve is authoritative over its whole
range; above it the high curve is used with the gap at the join added back
decaying as 1/H (splice_offset), so the entry stays in the condition it is
named for.  (They were once crossfaded over the top half-decade of the
medium curve, which for Pure Iron pulled 8-40 kA/m down onto Figure 8's
harder specimen, 2.4% low in B at 17 kA/m.) Only above the high-induction data (68 kA/m Telar,
786 kA/m Pure Iron) does the Akulov law of approach take over, which is
the regime where it is actually valid. (The previous revision spliced
Akulov directly onto the medium curves at ~10-17 kA/m, a factor 3-5 below
Ha; that over-saturated -- e.g. Telar B(24 kA/m) came out 2.108 T against
the bulletin's own tabulated 2.02-2.05 T.)

Js is GIVEN (datasheet / dilution anchor), never fitted from these curves;
see akulov_tail() for why that matters.
"""
import numpy as np
import bhlib as L
import femm_table as T

MU0 = 4e-7 * np.pi
HMAX = 4.0e5
NPTS = 61


def load(fn):
    d = np.loadtxt(fn, delimiter=",", skiprows=1)
    h, b = d[:, 0], d[:, 1]
    k = np.concatenate(([True], np.diff(b) > 0))
    return h[k], b[k]


def geo_mean_in_H(files, bgrid):
    """Average several curves in log H at fixed B -- the right average when
    the curves differ by a field scale rather than a level."""
    lh = []
    for f in files:
        h, b = load(f)
        lh.append(np.log(np.interp(bgrid, b, h)))
    return np.exp(np.mean(lh, axis=0)), np.std(lh, axis=0)


def akulov_tail(h, j, js):
    """Akulov coefficient with Js GIVEN, anchored to the last measured point.

    Js must NOT be fitted from this data. The law of approach J = Js(1-a/H^2)
    holds only for H >> the anisotropy field Ha = 2K1/Js ~ 4.5e4 A/m, and both
    measurements stop an order of magnitude below that -- fitting Js there
    returns ~1.93 T against a stated 2.15 T and then breaks monotonicity at
    the splice. So Js comes from the datasheet / dilution anchor exactly as it
    does for the fitted family, and only `a` is set here, by continuity at the
    last measured point.
    """
    a = max((1.0 - j[-1] / js) * h[-1] ** 2, 0.0)
    return js, a


def splice_offset(hm, jm, hh, jh):
    """Merge medium (hm, jm) with high-induction (hh, jh), the medium curve
    authoritative over its WHOLE range.  Above it the high curve is used
    with the gap measured at the medium curve's last point, dJ0, added back
    and decaying as 1/H:

        J(H) = Jh(H) + dJ0 * (H_end / H),   H > H_end

    Why.  Where the two overlap, a gap in J at fixed H that shrinks with
    field is a difference in wall pinning between the two specimens, and
    Bozorth's law of approach gives the wall (inhomogeneity) term as a0/H;
    rotation, which carries the curve toward saturation, does not depend on
    processing.  ARMCO Pure Iron Fig 8 runs 47 mT below Fig 7 at 17 kA/m
    and converges on it and on the fitted family to 7 mT by 70 kA/m and
    3 mT by 150 kA/m, which 1/H follows to within a few mT.  So the entry
    stays in the medium curve's condition -- the anneal it is named for --
    all the way up, and takes only the approach to saturation from the high
    curve.  Returns merged (h, j), dJ0 [T], and the RMS ln-H disagreement
    over the full overlap (the old seam number, for comparison)."""
    he = hm[-1]
    j_h_at_end = np.interp(np.log(he), np.log(hh), jh)
    dj0 = float(jm[-1] - j_h_at_end)
    jq = np.linspace(max(jm[np.searchsorted(hm, max(hh[0] * 1.05, 800.0))],
                         jh[0] * 1.02), jm[-1] * 0.999, 50)
    rms = float(np.sqrt(np.mean(np.log(np.interp(jq, jh, hh)
                                       / np.interp(jq, jm, hm)) ** 2)))
    up = hh > he * 1.001
    h = np.concatenate([hm, hh[up]])
    j = np.concatenate([jm, jh[up] + dj0 * he / hh[up]])
    # keep only points that rise above everything before them: digitizing
    # noise on a nearly flat curve makes single steps back that a
    # neighbour-only test lets through
    keep = np.concatenate(([True], j[1:] > np.maximum.accumulate(j)[:-1]))
    return h[keep], j[keep], dj0, rms


def full_curve(h, j, js, hmax=T.HMAX):
    """The data plus its Akulov tail out to hmax, densely, so that smoothing
    sees the join to the tail as well as the digitized points."""
    if h[-1] >= hmax:
        return h, j
    a = max((1.0 - j[-1] / js) * h[-1] ** 2, 0.0)
    ht = np.logspace(np.log10(h[-1]), np.log10(hmax), 200)[1:]
    return np.concatenate([h, ht]), np.concatenate([j, js * (1 - a / ht ** 2)])


def smooth_top(h, j, sigma, b_from=1.5, ramp=0.3, step=0.002):
    """Gaussian smoothing of J in log10 H, width sigma decades, applied only
    above the knee: its weight rises from 0 where J reaches b_from to 1
    over `ramp` decades.  Below that the curve is untouched.  A convolution
    of a rising curve with a positive kernel is still rising, so B stays
    monotone.

    Why.  The digitized bulletin curves carry bumps of a few per cent in
    slope where the curve is nearly flat (ARMCO Pure Iron Fig 7 at
    3.4-4.9 kA/m; Telar Fig 5 at 17-21 kA/m), and Telar's join to its
    Akulov tail at 68 kA/m is a kink in slope.  B is monotone either way,
    but the differential permeability turns more than once, failing the
    single-hump gate the fitted families are held to."""
    if sigma <= 0:
        return h, j
    lg = np.arange(np.log10(h[0]), np.log10(h[-1]), step)
    jg = np.interp(lg, np.log10(h), j)
    k = int(np.ceil(4 * sigma / step))
    x = np.arange(-k, k + 1) * step
    w = np.exp(-0.5 * (x / sigma) ** 2)
    w /= w.sum()
    js_ = np.convolve(np.pad(jg, k, mode="edge"), w, mode="valid")
    l1 = np.interp(b_from, jg, lg)
    wt = np.clip((lg - l1) / ramp, 0.0, 1.0)
    return 10.0 ** lg, (1 - wt) * jg + wt * js_


def splice(hm, jm, hh, jh):
    """Merge medium (hm, jm) with high-induction (hh, jh): the medium
    curve is authoritative over its whole range (it is the
    anneal-representative measurement), with a crossfade to the high
    curve only over the top ~half-decade before the medium data end.
    Returns merged (h, j) and the RMS ln-H disagreement over the FULL
    overlap -- printed as the honest seam number even though most of the
    overlap is not blended."""
    lo, hi = hm[-1] / 3.0, hm[-1] * 0.95
    full_lo = max(hh[0] * 1.05, 800.0)
    assert hi > lo * 1.2, "blend window too small"
    # seam disagreement over the FULL overlap, as horizontal lnH distance
    jq = np.linspace(max(jm[np.searchsorted(hm, full_lo)], jh[0] * 1.02),
                     jm[-1] * 0.999, 50)
    gap = np.log(np.interp(jq, jh, hh) / np.interp(jq, jm, hm))
    rms = float(np.sqrt(np.mean(gap ** 2)))

    hg = np.unique(np.concatenate([hm, hh]))
    w = np.clip(np.log(hg / lo) / np.log(hi / lo), 0, 1)
    jm_i = np.interp(np.log(hg), np.log(hm), jm)
    jh_i = np.interp(np.log(hg), np.log(hh), jh,
                     left=np.nan, right=np.nan)
    jh_i = np.where(np.isnan(jh_i), jm_i, jh_i)
    j = (1 - w) * jm_i + w * jh_i
    keep = np.concatenate(([True], np.diff(j) > 0))
    return hg[keep], j[keep], rms


def emit(tag, h, b, js_given, note, high_csv=None, h_start=None, mu_i=None):
    if h_start is not None:
        # drop the chart-floor points; below h_start the table follows
        # Rayleigh's mu = mu_i + nu H, matched at the first kept point
        k = h >= h_start
        h, b = h[k], b[k]
    j = b - MU0 * h
    if high_csv is not None:
        hh, bh = load(high_csv)
        jh = bh - MU0 * hh
        h_end = h[-1]
        h, j, dj0, seam = splice_offset(h, j, hh, jh)
        print("%-16s spliced with %s at %.0f A/m: gap %+.1f mT there, "
              "decaying as 1/H (overlap RMS lnH %.3f)"
              % (tag, high_csv, h_end, 1000 * dj0, seam))
    # Build the table with femm_table, the shared three-region builder, so
    # the curated entries and the raw inspection curves cannot be built two
    # different ways.  Its below-first-point branch ramps J linearly from
    # the origin (constant initial permeability); interpolating with a
    # clamp instead, as this function used to, left J flat below the first
    # measured point and made mu_r peak at the first grid point.
    js = js_given
    a = max((1.0 - j[-1] / js) * h[-1] ** 2, 0.0)
    print("%-16s data to H=%.0f A/m (J=%.3f T);  Js=%.4f T (given), "
          "Akulov a=%.3g above" % (tag, h[-1], j[-1], js, a))
    # The table must pass the fitted families' gates (bhlib.gate): B
    # increasing, dB/dH >= mu0, one turn in dB/dH with its maximum past the
    # first interval.  Smooth above the knee only as much as that takes.
    hf, jf = full_curve(h, j, js)
    out0, hg, _ = T.build(hf, jf + MU0 * hf, js, extend_low=True, mu_i=mu_i)
    for sigma in (0.0, 0.02, 0.03, 0.05, 0.07, 0.10, 0.15):
        hs, jsm = smooth_top(hf, jf, sigma)
        out, hg, _ = T.build(hs, jsm + MU0 * hs, js, extend_low=True,
                             mu_i=mu_i)
        dmin, turns, murat = L.gate(out, hg)
        if dmin >= 0.999 and turns <= 1 and murat < 1.0:
            break
    else:
        raise ValueError("%s: no smoothing up to 0.15 decade passes" % tag)
    dB = out - out0
    print("%-16s gates ok (bhlib.gate): turns=%d, mu_i/mu_max=%.2f; "
          "smoothed above 1.5 T at %.2f decade, max change %+.1f mT "
          "(%.2f%%), B(4e5)=%.3f T"
          % ("", turns, murat, sigma, 1000 * dB[np.argmax(np.abs(dB))],
             100 * np.max(np.abs(dB[1:] / out0[1:])), out[-1]))

    fn = "femm_%s.csv" % tag
    np.savetxt(fn, np.column_stack([out, hg]), delimiter=",", fmt="%.9f")
    print("%-16s wrote %s  -- %s\n" % ("", fn, note))
    return hg, out


# ---- ARMCO Pure Iron, Figure 7 + Figure 8 ----------------------------------
h, b = load("out/pi_fig7_annealed_raw.csv")
emit("ArmcoPureIron", h, b, 2.150,
     "Js 2.15 T stated in the bulletin; Fig 7 (5-17186 A/m), then Fig 8 "
     "high-induction curve (to 786 kA/m) with the gap at 17 kA/m added "
     "back as 1/H; no Akulov extension within the table; below 8 A/m "
     "Rayleigh mu = 200 + nu H (Bozorth Table 2 annealed iron), the chart-"
     "floor points 5-8 A/m dropped",
     high_csv="out/pi_fig8_high_raw.csv", h_start=8.0, mu_i=200.0)

# ---- ARMCO Telar 57, mean of 8/10/12 mm + Figure 5 --------------------------
bg = np.linspace(0.02, 1.83, 400)
hm, sd = geo_mean_in_H(["out/telar_8mm_annealed_raw.csv",
                        "out/telar_10mm_annealed_raw.csv",
                        "out/telar_12mm_annealed_raw.csv"], bg)
print("Telar 3-diameter scatter in H: median %.0f%%, max %.0f%% "
      "(1 sigma, over B=0.02-1.83 T)"
      % (100 * np.median(sd), 100 * sd.max()))
# Js from Eq. (5) of bhcurves at the 57S specification -- C at its 0.02
# maximum, Mn at the middle of 0.35-0.5 -- less the silicon family's
# coefficients (silib) for the 0.08 Si and the middle of 0.05-0.1 Al that
# the same analysis lists and Eq. (5) has no terms for: 2.1448 - 0.0079.
import silib as S
JS_TELAR = L.js_anchor(0.02, 0.425) - S.K_SI * 0.08 - S.K_AL * 0.075
emit("Telar57", hm, bg, JS_TELAR,
     "Js %.4f T from Eq. (5) at C 0.02, Mn 0.425, less Si 0.08 and Al 0.075 "
     "(Fig 5 reaches 2.114 at 68 kA/m, "
     "consistent); Figs 10/11/12 geomean, then Fig 5 high-induction curve "
     "(to 68 kA/m) with the gap at 9.6 kA/m added back as 1/H, Akulov "
     "above 68 kA/m; below its first averaged point (0.02 T) Rayleigh "
     "mu = 200 + nu H, as pure iron" % JS_TELAR,
     high_csv="out/telar_fig5_high_raw.csv", mu_i=200.0)
