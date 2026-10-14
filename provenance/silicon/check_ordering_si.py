# SPDX-License-Identifier: MIT
"""check_ordering_si.py -- the cross-curve gate for the silicon family.

The three per-curve gates (B monotone, dB/dH >= mu0, one mu peak) pass on
curves that are individually fine but collectively absurd.  For the
low-carbon family check_ordering.py caught exactly that: a 1400 F anneal
coming out HARDER than as-hot-rolled, over a whole band of induction.  The
silicon family needs the same check, but its ordering rule cannot be
written down a priori, because silicon does two opposing things -- it
softens the knee (hW falls 24% per wt%) and it lowers saturation (Js falls
0.048 T per wt%).  Two grades of different silicon MUST cross, once, so a
gate that forbade crossing would be wrong.

What CAN be checked is that the model orders the grades the way the PAGES
order them: for every pair and every induction, the sign of H_a - H_b in
the model against its sign in the digitized curves.

THREE QUALIFICATIONS, none of them a free knob, each added because without
it the gate measured something other than what it claims to.

1. DEADBAND.  Two grades whose pages differ by less than the fit's own
   accuracy are not ordered by the source at all, and demanding the model
   reproduce that order is demanding it reproduce noise.  M-19 and M-22 are
   the clearest case: their pages agree to 8% in H everywhere and cross
   each other FOUR times, so "which is softer" is not a fact the book
   records.  DEADBAND = 10%, about 1.5x the fitted RMS lnH of 0.069.
   Without it the gate reported 40 reorderings, every one a crossing read
   to a resolution the source does not have.

2. LEVEL-DOMINATED REGIONS ARE SKIPPED.  Near saturation dJ/dlnH collapses,
   so a level error of one per cent becomes a field error of tens of per
   cent and the ordering compares LEVELS, not shapes.  The emitted levels
   are deliberately NOT the pages' levels -- see SiFamily.js -- so testing
   the model against the pages up there asks it to reproduce the very thing
   it was built to override.  Where a one-sigma level error (SIG_LEVEL,
   the spread in how far the ten pages run low) would move either curve by
   more than the deadband, the point is skipped.  Levels are gated instead
   directly and against composition, in part 3.  Without this the gate
   reported 53 reorderings, all above 1.3 T and all involving the two
   pages whose levels were overridden.

3. A REORDERING MUST PERSIST.  A real one occupies a band -- the v10 defect
   ran from 1.5 T to the top of the data.  A single isolated induction is a
   crossing that landed between grid points, and at a separation barely
   over the deadband it is inside the model's stated accuracy anyway.  Runs
   of RUNMIN or more consecutive inductions fail the gate; isolated points
   are printed as marginal and do not.  One such point exists and is
   expected: M-22 against Q-Core at 0.35 T, where the pages are 10.0%
   apart -- 1.4x the fit RMS, and exactly on the deadband.

The alloy pairs are reported as a measurement rather than gated: how far
apart the book puts two grades it made from ONE alloy is the size of the
processing axis the book never describes.

Exits 1 on any persistent reordering or any level inversion.

Usage:  python check_ordering_si.py [out/si_params.json]
"""
import sys
import numpy as np

import bhlib as L
import silib as S
import simodel as M

SRC = sys.argv[1] if len(sys.argv) > 1 else "out/si_params.json"
F = M.SiFamily(SRC)
rows = S.table()
BG = np.linspace(0.3, 1.9, 33)          # induction grid, tesla
PAIRS = [("M19", "M22"), ("M27", "M36"), ("M47", "StatorB"),
         ("QCore_018in", "Type2S_018in"), ("QCore_024in", "Type2S_024in")]
# same grade, different gauge or processing: the same composition again, so
# the page difference is gauge (first three) or the 1450 F anneal (last)
GAUGE_PAIRS = [("QCore_018in", "QCore_024in"),
               ("Type2S_018in", "Type2S_024in"),
               ("StatorB", "StatorB_24FP"),
               ("StatorB_24FP", "StatorB_24SP")]
DEADBAND = 0.10         # in H; about 1.5x the fit's RMS lnH of 0.069
SIG_LEVEL = 0.015       # sd of how far the ten pages run low at the top
RUNMIN = 2              # consecutive inductions before a flip is a defect
HG = np.logspace(0.5, np.log10(4e5), 4000)


def _on_bg(b, y):
    out = np.interp(BG, b, y, left=np.nan, right=np.nan)
    out[(BG < b[0]) | (BG > b[-1])] = np.nan
    return out


def level_shift(key):
    """ln-H by which a one-sigma level error moves this grade, per induction."""
    b = F.b(key, HG)
    j = b - L.MU0 * HG
    slope = np.gradient(j, np.log(HG))
    # a level error eps scales J everywhere, so the field shift it costs is
    # eps * J / (dJ/dlnH), not eps * Js / (dJ/dlnH)
    return _on_bg(b, SIG_LEVEL * j / np.maximum(slope, 1e-9))


def h_of_b(key, model):
    """H at each induction of BG; nan outside the curve's own range."""
    if model:
        return _on_bg(F.b(key, HG), HG)
    hg, b, _ = S.load_raw(key)
    return _on_bg(b, hg)


def runs(mask):
    """Lengths of the True runs in a boolean array."""
    out, n = [], 0
    for v in mask:
        n = n + 1 if v else 0
        if n:
            if n == 1:
                out.append(1)
            else:
                out[-1] = n
    return out


print("source: %s" % SRC)
hm = {r["key"]: h_of_b(r["key"], True) for r in rows}
hp = {r["key"]: h_of_b(r["key"], False) for r in rows}
sh = {r["key"]: level_shift(r["key"]) for r in rows}
# a grade printed at more than one gauge is labelled with its gauge
_n = [r["name"] for r in rows]
name = {r["key"]: (r["name"] if _n.count(r["name"]) == 1
                   else "%s %.0fmil" % (r["name"], r["mils"])) for r in rows}

# ---- 1. does the model order the grades the way the pages do? ------------
bad = marg = 0
ceil = [BG[np.isfinite(sh[r["key"]]) & (sh[r["key"]] < DEADBAND)].max()
        for r in rows]
print("\nordering vs the pages, %d inductions from %.1f to %.1f T.  Counted "
      "only where the\npages separate the pair by more than %.0f%% in H and "
      "neither curve is level-\ndominated (that ceiling is %.2f-%.2f T by "
      "grade); %d+ consecutive = a defect."
      % (len(BG), BG[0], BG[-1], 100 * DEADBAND, min(ceil), max(ceil),
         RUNMIN))
for i, a in enumerate(rows):
    for b in rows[i + 1:]:
        ka, kb = a["key"], b["key"]
        ok = (np.isfinite(hp[ka]) & np.isfinite(hp[kb])
              & np.isfinite(hm[ka]) & np.isfinite(hm[kb]))
        if not ok.any():
            continue
        flip = np.sign(hm[ka] - hm[kb]) != np.sign(hp[ka] - hp[kb])
        flip &= np.abs(hp[ka] / hp[kb] - 1) > DEADBAND
        flip &= (sh[ka] + sh[kb]) < DEADBAND
        flip &= ok
        rr = runs(flip)
        if not rr:
            continue
        bb = BG[flip]
        if max(rr) >= RUNMIN:
            bad += sum(x for x in rr if x >= RUNMIN)
            print("  %-9s vs %-9s: reordered over %.2f-%.2f T, longest run "
                  "%d   <== DEFECT"
                  % (name[ka], name[kb], bb.min(), bb.max(), max(rr)))
        else:
            marg += len(rr)
            print("  %-9s vs %-9s: %d isolated point(s) at %.2f-%.2f T "
                  "(pages %.1f%% apart) -- marginal"
                  % (name[ka], name[kb], len(rr), bb.min(), bb.max(),
                     100 * np.abs(hp[ka] / hp[kb] - 1)[flip].max()))
print("  %d persistent reordering(s), %d marginal point(s)" % (bad, marg))

# ---- 2. the processing axis, reported not gated --------------------------
print("\nalloy pairs -- one alloy, two grades, so this is processing alone:")
for ka, kb in PAIRS + [(None, None)] + GAUGE_PAIRS:
    if ka is None:
        print("gauge and anneal pairs -- one grade, so this is gauge or the "
              "anneal alone:")
        continue
    ok = (np.isfinite(hm[ka]) & np.isfinite(hm[kb])
          & np.isfinite(hp[ka]) & np.isfinite(hp[kb]))
    sp, sm = np.abs(hp[ka] / hp[kb] - 1)[ok], np.abs(hm[ka] / hm[kb] - 1)[ok]
    print("  %-9s / %-9s: pages %4.1f%% median %5.1f%% max;  "
          "model %4.1f%% / %5.1f%%"
          % (name[ka], name[kb], 100 * np.median(sp), 100 * sp.max(),
             100 * np.median(sm), 100 * sm.max()))

# ---- 3. saturation falls with silicon ------------------------------------
print("\nsaturation vs silicon:")
order = sorted(rows, key=lambda r: r["sieq"])
bsat = [float(F.b(r["key"], 4e5)) for r in order]
nlev = 0
for i in range(len(order)):
    for j in range(i + 1, len(order)):
        if (order[j]["sieq"] > order[i]["sieq"] + 1e-9
                and bsat[j] > bsat[i] + 1e-9):
            nlev += 1
            print("  %s (%.2f%%) saturates ABOVE %s (%.2f%%)   <== INVERSION"
                  % (order[j]["name"], order[j]["sieq"],
                     order[i]["name"], order[i]["sieq"]))
for r, bs in zip(order, bsat):
    print("  %-9s Si-eq %.2f   B(4e5) = %.4f T   (its page reached %.4f T)"
          % (r["name"], r["sieq"], bs, F.js_fit(r["key"])))
print("  falls with silicon, ties between alloy twins allowed: %s"
      % ("NO" if nlev else "yes"))

fail = bad + nlev
print("\n%s" % ("PASS" if fail == 0 else "FAIL (%d)" % fail))
sys.exit(1 if fail else 0)
