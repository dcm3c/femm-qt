# SPDX-License-Identifier: MIT
"""bhmodel.py -- the model forms, in one place.

Every consumer of a fitted parameter set (the fit itself, the emitters,
the gates, the comparison plots) used to carry its own copy of shape().
Two model versions now coexist, so the forms live here and everything
else calls in.

  shape_v10(par, c, d, h)   per-condition wall parameters (8 per cond)
  shape_fit(par, c, d, h, js)  two regimes; pinning scales per condition,
                            knee width per regime, tail exponent global
  toe(h, hi, cr)            Rayleigh quadratic through a saturating map;
                            cr = 0 is the plain h/(h+hi)

`par` is a dict name -> array, either sliced from the optimiser's flat
vector during a fit or loaded from a saved JSON afterwards.  Family()
wraps a saved JSON and dispatches on its "model" tag.
"""
import json
import numpy as np
import bhlib as L


def burr(h, a, k, m):
    return 1 - (1 + (h / a)**k)**(-m)


def rayburr(h, a, k):
    """One wall kernel with an H^2 onset and a 1/H tail; k turns the knee.

        F = [1 - (1+u^k)^(-1/k)]^(2/k),  u = h/a

    Computed through logaddexp and expm1 so that u^k cannot overflow and
    the small-u bracket keeps its precision.  a0 = 2a/k.
    """
    u = np.maximum(np.asarray(h, float), 1e-30) / a
    x = np.logaddexp(0.0, k * np.log(u)) / k      # ln(1+u^k)/k
    g = -np.expm1(-x)                             # 1 - (1+u^k)^(-1/k)
    return np.exp((2.0 / k) * np.log(np.maximum(g, 1e-300)))


def softwall(h, a):
    """The k=2, m=1/2 Burr: 1 - 1/sqrt(1+u^2).

    Rayleigh's irreversible H^2 at the bottom, Neel's 1 - a/H at the
    top, from the one population.  a0 = a.
    """
    u = np.asarray(h, float) / a
    return 1.0 - 1.0 / np.sqrt(1.0 + u * u)


def burr_d(h, a, k, m):
    """dF/dh of burr(), for the C1 splice."""
    u = (h / a)**k
    return m * k * u / h * (1 + u)**(-m - 1)


def toe(h, hi, cr=0.0):
    """Rayleigh's quadratic, saturated by Frolich-Kennelly.

        u = h/hi,  v = u(1 + cr u),  toe = v/(1+v)

    v is Rayleigh's law in reduced field; v/(1+v) is the same rational
    saturation the plain form used.  The expansion is u + (cr-1)u^2, so
    the permeability rises from the origin for cr > 1 and falls for
    cr < 1.  The initial slope is 1/hi whatever cr is, so hi keeps its
    meaning: mu_i = fi*Js/(hi*mu0).

    cr = 0 gives h/(h+hi) exactly, which is what the model used before
    and what an archived parameter set without a curvature evaluates to.
    """
    u = h / hi
    v = u * (1.0 + cr * u)
    return v / (1.0 + v)


def _sig(z):
    return 1 / (1 + np.exp(-z))


def toe_peak_u(cr):
    """Where toe()'s permeability stops rising, in units of h_t.

    d/du of v/(1+v) with v = u(1+cr u) is (1+2cr u)/(1+v)^2, and setting
    its derivative to zero gives 3cr^2 u^2 + 3cr u + (1-cr) = 0, whose
    positive root is this.  It tends to 1/sqrt(3cr) for large cr and to 0
    as cr -> 1, where the curvature vanishes and there is no peak.
    """
    return (-3.0 + np.sqrt(12.0 * cr - 3.0)) / (6.0 * cr)


def regime(d, nreg=2):
    """0 = as hot rolled (dislocation pinning), 1 = furnace-annealed.

    With nreg = 3 the anneals are split at A1 (727 C, 1341 F): 1 = annealed
    below it (1300 F), 2 = annealed above it (1400 and 1500 F).
    """
    if nreg == 3:
        return {2: 0, 3: 1, 4: 2, 5: 2}[d]
    return 0 if d == 2 else 1


def _reg(par, d):
    """Regime of condition d under par's own grouping (2 or 3 regimes)."""
    return regime(d, len(par["lkW"]) if "lkW" in par else 2)


def shape_v10(par, ci, c, d, h):
    j = ci[d]
    x = c / 0.77
    fr = _sig(par["lfr"][0])
    fi = _sig(par["lfi"][j] + par["bFI"][0] * x)
    hi = np.exp(par["lhi"][j] + par["bI"][0] * x)
    hR, kR, mR = np.exp(par["rot"])
    hW = np.exp(par["lhW"][j] + par["bW"][j] * x)
    kW = np.exp(par["lkW"][j] + par["bK"][j] * x)
    mW = np.exp(par["lmW"][j] + par["bM"][j] * x)
    return (fi * h / (h + hi) + (1 - fi - fr) * burr(h, hW, kW, mW)
            + fr * burr(h, hR, kR, mR))


def wall_fit(par, ci, c, d):
    """The condition-resolved process parameters for carbon c, cond d."""
    j, r, x = ci[d], _reg(par, d), c / 0.77
    fi = (_sig(par["lfi"][0] + par["bFI"][0] * x)
          if "lfi" in par else np.nan)
    # Absent under the "tied" layout, where h_t follows h_W instead of
    # being fitted; shape_tied computes it and ignores this.
    hi = (np.exp(par["lhi"][j] + par["bI"][r] * x)
          if "lhi" in par else np.nan)
    bW = par["bW"]
    # bW per regime (adopted: the anneals may differ only by a grade-
    # independent pinning shift) or per condition (the experiment that
    # let 1030 harden from 1400F to 1500F on one datasheet's word)
    # per regime when bW has one entry per regime, else per condition
    hW = np.exp(par["lhW"][j] + (bW[r] if len(bW) == len(par["lkW"]) else bW[j]) * x)
    kW = np.exp(par["lkW"][r] + par["bK"][r] * x)
    if "ltail" in par:
        lt = par["ltail"]
        mW = np.exp(lt[r] if len(lt) > 1 else lt[0]) / kW  # global or per regime
    else:
        mW = np.nan            # the "rb" kernel has no m
    return fi, hi, hW, kW, mW


def shape_v11(par, ci, c, d, h, js=None):
    """The superseded form: hR a free parameter, Akulov only a prior.

    Kept so the archived variant runs under out/v11_*/ still load.  js is
    accepted and ignored, so the caller need not know which form it has.
    """
    fr = _sig(par["lfr"][0])
    hR, kR, mR = np.exp(par["rot"])
    fi, hi, hW, kW, mW = wall_fit(par, ci, c, d)
    return (fi * h / (h + hi) + (1 - fi - fr) * burr(h, hW, kW, mW)
            + fr * burr(h, hR, kR, mR))


def shape_fit(par, ci, c, d, h, js):
    """Rotation scale tied to the composition's own anisotropy field.

        hR = (2 K1 / js) sqrt((2/105) / fr) exp(lrs)

    K1 is a property of iron and does not move with carbon, but Js does,
    so Ha = 2 K1 / Js moves with composition: 44.6 kA/m at ARMCO grade 2
    up to 46.0 at 1030.  Only the overall level exp(lrs) is fitted, which
    is how simodel.py does it for silicon.  The Akulov relation is
    therefore built into the parameterisation, not pulled toward by a
    residual.
    """
    fr = _sig(par["lfr"][0])
    kR, mR = np.exp(par["rot"])
    hR = (2 * L.K1 / js) * np.sqrt((2.0 / 105.0) / fr) * np.exp(par["lrs"][0])
    fi, hi, hW, kW, mW = wall_fit(par, ci, c, d)
    # Toe curvature, per regime.  Absent from a parameter set fitted
    # before the toe had one, in which case cr = 0 and toe() is the plain
    # h/(h+hi) those parameters were fitted against.
    cr = np.exp(par["lc"][regime(d)]) if "lc" in par else 0.0
    return (fi * toe(h, hi, cr) + (1 - fi - fr) * burr(h, hW, kW, mW)
            + fr * burr(h, hR, kR, mR))


def shape_tied(par, ci, c, d, h, js):
    """As shape_fit, but the toe's scale follows the wall pinning field.

    The toe is reversible bowing of walls that have not broken away yet,
    so its rise ends where they start to move: its permeability peaks at
    h_W.  That fixes h_t = h_W / toe_peak_u(cr) and leaves one number per
    regime, cr, for the whole toe shape -- lhi and bI are gone, and the
    toe's carbon dependence is inherited from h_W, which is what sharing
    a pinning field means.
    """
    fr = _sig(par["lfr"][0])
    kR, mR = np.exp(par["rot"])
    hR = (2 * L.K1 / js) * np.sqrt((2.0 / 105.0) / fr) * np.exp(par["lrs"][0])
    fi, _hi, hW, kW, mW = wall_fit(par, ci, c, d)
    cr = 1.0 + np.exp(par["lcr"][regime(d)])
    ht = hW / toe_peak_u(cr)
    return (fi * toe(h, ht, cr) + (1 - fi - fr) * burr(h, hW, kW, mW)
            + fr * burr(h, hR, kR, mR))


def _rot(par, js, d=None, c=None):
    """(fr, hR, kR, mR): the rotation term, identical in every model.

    lrs has one entry (adopted) or one per regime (LRS_PER_REGIME=1, the
    test of whether the level tracks residual stress by condition).  An
    optional carbon slope bR (LRS_BC=1) makes it lrs + bR * C/0.77, the
    test of whether the excess over Akulov grows with cementite.
    """
    fr = _sig(par["lfr"][0])
    kR, mR = np.exp(par["rot"])
    lrs = par["lrs"]
    lrs = lrs[_reg(par, d)] if len(lrs) > 1 else lrs[0]
    if "bR" in par and c is not None:
        lrs = lrs + par["bR"][0] * c / 0.77
    hR = (2 * L.K1 / js) * np.sqrt((2.0 / 105.0) / fr) * np.exp(lrs)
    return fr, hR, kR, mR


def shape_rb(par, ci, c, d, h, js):
    """One wall kernel carrying both laws of approach, plus a tied toe.

    rayburr has zero permeability at the origin, so a reversible term is
    still needed for mu_1; its scale is tied to the wall pinning field
    rather than fitted.
    """
    fr, hR, kR, mR = _rot(par, js)
    r = regime(d)
    fi, _hi, hW, kW, _mW = wall_fit(par, ci, c, d)
    hi = hW * np.exp(par["lqt"][r])
    return (fi * h / (h + hi) + (1 - fi - fr) * rayburr(h, hW, kW)
            + fr * burr(h, hR, kR, mR))


def shape_two(par, ci, c, d, h, js, straight=False):
    """Soft and sharp wall populations, with the toe tied to the soft one.

    soft = softwall(h, hS) carries Rayleigh's irreversible term and
    Neel's a0; the sharp Burr carries the knee; the toe carries chi_0.
    The toe is u/(1+u), or with straight=True u/sqrt(1+u^2), which has
    no u^2 term and a 1/H^2 tail.
    """
    fr, hR, kR, mR = _rot(par, js, d, c)
    r, x = _reg(par, d), c / 0.77
    fi, _hi, hW, kW, mW = wall_fit(par, ci, c, d)
    fS = _sig(par["lfS"][0] + par["bFS"][0] * x)
    hS = hW * np.exp(par["lqS"][r])
    fk = 1.0 - fi - fS - fr
    h = np.asarray(h, float)
    toe = h / np.sqrt(h * h + hS * hS) if straight else h / (h + hS)
    return (fi * toe + fS * softwall(h, hS)
            + fk * burr(h, hW, kW, mW) + fr * burr(h, hR, kR, mR))


def shape_two_straight(par, ci, c, d, h, js):
    """MODEL=two with the straight toe (variant TOE = "straight")."""
    return shape_two(par, ci, c, d, h, js, straight=True)


def splice_core(par, ci, c, d, js):
    """The wall + rotation model with no toe, and its join field.

    Returns (core, core_d, H2): the shape and its h-derivative as
    callables, and the join H2 = h_W exp(lq[regime] + bQ[regime] x).
    """
    fr = _sig(par["lfr"][0])
    kR, mR = np.exp(par["rot"])
    hR = (2 * L.K1 / js) * np.sqrt((2.0 / 105.0) / fr) * np.exp(par["lrs"][0])
    _fi, _hi, hW, kW, mW = wall_fit(par, ci, c, d)
    # The join window moves up with carbon because bK sharpens the knee,
    # so the join carries its own carbon slope; without it one ratio per
    # regime lands 1010 inside its window and 1030 on the edge, where
    # mu_1 collapses.
    r = regime(d)
    bq = par["bQ"][r] if "bQ" in par else 0.0
    H2 = hW * np.exp(par["lq"][r] + bq * c / 0.77) if "lq" in par else np.nan

    def core(h):
        return (1 - fr) * burr(h, hW, kW, mW) + fr * burr(h, hR, kR, mR)

    def core_d(h):
        return (1 - fr) * burr_d(h, hW, kW, mW) + fr * burr_d(h, hR, kR, mR)

    return core, core_d, H2


def splice_rayleigh(par, ci, c, d, js):
    """The Rayleigh coefficients (a, b) the C1 join at H2 implies.

    J/Js = a h + b h^2 below H2.  a = 2 core(H2)/H2 - core'(H2) is the
    initial slope, and it is positive only where the model's local
    log-log slope at H2 is below 2; b > 0 wherever that slope is above 1.
    """
    core, core_d, H2 = splice_core(par, ci, c, d, js)
    f2, s2 = core(H2), core_d(H2)
    return 2 * f2 / H2 - s2, (s2 * H2 - f2) / H2**2, H2


def splice_join_mu(par, ci, c, d, js):
    """The join that gives the asked-for initial permeability.

    a(H) = 2 core(H)/H - core'(H) is the Rayleigh initial slope a C1 join
    at H implies; it is 0 where the model's local log-log slope is 2 and
    peaks where that slope is 1.  The toe asks for a_t = mu_1 mu_0 / Js
    with ln(mu_1 mu0 h_W/Js) = lm[regime] + bM[regime] x, and H2 is the root of
    a(H) = a_t on the rising side of that window.  Returns (a, b, H2,
    shortfall): shortfall is a_t - max a when the ask cannot be met, else 0.
    """
    core, core_d, _ = splice_core(par, ci, c, d, js)
    r, x = regime(d), c / 0.77
    # mu_1 scales with Js/(mu0 h_W): a softer anneal, with its lower
    # pinning field, gets a higher initial permeability by inheritance,
    # the same way the join ratio did.  lm is ln(mu_1 mu0 h_W / Js).
    _fi, _hi, hW, _kW, _mW = wall_fit(par, ci, c, d)
    a_t = np.exp(par["lm"][r] + par["bM"][r] * x) / hW
    hh = np.logspace(0.5, 4.5, 81)
    aa = 2 * core(hh) / hh - core_d(hh)
    i = int(np.argmax(aa))
    if aa[i] <= a_t:                       # cannot be met: sit at the top
        H2, short = hh[i], a_t - aa[i]
    else:
        # bracket on the rising side: last point below a_t before the peak
        below = np.where(aa[:i + 1] < a_t)[0]
        j = below[-1] if len(below) else 0
        lo, hi = hh[j], hh[min(j + 1, i)]
        g = lambda H: 2 * core(H) / H - core_d(H) - a_t
        for _ in range(40):                # bisection, 1e-12 relative
            mid = np.sqrt(lo * hi)
            if g(mid) < 0:
                lo = mid
            else:
                hi = mid
        H2, short = np.sqrt(lo * hi), 0.0
    f2, s2 = core(H2), core_d(H2)
    return 2 * f2 / H2 - s2, (s2 * H2 - f2) / H2**2, H2, short


def shape_splicemu(par, ci, c, d, h, js):
    """As shape_splice, with the toe parameterized by mu_1 (see above)."""
    core, _core_d, _ = splice_core(par, ci, c, d, js)
    a, b, H2, _short = splice_join_mu(par, ci, c, d, js)
    h = np.asarray(h, dtype=float)
    return np.where(h < H2, a * h + b * h**2, core(np.maximum(h, 1e-30)))


def splice_join_window(par, ci, c, d, js):
    """The join at a fixed position inside the window where a C1 Rayleigh
    toe exists.

    a(H) = 2 core(H)/H - core'(H) is zero where the model's local log-log
    slope is 2 and largest where it is 1.  The join is placed a fraction
    q = sigmoid(lqw[regime]) of the way, in ln H, from the slope-2
    crossing to the slope-1 crossing, so it is inside the window for every
    grade by construction and mu_1 is that grade's own fraction of the
    largest value the wall term can join to.  Returns (a, b, H2).
    """
    core, core_d, _ = splice_core(par, ci, c, d, js)
    q = _sig(par["lqw"][regime(d)])
    hh = np.logspace(0.5, 4.5, 161)
    sl = hh * core_d(hh) / core(hh)             # local d ln J / d ln H
    i = int(np.argmin(np.abs(sl - 1.0)))         # near the slope-1 point
    # last crossing of slope 2 before the slope-1 point
    above = np.where(sl[:i] > 2.0)[0]
    j = above[-1] if len(above) else 0
    lo = np.exp(np.interp(2.0, [sl[j + 1], sl[j]], np.log([hh[j + 1], hh[j]]))) if j + 1 < len(hh) else hh[j]
    hi = np.exp(np.interp(1.0, [sl[i + 1], sl[i]], np.log([hh[i + 1], hh[i]]))) if sl[i] > 1.0 and i + 1 < len(hh) else hh[i]
    H2 = np.exp(np.log(lo) + q * (np.log(hi) - np.log(lo)))
    f2, s2 = core(H2), core_d(H2)
    return 2 * f2 / H2 - s2, (s2 * H2 - f2) / H2**2, H2


def shape_splicew(par, ci, c, d, h, js):
    """As shape_splice, with the join placed inside its window (above)."""
    core, _core_d, _ = splice_core(par, ci, c, d, js)
    a, b, H2 = splice_join_window(par, ci, c, d, js)
    h = np.asarray(h, dtype=float)
    return np.where(h < H2, a * h + b * h**2, core(np.maximum(h, 1e-30)))


def shape_splice(par, ci, c, d, h, js):
    """Rayleigh toe below H2, wall + rotation above, C1 at the join."""
    core, _core_d, H2 = splice_core(par, ci, c, d, js)
    a, b, _ = splice_rayleigh(par, ci, c, d, js)
    h = np.asarray(h, dtype=float)
    return np.where(h < H2, a * h + b * h**2, core(np.maximum(h, 1e-30)))


SHAPES = {"v10": shape_v10, "v11": shape_v11, "fit": shape_fit,
          "tied": shape_tied, "splice": shape_splice,
          "splicemu": shape_splicemu, "splicew": shape_splicew,
          "rb": shape_rb, "two": shape_two}


class Family:
    """A fitted family, loaded from its parameter JSON."""

    def __init__(self, path):
        P = json.load(open(path))
        self.P = P
        self.version = P.get("model", "v10")
        self.par = {k: np.array(v, float) for k, v in P["params"].items()}
        # Superseded, and 0 in anything fitted after 2026-09-25: the
        # emitted level is the composition law alone.  Read with a
        # default so the archived runs, whose value is meaningful,
        # still reproduce.
        self.djsbar = P.get("djsbar", 0.0)
        self.conds = P["conds"]
        self.ci = {d: i for i, d in enumerate(self.conds)}
        self._shape = SHAPES[self.version]
        # The two-population toe form is a recorded variant, not a model
        # tag; anything fitted before it was recorded used the rational one.
        if (self.version == "two"
                and P.get("variant", {}).get("TOE", "rational") == "straight"):
            self._shape = shape_two_straight

    def shape(self, c, d, h, mn=None):
        """Normalized shape.  mn is required for the "fit" model, whose
        rotation scale depends on Js(C,Mn); the older forms ignore it."""
        h = np.asarray(h, float)
        if self.version not in ("v10", "v11") and mn is None:
            raise ValueError("shape() needs mn: hR depends on Js(C,Mn)")
        js = None if mn is None else L.js_anchor(c, mn)
        return self._shape(self.par, self.ci, c, d, h, js)

    def j(self, c, mn, d, h):
        return (L.js_anchor(c, mn) + self.djsbar) * self.shape(c, d, h, mn)

    def b(self, c, mn, d, h):
        h = np.asarray(h, float)
        return self.j(c, mn, d, h) + L.MU0 * h

    def join(self, c, mn, d):
        """(a, b, H2) of the spliced Rayleigh toe: J/Js = a h + b h^2 below
        H2.  mu_1 = a Js/mu0.  Only for the spliced models."""
        if self.version != "splice":
            raise ValueError("join() is for the spliced model")
        return splice_rayleigh(self.par, self.ci, c, d, L.js_anchor(c, mn))

    def tail(self, c, d):
        """Wall-term tail exponent kW*mW."""
        j, x = self.ci[d], c / 0.77
        if self.version == "v10":
            return float(np.exp(self.par["lkW"][j] + self.par["bK"][j] * x)
                         * np.exp(self.par["lmW"][j] + self.par["bM"][j] * x))
        lt = self.par["ltail"]
        return float(np.exp(lt[_reg(self.par, d)] if len(lt) > 1 else lt[0]))
