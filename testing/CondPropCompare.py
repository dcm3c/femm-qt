#!/usr/bin/env python3
"""
CondPropCompare.py -- diff two conductor-property test runs (shared by the
electrostatics / current-flow / heat suites, EOCondPropTest / COCondPropTest /
HOCondPropTest). Both runs emit one line per conductor, keyed by name:

    <conductor> = re im | re im     (V | q)  where q is charge / current / heat flow

Each of the two components is normalized by the dominant magnitude of that
component among the section's conductors, so a grounded electrode's zero voltage
or a floating conductor's near-zero charge is floored by the driven conductors
rather than judged against itself. 2% tol.

Usage:
    femm_headless -run EOCondPropTest.py > fq_eic_result.txt
    python3 CondPropCompare.py OF_eic_result.txt fq_eic_result.txt
"""
import math
import re
import sys

TOL   = 0.02
FLOOR = 1e-12


def parse(path):
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
        comps = []
        for piece in m.group(2).split("|"):
            nums = piece.split()
            if len(nums) >= 2:
                comps.append(complex(float(nums[0]), float(nums[1])))
        if comps:
            vals[(s, m.group(1).strip())] = comps
    return secs, vals


def _fmt(z):
    if abs(z.imag) < 1e-9 * max(abs(z.real), 1.0):
        return "%.6g" % z.real
    return "%.6g%+.4gj" % (z.real, z.imag)


def compare(ref_path, test_path, tol=TOL, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    def secdom(s, j):
        return max((abs(c[j]) for (ss, _), c in rv.items() if ss == s and len(c) > j),
                   default=0.0)
    dom = {(s, j): secdom(s, j) for s in range(len(rsec)) for j in (0, 1)}

    ok, flags = 0, []
    for k in sorted(rv, key=lambda x: (x[0], str(x[1]))):
        if k not in tv:
            continue
        s, cname = k
        rc, tc = rv[k], tv[k]
        for j in range(min(len(rc), len(tc))):
            denom = dom.get((s, j), 0.0)
            if denom <= floor:
                ok += 1
                continue
            err = abs(tc[j] - rc[j]) / denom
            if err <= tol:
                ok += 1
            else:
                flags.append((s, cname, j, rc[j], tc[j], err))

    label = {0: "V", 1: "q"}
    print("compared %d conductor-property components: %d agree (err <= %.1f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for s, cname, j, a, b, err in sorted(flags, key=lambda x: -x[5]):
            print("  [%-26s] %-12s %-2s ref=%-14s test=%-14s err=%.1f%%"
                  % (rsec[s][:26], cname, label.get(j, j), _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_cond_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_cond_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
