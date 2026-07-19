#!/usr/bin/env python3
"""
HILineIntegralCompare.py -- diff two HILineIntegralTest runs.

Compares the printed output of the ho_lineintegral test (HILineIntegralTest.py on
femm-qt vs HILineIntegralTest.lua on Original FEMM). Both runs emit the same
format, one line per integral:

    [<type>] <label> = re im | re im       (heat is static -> im == 0)

Heat flow has no stress-tensor force/torque line integral, so every type
(0 G.t, 1 F.n, 2 length/area, 3 avg T) is a scalar compared by relative error,
with a looser ~2% tol (a single-contour line integral is more mesh-sensitive
than a block integral).

The axisymmetric avg-T (type 3) on the r=0 axis is a 2*pi*r-weighted 0/0: OF
returns NaN, femm-qt returns the plain line average. The parser tolerates the
Windows NaN spellings and the comparison skips any non-finite reference.

Usage:
    femm_headless -run HILineIntegralTest.py > fq_hline_result.txt
    python3 HILineIntegralCompare.py OF_hline_result.txt fq_hline_result.txt
"""
import math
import re
import sys

TOL   = 0.02
FLOOR = 1e-9


def pf(s):
    # Tolerate Windows non-finite spellings (1.#QNAN, -1.#IND, 1.#INF).
    try:
        return float(s)
    except ValueError:
        sl = s.lower()
        if "inf" in sl:
            return float("-inf") if s.lstrip().startswith("-") else float("inf")
        return float("nan")


def parse(path):
    secs, vals, s = [], {}, -1
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.replace("-->", "").strip()   # OF echoes each console line with a --> prefix
        if t.startswith("==="):
            s += 1
            secs.append(t.strip("= ").strip())
            continue
        m = re.match(r"\[(\d+)\]\s+.*?=\s+(.+)", t)
        if not m:
            continue
        comps = []
        for piece in m.group(2).split("|"):
            nums = piece.split()
            if len(nums) >= 2:
                comps.append(complex(pf(nums[0]), pf(nums[1])))
        vals[(s, int(m.group(1)))] = comps
    return secs, vals


def _fmt(z):
    if abs(z.imag) < 1e-9 * max(abs(z.real), 1.0):
        return "%.6g" % z.real
    return "%.6g%+.4gj" % (z.real, z.imag)


def compare(ref_path, test_path, tol=TOL, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    ok, flags = 0, []
    for k in sorted(rv):
        if k not in tv:
            continue
        s, n = k
        rc, tc = rv[k], tv[k]
        for j in range(min(len(rc), len(tc))):
            if not (math.isfinite(rc[j].real) and math.isfinite(rc[j].imag)):
                continue  # reference is a degenerate NaN/inf (axis avg-T); skip
            denom = abs(rc[j])
            if denom <= floor:
                ok += 1
                continue
            err = abs(tc[j] - rc[j]) / denom
            if err <= tol:
                ok += 1
            else:
                flags.append((k, j, rc[j], tc[j], err))

    print("compared %d integral components: %d agree (err <= %.1f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for (s, n), j, a, b, err in sorted(flags, key=lambda x: -x[4]):
            print("  [%-26s] type %d comp %d  ref=%-14s test=%-14s err=%.1f%%"
                  % (rsec[s][:26], n, j, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_hline_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_hline_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
