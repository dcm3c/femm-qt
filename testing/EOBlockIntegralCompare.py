#!/usr/bin/env python3
"""
EOBlockIntegralCompare.py -- diff two ElecBlockIntegralTest runs.

Compares the printed output of the electrostatics block-integral test
(ElecBlockIntegralTest.py on femm-qt vs ElecBlockIntegralTest.lua on Original
FEMM) and flags meaningful disagreements. Both output formats are auto-detected
(OF prefixes lines with "-->").

Each electrostatics block integral reports a single (re, im) pair -- a scalar
(energy, area, volume, torque; im ~ 0) or a 2-vector (average D, average E,
force; re/im = x/y, or r/z in the axisymmetric case). So, unlike the magnetics
comparator, no cross-integral force pairing is needed: the error is just the
complex-magnitude relative difference, which handles a near-zero component of a
vector correctly (it's normalized by the whole vector's magnitude).

Usage:
    # capture a femm-qt run:
    femm_headless -run ElecBlockIntegralTest.py | grep -E '={5,}|^  \\[' > fq_elec_result.txt
    # compare against an OF reference (reference first, test second):
    python3 EOBlockIntegralCompare.py OF_elec_result.txt fq_elec_result.txt

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
            print("  [%-34s] #%d  ref=%-14s test=%-14s rel=%.3f%%"
                  % (rsec[s][:34], n, _fmt(a), _fmt(b), ne * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_elec_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_elec_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
