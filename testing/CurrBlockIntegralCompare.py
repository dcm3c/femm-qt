#!/usr/bin/env python3
"""
CurrBlockIntegralCompare.py -- diff two CurrBlockIntegralTest runs.

Compares the printed output of the co_blockintegral test (CurrBlockIntegralTest.py
on femm-qt vs CurrBlockIntegralTest.lua on Original FEMM) and flags meaningful
disagreements. Both output formats are auto-detected per line:

    OF (Lua):   -->   [n] label:   re=X   im=Y
    femm-qt:      [n] label = value units (kind)

Normalization (co_blockintegral-specific):
  * REACTIVE (1) and APPARENT (2) power: Original FEMM has a bug here -- it forms
    the reactive/apparent power from E*E (phasor squared) instead of E*conj(E),
    so its result carries a spurious phase exp(2j*theta) while the *magnitude* is
    right. femm-qt fixes this and returns the correct real quantities
    Q = 1/2 Im(integral J.conj(E)) and |S| = |integral 1/2 J.conj(E)| = sqrt(P^2+Q^2).
    So for these two types we compare femm-qt's real value against |OF| (the
    magnitude of OF's complex value); femm-qt is authoritative. See
    curr_results_doc.cpp BlockIntegral cases 1 and 2.
  * A FORCE component is normalized by the magnitude of the dominant force in its
    run, so a small cross-axis component isn't judged against itself and a
    sign-flip on a near-zero residual doesn't masquerade as a huge error.
  * A TORQUE is normalized by the run's dominant torque, or by force * LENGTH for
    runs that produce no real torque.
  * Everything else (real power, stored energy, area, volume) is a plain relative
    error against the reference.

Usage:
    # capture a femm-qt run:
    femm_headless CurrBlockIntegralTest.py | grep -E '={5,}|^  \\[' > fq_result.txt
    # compare against an OF reference (reference first, test second):
    python3 CurrBlockIntegralCompare.py OF_curr_result.txt fq_result.txt

Exit status is nonzero if anything is flagged, so it can gate a build.
"""
import re
import sys

# --- co_blockintegral (current flow) integral classification ---
# 0 real power, 1 reactive power, 2 apparent power, 3 time-avg stored energy,
# 4 cross-section area, 5 volume, 6/7 WST force x/y (SS), 8/9 WST force x/y (2x),
# 10 WST torque (SS), 11 WST torque (2x).
FORCE  = {6, 7, 8, 9}          # x/y (r/z) force components, SS and 2x
TORQUE = {10, 11}
MAGREF = {1, 2}                # compare femm-qt real vs |OF| (OF's phase is a bug)

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

    def refval(s, n):
        # reactive/apparent power: OF's phase is spurious, only |OF| is meaningful.
        v = rv[(s, n)]
        return complex(abs(v)) if n in MAGREF else v

    def maxmag(s, group):
        v = [abs(rv[(s, n)]) for n in group if (s, n) in rv]
        return max(v) if v else 0.0

    def scale(s, n):
        if n in FORCE:
            return maxmag(s, FORCE)                              # run's dominant force
        if n in TORQUE:
            return max(maxmag(s, TORQUE), maxmag(s, FORCE) * length)
        return abs(refval(s, n))                                 # scalar: relative to self

    ok, flags = 0, []
    for k in sorted(rv):
        if k not in tv:
            continue
        s, n = k
        ref = refval(s, n)
        sc = scale(s, n)
        ne = abs(tv[k] - ref) / sc if sc > floor else 0.0
        if sc <= floor or ne <= tol:
            ok += 1
        else:
            flags.append((k, ref, tv[k], ne))

    print("compared %d integral values: %d agree (norm err <= %.1f%%), %d flagged"
          % (ok + len(flags), ok, tol * 100, len(flags)))
    if flags:
        print("\nFLAGGED (reference vs test):")
        for (s, n), a, b, ne in sorted(flags, key=lambda x: -x[3]):
            print("  [%-40s] #%2d  ref=%-14s test=%-14s norm=%.1f%%"
                  % (rsec[s][:40], n, _fmt(a), _fmt(b), ne * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_curr_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
