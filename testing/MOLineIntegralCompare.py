#!/usr/bin/env python3
"""
MOLineIntegralCompare.py -- diff two MOLineIntegralTest runs.

Compares the printed output of the mo_lineintegral test (MOLineIntegralTest.py on
femm-qt vs MOLineIntegralTest.lua on Original FEMM). Both runs emit the same
format, one line per integral:

    [<type>] <label> = re im | re im | ...      (one "re im" pair per component)

Normalization (mo_lineintegral-specific):
  * FORCE (type 3) and TORQUE (type 4): each component is normalized by the
    dominant (largest-magnitude) component of that same integral, so a small
    cross-axis force or a near-zero residual isn't judged against itself.
  * Everything else (B.n flux, H.t, contour length/area, (B.n)^2) is a plain
    relative error against the reference component.

The tolerance is looser than the block-integral suite: a single-contour Maxwell-
stress line integral converges more slowly than the WST volume average and is
more sensitive to the mesh (femm-qt's tangle vs OF's triangle), so ~1-2% is
expected even when everything is correct.

Usage:
    # capture a femm-qt run:
    femm_headless -run MOLineIntegralTest.py > fq_line_result.txt
    # compare against an OF reference (reference first, test second):
    python3 MOLineIntegralCompare.py OF_line_result.txt fq_line_result.txt

Exit status is nonzero if anything is flagged, so it can gate a build.
"""
import re
import sys

FORCE  = {3}
TORQUE = {4}

TOL   = 0.02     # flag if normalized/relative error exceeds this
FLOOR = 1e-9     # below this magnitude a component is treated as ~0 and skipped


def parse(path):
    """Return (section_titles, {(section, type): [complex, ...]})."""
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
                comps.append(complex(float(nums[0]), float(nums[1])))
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
        # scale for force/torque: the dominant component of this integral
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
            print("  [%-34s] type %d comp %d  ref=%-14s test=%-14s err=%.1f%%"
                  % (rsec[s][:34], n, j, _fmt(a), _fmt(b), err * 100))
    return len(flags)


if __name__ == "__main__":
    ref  = sys.argv[1] if len(sys.argv) > 1 else "OF_line_result.txt"
    test = sys.argv[2] if len(sys.argv) > 2 else "fq_line_result.txt"
    sys.exit(1 if compare(ref, test) else 0)
