#!/usr/bin/env python3
"""
MOCircuitPropCompare.py -- diff two MOCircuitPropTest runs.

Compares the printed output of the mo_getcircuitproperties test (MOCircuitPropTest
.py on femm-qt vs MOCircuitPropTest.lua on Original FEMM). Both runs emit one line
per circuit, keyed by circuit name:

    <circuit> = re im | re im | re im        (current | voltage drop | flux linkage)

Per component:
  * current (comp 0): the prescribed excitation -- plain relative error.
  * voltage (comp 1) and flux linkage (comp 2): normalized by the dominant V / Flux
    among that section's circuits, so an unexcited circuit's near-zero V/Flux is
    floored by the driven circuits rather than judged against itself.
2% tol.

Usage:
    femm_headless -run MOCircuitPropTest.py > fq_circ_result.txt
    python3 MOCircuitPropCompare.py OF_circ_result.txt fq_circ_result.txt
"""
import math
import re
import sys

import of_ac_curve

TOL   = 0.02
FLOOR = 1e-9


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
                # A host's own banner can match the key = value shape above:
                # Scilab on Linux prints a JVM class-data-sharing warning
                # holding (value = "org.scilab...ScilabClassLoader") on
                # STDOUT, which lands in a captured binding run. Drop a line
                # whose "numbers" are not numbers rather than dying on it --
                # the values around it are good. Same guard as
                # PhaseCompare.parse, which is why that comparer alone
                # survived the Scilab runs.
                try:
                    comps.append(complex(float(nums[0]), float(nums[1])))
                except ValueError:
                    comps = []
                    break
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

    # per-section dominant voltage (comp 1) and flux (comp 2)
    def secdom(s, j):
        return max((abs(c[j]) for (ss, _), c in rv.items() if ss == s and len(c) > j),
                   default=0.0)
    vdom = {s: secdom(s, 1) for s in range(len(rsec))}
    fdom = {s: secdom(s, 2) for s in range(len(rsec))}

    ok, flags, ac_cut = 0, [], 0
    for k in sorted(rv, key=lambda x: (x[0], str(x[1]))):
        if k not in tv:
            continue
        s, cname = k
        rc, tc = rv[k], tv[k]
        if of_ac_curve.excluded(rsec[s]):
            ac_cut += min(len(rc), len(tc))
            continue
        for j in range(min(len(rc), len(tc))):
            denom = abs(rc[j]) if j == 0 else (vdom[s] if j == 1 else fdom[s])
            if denom <= floor:
                ok += 1
                continue
            err = abs(tc[j] - rc[j]) / denom
            if err <= tol:
                ok += 1
            else:
                flags.append((s, cname, j, rc[j], tc[j], err))

    label = {0: "I", 1: "V", 2: "Flux"}
    print("compared %d circuit-property components: %d agree (err <= %.1f%%), %d flagged%s"
          % (ok + len(flags), ok, tol * 100, len(flags), of_ac_curve.note(ac_cut)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for s, cname, j, a, b, err in sorted(flags, key=lambda x: -x[5]):
            print("  [%-34s] %-12s %-4s ref=%-13s test=%-13s err=%.1f%%"
                  % (rsec[s][:34], cname, label.get(j, j), _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_circ_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_circ_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
