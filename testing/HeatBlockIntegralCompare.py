#!/usr/bin/env python3
"""
HeatBlockIntegralCompare.py -- diff two HeatBlockIntegralTest runs.

Compares the printed output of the heat-flow block-integral test
(HeatBlockIntegralTest.py on femm-qt vs HeatBlockIntegralTest.lua on Original
FEMM) and flags meaningful disagreements. Both output formats are auto-detected
(OF prefixes lines with "-->").

Each heat block integral reports a single (re, im) pair -- a scalar (average
temperature, area, volume; im ~ 0) or a 2-vector (average F/heat flux, average
G/gradient; re/im = x/y, or r/z). As with the electrostatics comparator, the
metric is just the complex-magnitude relative difference (which normalizes a
near-zero component of a vector by the whole vector's magnitude).

Usage:
    # capture a femm-qt run:
    femm_headless -run HeatBlockIntegralTest.py | grep -E '={5,}|^  \\[' > fq_heat_result.txt
    # compare against an OF reference (reference first, test second):
    python3 HeatBlockIntegralCompare.py OF_heat_result.txt fq_heat_result.txt

Exit status is nonzero if anything is flagged.
"""
import re
import sys

TOL   = 0.001    # flag if relative error exceeds this (0.1%)
FLOOR = 1e-12    # below this magnitude the quantity is ~0; skip


def parse(path):
    """Return (section_titles, {(section, n): complex}); auto-detects OF/femm-qt."""
    secs, vals, s = [], {}, -1
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.replace("-->", "").strip()
        if t.startswith("====="):
            s += 1
            secs.append(t.strip("= ").strip())
            continue
        m = re.match(r"\[(\d+)\]\s+.*?:\s+re=(\S+)\s+im=(\S+)", t)
        if m:
            vals[(s, int(m.group(1)))] = complex(float(m.group(2)), float(m.group(3)))
    return secs, vals


def _fmt(z):
    if abs(z.imag) < 1e-12 * max(abs(z.real), 1.0):
        return "%.6g" % z.real
    return "%.6g%+.4gj" % (z.real, z.imag)


def compare(ref_path, test_path, tol=TOL, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    ok, flags = 0, []
    for k in sorted(rv):
        if k not in tv:
            continue
        a, b = rv[k], tv[k]
        sc = abs(a)
        ne = abs(b - a) / sc if sc > floor else 0.0
        if sc <= floor or ne <= tol:
            ok += 1
        else:
            flags.append((k, a, b, ne))

    print("compared %d integral values: %d agree (rel err <= %.2f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for (s, n), a, b, ne in sorted(flags, key=lambda x: -x[3]):
            print("  [%-30s] #%d  ref=%-14s test=%-14s rel=%.3f%%"
                  % (rsec[s][:30], n, _fmt(a), _fmt(b), ne * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_heat_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_heat_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
