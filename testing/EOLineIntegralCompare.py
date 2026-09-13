#!/usr/bin/env python3
"""
EOLineIntegralCompare.py -- diff two EOLineIntegralTest runs.

Compares the printed output of the eo_lineintegral test (EOLineIntegralTest.py on
femm-qt vs EOLineIntegralTest.lua on Original FEMM). Both runs emit the same
format, one line per integral:

    [<type>] <label> = re im | re im       (electrostatics is static -> im == 0)

Normalization (eo_lineintegral-specific):
  * FORCE (type 3) and TORQUE (type 4): each component normalized by the dominant
    (largest-magnitude) component of that same integral, so a near-zero cross-axis
    force isn't judged against itself.
  * Everything else (D.n, E.t, contour length/area) is a plain relative error.

As with the magnetics line suite, a single-contour stress-tensor line integral is
more mesh-sensitive than the WST block integral, so a looser ~2% tol is used.

Usage:
    femm_headless -run EOLineIntegralTest.py > fq_eline_result.txt
    python3 EOLineIntegralCompare.py OF_eline_result.txt fq_eline_result.txt
"""
import re
import sys

FORCE  = {3}
TORQUE = {4}

TOL   = 0.02
FLOOR = 1e-12    # electrostatic charges/forces are tiny in SI; use a small floor


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
        dom = max((abs(c) for c in rc), default=0.0) if n in FORCE or n in TORQUE else None
        for j in range(min(len(rc), len(tc))):
            denom = dom if dom is not None else abs(rc[j])
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
            print("  [%-30s] type %d comp %d  ref=%-14s test=%-14s err=%.1f%%"
                  % (rsec[s][:30], n, j, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_eline_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_eline_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
