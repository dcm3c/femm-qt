#!/usr/bin/env python3
"""
MOBlockIntegralCompare.py -- diff two MOBlockIntegralTest runs.

Compares the printed output of the mo_blockintegral test (MOBlockIntegralTest.py
on femm-qt vs MOBlockIntegralTest.lua on Original FEMM) and flags meaningful
disagreements. Both output formats are auto-detected per line:

    OF (Lua):   -->   [n] label:   re=X   im=Y
    femm-qt:      [n] label = value units (kind)

Normalization (this is mo_blockintegral-specific -- see FORCE/TORQUE below):
  * A force component is normalized by the magnitude of the dominant force in
    its run, so a small cross-axis component (e.g. a 0.2 N y-force riding on a
    400 N x-force) isn't judged against itself and sign-flip on a near-zero
    residual doesn't masquerade as a huge error.
  * A torque is normalized by the run's dominant torque, or by force * LENGTH
    for runs that produce no real torque (so ~0 torque residuals don't flag).
  * Everything else (energy, losses, current, area, A.J, ...) is a plain
    relative error against the reference.

Usage:
    # capture a femm-qt run:
    femm_headless -run MOBlockIntegralTest.py | grep -E '={5,}|^  \\[' > fq_result.txt
    # compare against an OF reference (reference first, test second):
    python3 MOBlockIntegralCompare.py OF_result.txt fq_result.txt

Exit status is nonzero if anything is flagged, so it can gate a build.

Sibling comparators for the line integrals and the other physics will follow
this same shape, each with its own integral classification.
"""
import re
import sys

# --- mo_blockintegral (magnetics) integral classification ---
FORCE  = {11, 12, 13, 14, 18, 19, 20, 21, 25, 26, 28, 29}   # x/y force components
TORQUE = {15, 16, 22, 23, 27, 30}

TOL    = 0.02    # flag if normalized error exceeds this
LENGTH = 0.03    # characteristic lever arm (m): a torque residual below
                 # force*LENGTH is treated as negligible
FLOOR  = 1e-6    # below this scale, the whole quantity is ~0; skip


def parse(path):
    """Return (section_titles, {(section, n): complex}); auto-detects the format."""
    secs, vals, s = [], {}, -1
    for line in open(path, encoding="utf-8", errors="replace"):
        t = line.replace("-->", "").strip()
        if t.startswith("====="):
            s += 1
            secs.append(t.strip("= ").strip())
            continue
        m = re.match(r"\[(\d+)\]\s+.*:\s+re=(\S+)\s+im=(\S+)", t)          # OF
        if m:
            vals[(s, int(m.group(1)))] = complex(float(m.group(2)), float(m.group(3)))
            continue
        m = re.match(r"\[\s*(\d+)\]\s+.*?=\s+(\S+)\s+\S+\s+\(\w+\)", t)    # femm-qt
        if m:
            try:
                vals[(s, int(m.group(1)))] = complex(m.group(2))
            except ValueError:
                pass
    return secs, vals


def _fmt(z):
    if abs(z.imag) < 1e-12 * max(abs(z.real), 1.0):
        return "%.6g" % z.real
    return "%.6g%+.4gj" % (z.real, z.imag)


def compare(ref_path, test_path, tol=TOL, length=LENGTH, floor=FLOOR):
    rsec, rv = parse(ref_path)
    _,    tv = parse(test_path)

    def maxmag(s, group):
        v = [abs(rv[(s, n)]) for n in group if (s, n) in rv]
        return max(v) if v else 0.0

    def scale(s, n):
        if n in FORCE:
            return maxmag(s, FORCE)                              # run's dominant force
        if n in TORQUE:
            return max(maxmag(s, TORQUE), maxmag(s, FORCE) * length)
        return abs(rv.get((s, n), 0.0))                          # scalar: relative to self

    ok, flags = 0, []
    for k in sorted(rv):
        if k not in tv:
            continue
        s, n = k
        sc = scale(s, n)
        ne = abs(tv[k] - rv[k]) / sc if sc > floor else 0.0
        if sc <= floor or ne <= tol:
            ok += 1
        else:
            flags.append((k, rv[k], tv[k], ne))

    print("compared %d integral values: %d agree (norm err <= %.1f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for (s, n), a, b, ne in sorted(flags, key=lambda x: -x[3]):
            print("  [%-40s] #%2d  ref=%-14s test=%-14s norm=%.1f%%"
                  % (rsec[s][:40], n, _fmt(a), _fmt(b), ne * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
