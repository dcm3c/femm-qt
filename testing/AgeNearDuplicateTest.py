# AgeNearDuplicateTest.py -- a near-duplicate input point must not shift the
# sliding-band (AGE) ring references (CPython; drives fkn directly).
#
# readFemFile records each air-gap ring as a list of input-vertex indices.
# cleanPSLG then merges points closer than bbDiag*1e-6 and compacts the
# vertex list, remapping segments but, through tangle 0.4.18, NOT the ring
# lists -- so one merged point anywhere before the ring nodes left every
# band reference pointing one node off its circle. The solver used them
# without complaint (2.2% of max|A| on the model that revealed it), so
# this was a silent wrong answer. The editor, mi_addnode and the DXF
# reader all refuse or merge such points, which is why only externally
# generated .fem files could expose it. Reported 27 Oct 2026 (sliding-band
# references off-circle); fixed in tangle 0.4.19.
#
# The fixture is the shipped SPM motor with an air gap element plus one
# extra input point 1e-9 in from its first point: far inside the merge
# tolerance, referenced by nothing. Both models are solved with fkn and:
#   1. every ring reference in the modified model lies on its circle, with
#      strictly increasing angle around the ring;
#   2. the ring references match the unmodified model's node for node;
#   3. the nodal potentials agree to solver precision.
#
# Usage:  python3 AgeNearDuplicateTest.py [path-to-fkn]
# Runs from testing/; writes and removes agedup_tmp* scratch files.
# Exits nonzero on any violation.

import math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _default_engine(name):
    # Windows and Linux leave the engine in build/bin. macos_build.sh keeps
    # only the copy inside the app bundle, where the solvers sit beside it.
    b = os.path.join(HERE, "..", "build", "bin")
    loose = os.path.join(b, name + (".exe" if os.name == "nt" else ""))
    bundled = os.path.join(b, "femm_gui.app", "Contents", "MacOS", name)
    return bundled if not os.path.exists(loose) and os.path.exists(bundled) else loose


FKN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("fkn")
FIXTURE = os.path.join(HERE, "..", "examples", "my_SPM_motor_AGB.fem")
BASE = os.path.join(HERE, "agedup_tmp")
DELTA = 1e-9   # inches; the merge tolerance on this model is ~3e-6


def write_models():
    text = open(FIXTURE, encoding="utf-8-sig").read().splitlines()
    i = next(k for k, ln in enumerate(text) if ln.startswith("[NumPoints]"))
    n = int(text[i].split("=")[1])
    first = text[i + 1].split()
    dup = "%.17g\t%s\t%s\t%s" % (float(first[0]) + DELTA, first[1], first[2], first[3])
    plain = text[:]
    with open(BASE + "_a.fem", "w") as f:
        f.write("\n".join(plain) + "\n")
    mod = text[:i] + ["[NumPoints] = %d" % (n + 1)] + text[i + 1:i + 1 + n] + [dup] + text[i + 1 + n:]
    with open(BASE + "_b.fem", "w") as f:
        f.write("\n".join(mod) + "\n")


def solve(tag):
    r = subprocess.run([FKN, BASE + "_" + tag], capture_output=True, text=True)
    if r.returncode != 0:
        return None, "fkn exit %d on %s\n%s" % (r.returncode, tag, r.stderr)
    return BASE + "_" + tag + ".ans", None


def read_ans(path):
    """-> (nodes [(x, y, A)], rings [per AGE: (ri, ro, cx, cy, [(i0, i2, i4, i6)])])"""
    rows = [r.strip() for r in open(path, encoding="utf-8-sig").read().splitlines()]
    s = rows.index("[Solution]") + 1
    n = int(rows[s])
    nodes = [tuple(map(float, r.split()[:3])) for r in rows[s + 1:s + 1 + n]]
    c = s + 1 + n
    c += 1 + int(rows[c])          # elements
    c += 1 + int(rows[c])          # block labels
    c += 1 + int(rows[c])          # periodic pairs
    ngaps = int(rows[c]); c += 1
    rings = []
    for _ in range(ngaps):
        c += 1                     # name line
        meta = list(map(float, rows[c].split()))
        ri, ro, cx, cy, seg = meta[3], meta[4], meta[6], meta[7], int(meta[8])
        refs = []
        for k in range(seg + 1):
            v = rows[c + 1 + k].split()
            refs.append(tuple(int(v[col]) for col in (0, 2, 4, 6)))
        rings.append((ri, ro, cx, cy, refs))
        c += 1 + seg + 1
    return nodes, rings


def check_ring(nodes, ring, fails, tag):
    ri, ro, cx, cy, refs = ring
    prev = None
    for k, (i0, i2, i4, i6) in enumerate(refs):
        for idx, radius, what in ((i0, ri, "inner"), (i2, ri, "inner"), (i4, ro, "outer"), (i6, ro, "outer")):
            x, y = nodes[idx][0], nodes[idx][1]
            r = math.hypot(x - cx, y - cy)
            if abs(r - radius) > 1e-6 * radius:
                fails.append("%s: ring row %d %s reference %d is at r=%.6g, not %.6g" % (tag, k, what, idx, r, radius))
                return
        x, y = nodes[i0][0], nodes[i0][1]
        ang = math.atan2(y - cy, x - cx)
        if prev is not None and k < len(refs) - 1:
            d = (ang - prev + math.pi) % (2 * math.pi) - math.pi
            if d <= 0:
                fails.append("%s: ring angle not increasing at row %d" % (tag, k))
                return
        prev = ang


def main():
    write_models()
    fails = []
    try:
        ans_a, err = solve("a")
        if err: fails.append(err)
        ans_b, err = solve("b")
        if err: fails.append(err)
        if not fails:
            na, ra = read_ans(ans_a)
            nb, rb = read_ans(ans_b)
            if len(ra) != len(rb) or not ra:
                fails.append("air gap count differs or zero: %d vs %d" % (len(ra), len(rb)))
            else:
                for g, (ring_a, ring_b) in enumerate(zip(ra, rb)):
                    before = len(fails)
                    check_ring(nb, ring_b, fails, "gap %d (with duplicate)" % g)
                    check_ring(na, ring_a, fails, "gap %d (plain)" % g)
                    ca = [[nodes_xy(na, i) for i in row] for row in ring_a[4]]
                    cb = [[nodes_xy(nb, i) for i in row] for row in ring_b[4]]
                    if ca != cb:
                        bad = sum(1 for p, q in zip(ca, cb) if p != q)
                        fails.append("gap %d: %d of %d ring rows reference different nodes" % (g, bad, len(ca)))
                    if len(fails) == before:
                        print("  gap %d: %d ring rows, all references on circle and in order, identical with and without the extra point" % (g, len(ring_a[4])))
                amax = max(abs(a) for _, _, a in na)
                dmax = 0.0
                if len(na) == len(nb):
                    bx = {(round(x, 9), round(y, 9)): a for x, y, a in nb}
                    missing = 0
                    for x, y, a in na:
                        q = bx.get((round(x, 9), round(y, 9)))
                        if q is None:
                            missing += 1
                        else:
                            dmax = max(dmax, abs(a - q))
                    print("  nodes %d, unmatched %d, max |dA| = %.3g (%.3g of max |A|)" % (len(na), missing, dmax, dmax / amax))
                    if missing or dmax > 1e-9 * amax:
                        fails.append("solutions differ: %d unmatched nodes, max |dA|/max|A| = %.3g" % (missing, dmax / amax))
                else:
                    fails.append("node counts differ: %d vs %d (the extra point was not merged)" % (len(na), len(nb)))
    finally:
        for f in os.listdir(HERE):
            if f.startswith("agedup_tmp"):
                try:
                    os.remove(os.path.join(HERE, f))
                except OSError:
                    pass
    if fails:
        for f in fails:
            print("  VIOLATION: " + f)
        print("AGE NEAR-DUPLICATE CHECK FAILED: %d violation(s)" % len(fails))
        sys.exit(1)
    print("age near-duplicate check: ring references on circle, in order and unshifted; solution identical")


def nodes_xy(nodes, i):
    return (round(nodes[i][0], 12), round(nodes[i][1], 12))


if __name__ == "__main__":
    main()
