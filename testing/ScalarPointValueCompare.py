#!/usr/bin/env python3
"""
ScalarPointValueCompare.py -- diff two scalar-physics point-value runs (shared by
EO/CO/HOPointValueTest). Each line is a point, keyed by a label, with the physics'
point values as "re im" pairs:

    eo: V | Dx | Dy | Ex | Ey | epsx | epsy | energy
    co: V | Jx | Jy | Kx | Ky | Ex | Ey | epsx | epsy | Jdx | Jdy | sigx | sigy | Jcx | Jcy
    ho: T | Fx | Fy | Gx | Gy | kx | ky

col 0 is the scalar potential (V / T), compared by relative error. Every other
value is an (x,y) vector-field component (D,E for eo; J,K,E,eps,Jd,sig,Jc for co;
F,G,kappa for ho), so each is normalized by the magnitude of its pair at that
point -- a transverse component (e.g. a 1e3 Jx riding on a 2e7 Jy) is judged
against the field there, not against itself. 2% tol -- point values are local and
mesh-sensitive (scalar-physics fields are piecewise-constant per element, so
less sensitive than magnetics iron, but still high-gradient near electrodes).

Usage:
    femm_headless -run EOPointValueTest.py > fq_eopv_result.txt
    python3 ScalarPointValueCompare.py OF_eopv_result.txt fq_eopv_result.txt [tol]
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
        m = re.match(r"(.+?)\s+=\s+(.+)$", t)
        if not m or "no mesh" in m.group(2):
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

    def denom_for(comps, j):
        # col 0 is the scalar V/T; cols >=1 are consecutive (x,y) vector pairs.
        if j == 0:
            return abs(comps[0])
        p = (j - 1) // 2
        a, b = 1 + 2 * p, 2 + 2 * p
        if b < len(comps):
            return math.hypot(abs(comps[a]), abs(comps[b]))   # local pair magnitude
        return abs(comps[j])                                  # lone trailing scalar (eo energy)

    ok, flags = 0, []
    for k in sorted(rv, key=lambda x: (x[0], str(x[1]))):
        if k not in tv:
            continue
        s, pt = k
        rc, tc = rv[k], tv[k]
        for j in range(min(len(rc), len(tc))):
            denom = denom_for(rc, j)
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
            print("  [%-24s] %-12s col%-2d ref=%-13s test=%-13s err=%.1f%%"
                  % (rsec[s][:24], pt, j, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_spv_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_spv_result.txt"
    tol  = float(sys.argv[3]) if len(sys.argv) > 3 else TOL
    sys.exit(1 if compare(ref, test, tol) else 0)
