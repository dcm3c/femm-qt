#!/usr/bin/env python3
"""
MOPointValueCompare.py -- diff two MOPointValueTest runs.

Compares mo_getpointvalues output (MOPointValueTest.py on femm-qt vs
MOPointValueTest.lua on Original FEMM). Each line is a point, keyed by a
material-type label, with the 14 values as "re im" pairs:

    <point> = A | B1 | B2 | sig | E | H1 | H2 | Je | Js | mu1 | mu2 | Pe | Ph | ff

Normalization:
  * B1,B2 by the local |B| and H1,H2 by the local |H| at that point, so a
    component is judged against the field strength there -- every point (weak or
    strong field) is validated to the same relative precision.
  * every other value by its dominant magnitude across all sampled points, so a
    value that is legitimately 0 at a point (Js off a coil, Je/Pe/Ph at DC) is
    floored by the point where it is real.

Point values are local (no averaging), so they are the most mesh-sensitive query:
a looser tol is expected, especially at saturated-iron points where a small B
difference is amplified through the nonlinear permeability. Default 3%.

Usage:
    femm_headless -run MOPointValueTest.py > fq_pv_result.txt
    python3 MOPointValueCompare.py OF_pv_result.txt fq_pv_result.txt [tol]
"""
import math
import re
import sys

TOL   = 0.03
FLOOR = 1e-9
NAMES = ["A", "B1", "B2", "sig", "E", "H1", "H2", "Je", "Js",
         "mu1", "mu2", "Pe", "Ph", "ff"]


def parse(path):
    secs, vals, s = [], {}, -1
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.replace("-->", "").strip()
        if t.startswith("==="):
            s += 1
            secs.append(t.strip("= ").strip())
            continue
        m = re.match(r"(.+?)\s+=\s+(.+)$", t)   # spaced " = " (labels like Bdes=0.1 embed '=')
        if not m or "no mesh" in m.group(2):
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
        return "%.5g" % z.real
    return "%.5g%+.3gj" % (z.real, z.imag)


def compare(ref_path, test_path, tol=TOL, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    n = max((len(c) for c in rv.values()), default=0)
    gmax = [max((abs(c[j]) for c in rv.values() if len(c) > j), default=0.0)
            for j in range(n)]

    ok, flags = 0, []
    for k in sorted(rv, key=lambda x: (x[0], str(x[1]))):
        if k not in tv:
            continue
        s, pt = k
        rc, tc = rv[k], tv[k]
        for j in range(min(len(rc), len(tc))):
            if j in (1, 2):
                denom = math.hypot(abs(rc[1]), abs(rc[2]))   # local |B|
            elif j in (5, 6):
                denom = math.hypot(abs(rc[5]), abs(rc[6]))   # local |H|
            else:
                denom = gmax[j]
            if denom <= floor:
                ok += 1
                continue
            err = abs(tc[j] - rc[j]) / denom
            if err <= tol:
                ok += 1
            else:
                flags.append((s, pt, j, rc[j], tc[j], err))

    print("compared %d point values: %d agree (err <= %.1f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for s, pt, j, a, b, err in sorted(flags, key=lambda x: -x[5]):
            nm = NAMES[j] if j < len(NAMES) else str(j)
            print("  [%-26s] %-13s %-4s ref=%-13s test=%-13s err=%.1f%%"
                  % (rsec[s][:26], pt, nm, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_pv_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_pv_result.txt"
    tol  = float(sys.argv[3]) if len(sys.argv) > 3 else TOL
    sys.exit(1 if compare(ref, test, tol) else 0)
