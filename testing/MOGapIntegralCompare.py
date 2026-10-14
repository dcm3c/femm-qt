#!/usr/bin/env python3
"""
MOGapIntegralCompare.py -- diff two MOGapIntegralTest runs.

Compares the printed output of the air-gap-element test (MOGapIntegralTest.py on
femm-qt vs MOGapIntegralTest.lua on Original FEMM). Both runs emit the same
format, one line per quantity, keyed by label:

    <label> = re im | re im | ...

Normalization is class-aware, because the two fixtures each exercise only one of
force / torque (the radial bearing has ~0 torque; the balanced 360-degree motor
has ~0 net force), and a machine's even harmonics are ~0 by symmetry:
  * FORCE (force/force2x/forceinc) and TORQUE (torque/...): each normalized by the
    dominant magnitude of that class across the WHOLE test, so the fixture that
    lacks one has its near-zero residual floored by the other fixture's real value.
  * HARM (harm1..4) and GAPB (gapB@*): normalized by that class's dominant within
    the section, so the fundamental floors the near-zero even harmonics and a
    gap-B zero-crossing is floored by the gap-B peak.
  * energy / nharm / gapA: plain relative error.
2% tol -- gap integrals come off the meshed gap boundary and are mesh-sensitive.

Usage:
    femm_headless -run MOGapIntegralTest.py > fq_gap_result.txt
    python3 MOGapIntegralCompare.py OF_gap_result.txt fq_gap_result.txt
"""
import math
import re
import sys

import of_ac_curve

TOL   = 0.02
FLOOR = 1e-9


def pf(s):
    try:
        return float(s)
    except ValueError:
        sl = s.lower()
        if "inf" in sl:
            return float("-inf") if s.lstrip().startswith("-") else float("inf")
        return float("nan")


def parse(path):
    """Return (section_titles, {(section, label): [complex, ...]})."""
    secs, vals, s = [], {}, -1
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.replace("-->", "").strip()
        if t.startswith("==="):
            s += 1
            secs.append(t.strip("= ").strip())
            continue
        m = re.match(r"(.+?)\s*=\s*(.+)$", t)
        if not m:
            continue
        label = m.group(1).strip()
        comps = []
        for piece in m.group(2).split("|"):
            nums = piece.split()
            if len(nums) >= 2:
                comps.append(complex(pf(nums[0]), pf(nums[1])))
        if comps:
            vals[(s, label)] = comps
    return secs, vals


def _fmt(z):
    if abs(z.imag) < 1e-9 * max(abs(z.real), 1.0):
        return "%.6g" % z.real
    return "%.6g%+.4gj" % (z.real, z.imag)


def _class(label):
    if "torque" in label:                 return "TORQUE"
    if "force" in label:                  return "FORCE"
    if label.startswith("harm"):          return "HARM"
    if label.startswith("gapB"):          return "GAPB"
    return None   # energy / nharm / gapA -> plain relative error


def compare(ref_path, test_path, tol=TOL, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    def mag(pred):
        return max((abs(c) for (s, label), comps in rv.items() if pred(s, label)
                    for c in comps), default=0.0)

    # FORCE/TORQUE: dominant across the whole test; HARM/GAPB: per section.
    gdom = {"FORCE":  mag(lambda s, l: _class(l) == "FORCE"),
            "TORQUE": mag(lambda s, l: _class(l) == "TORQUE")}
    sdom = {}
    for cls in ("HARM", "GAPB"):
        for s in range(len(rsec)):
            sdom[(s, cls)] = mag(lambda ss, l, s=s, cls=cls: ss == s and _class(l) == cls)

    ok, flags, ac_cut = 0, [], 0
    for k in sorted(rv, key=lambda x: (x[0], str(x[1]))):
        if k not in tv:
            continue
        s, label = k
        rc, tc = rv[k], tv[k]
        if of_ac_curve.excluded(rsec[s]):
            ac_cut += min(len(rc), len(tc))
            continue
        cls = _class(label)
        for j in range(min(len(rc), len(tc))):
            if not (math.isfinite(rc[j].real) and math.isfinite(rc[j].imag)):
                continue
            if cls in ("FORCE", "TORQUE"):
                denom = gdom[cls]
            elif cls in ("HARM", "GAPB"):
                denom = sdom[(s, cls)]
            else:
                denom = abs(rc[j])   # energy / nharm / gapA: relative
            if denom <= floor:
                ok += 1
                continue
            err = abs(tc[j] - rc[j]) / denom
            if err <= tol:
                ok += 1
            else:
                flags.append((s, label, j, rc[j], tc[j], err))

    print("compared %d gap-integral components: %d agree (err <= %.1f%%), %d flagged%s"
          % (ok + len(flags), ok, tol * 100, len(flags), of_ac_curve.note(ac_cut)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for s, label, j, a, b, err in sorted(flags, key=lambda x: -x[5]):
            print("  [%-28s] %-10s comp %d  ref=%-13s test=%-13s err=%.1f%%"
                  % (rsec[s][:28], label, j, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_gap_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_gap_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
