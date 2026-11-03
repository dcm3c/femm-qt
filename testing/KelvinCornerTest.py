# KelvinCornerTest.py -- periodic pairing where two pairings meet at a node
# (CPython; drives femm_headless).
#
# A half model of a current-carrying disk in free space: a 180-degree sector
# of radius 20 whose two radial cuts are paired anti-periodically (rotation
# by 180 degrees), with the outside of the disk represented by a Kelvin
# inversion: a second half-disk of radius 2 centred at (28, 0) whose arc is
# paired periodically with the main arc and whose own diameter halves are
# anti-periodic. In the plane, inversion needs no permeability scaling, so
# this is an exact free-space model.
#
# Each of the four corner nodes (20,0) (-20,0) (26,0) (30,0) sits on TWO
# boundaries belonging to different transformations and so has two
# different partners. The mesher's node-twin map is a matching and holds
# one, which through tangle 0.4.18 (a) desynchronised the refinement splits
# on the Kelvin arcs so the whole chain went unpaired -- the exterior
# diameter silently became a natural boundary, +23% error -- and (b) dropped
# one corner pair depending on the order the boundary properties were
# declared. Fixed in 0.4.19 with per-segment partners and pairs emitted per
# entity end. Found while chasing the 27 Oct 2026 sliding-band report.
#
# Checks, on the model built with the properties declared in both orders:
#   1. every node on the four radial cuts has an (anti)periodic partner;
#   2. the four corner pairs are all emitted and connect the corners;
#   3. the two declaration orders give the same potentials;
#   4. A near the axis of symmetry agrees with the exact free-space solution
#      (J = +1 MA/m^2 in the upper half of a 10 mm disk, -1 in the lower)
#      to 1%: this is the check that fails when the Kelvin chain is lost.
#
# Usage:  python3 KelvinCornerTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes kc_tmp* scratch files.
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


BIN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("femm_headless")
BASE = os.path.join(HERE, "kc_tmp").replace("\\", "/")

SCRIPT = r'''
D = "%s"

def props(order):
    anti = lambda n: mi_addboundprop(n, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0)
    per  = lambda n: mi_addboundprop(n, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0)
    if order == "a":
        anti("InnerRadial"); anti("OuterRadial"); per("Kelvin"); anti("AuxiliaryRadial")
    else:
        anti("AuxiliaryRadial"); per("Kelvin"); anti("OuterRadial"); anti("InnerRadial")

def arc(x1, y1, x2, y2, prop):
    mi_drawarc(x1, y1, x2, y2, 180, 2.5)
    if prop:
        mi_clearselected()
        mi_selectarcsegment((x1 + x2) / 2.0, (y1 + y2) / 2.0 + abs(x1 - x2) / 2.0)
        mi_setarcsegmentprop(2.5, prop, 0, 0)
        mi_clearselected()

def seg(x1, x2, prop):
    mi_drawline(x1, 0, x2, 0)
    mi_clearselected()
    mi_selectsegment((x1 + x2) / 2.0, 0)
    mi_setsegmentprop(prop, 0, 1, 0, 0)
    mi_clearselected()

def label(x, y, mat, size):
    mi_addblocklabel(x, y)
    mi_selectlabel(x, y)
    if size > 0:
        mi_setblockprop(mat, 0, size, "<None>", 0, 0, 0)
    else:
        mi_setblockprop(mat, 1, 0, "<None>", 0, 0, 0)
    mi_clearselected()

for order in ("a", "b"):
    newdocument(0)
    mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    mi_addmaterial("Air", 1, 1, 0, 0)
    mi_addmaterial("UniformCurrent", 1, 1, 0, 1)
    props(order)
    arc(20, 0, -20, 0, "Kelvin")
    arc(10, 0, -10, 0, None)
    arc(30, 0, 26, 0, "Kelvin")
    seg(-20, -10, "OuterRadial"); seg(10, 20, "OuterRadial")
    seg(-10, 0, "InnerRadial");   seg(0, 10, "InnerRadial")
    seg(26, 28, "AuxiliaryRadial"); seg(28, 30, "AuxiliaryRadial")
    label(0, 5, "UniformCurrent", 0)
    label(2, 15, "Air", 0)
    label(28, 1, "Air", 0.2)
    mi_saveas(D + "_" + order + ".fem")
    mi_analyze()
    mi_close()
    print("BUILT", order)
''' % BASE

MU0 = 4e-7 * math.pi
R, J = 10e-3, 1e6


def exact_A(px, py, N=400):
    """Free-space A at (px, py) in metres for the full disk, J = +1 MA/m^2
    above y = 0 and -1 below; the half model's full equivalent."""
    h = 2 * R / N
    s = 0.0
    for i in range(N):
        x = -R + (i + 0.5) * h
        for j in range(N):
            y = -R + (j + 0.5) * h
            if x * x + y * y < R * R:
                s += (J if y > 0 else -J) * math.log(math.hypot(px - x, py - y))
    return -MU0 / (2 * math.pi) * s * h * h


def read_ans(path):
    rows = [r.strip() for r in open(path, encoding="utf-8-sig").read().splitlines()]
    s = rows.index("[Solution]") + 1
    n = int(rows[s])
    nodes = [tuple(map(float, r.split()[:3])) for r in rows[s + 1:s + 1 + n]]
    c = s + 1 + n
    c += 1 + int(rows[c])          # elements
    c += 1 + int(rows[c])          # block labels
    npairs = int(rows[c])
    pairs = []
    for line in rows[c + 1:c + 1 + npairs]:
        v = line.split()
        pairs.append((int(v[0]), int(v[1]), int(v[2])))
    return nodes, pairs


def check(order, nodes, pairs, fails):
    partner = {}
    for a, b, t in pairs:
        partner.setdefault(a, set()).add(b)
        partner.setdefault(b, set()).add(a)
    axis = [i for i, (x, y, _) in enumerate(nodes) if abs(y) < 1e-9]
    unpaired = [nodes[i][0] for i in axis if i not in partner]
    print("  order %s: %d nodes, %d pairs, %d axis nodes, %d unpaired" % (order, len(nodes), len(pairs), len(axis), len(unpaired)))
    if unpaired:
        fails.append("order %s: %d radial-cut nodes have no partner, e.g. x=%s" % (order, len(unpaired), unpaired[:4]))
    name = {}
    for i, (x, y, _) in enumerate(nodes):
        if abs(y) < 1e-9 and abs(x - round(x)) < 1e-9 and round(x) in (20, -20, 26, 30):
            name[i] = round(x)
    got = set()
    for a, b, t in pairs:
        if a in name and b in name:
            got.add((min(name[a], name[b]), max(name[a], name[b]), t))
    want = {(20, 26, 0), (26, 30, 1), (-20, 30, 0), (-20, 20, 1)}
    if got != want:
        fails.append("order %s: corner pairs %s, want %s" % (order, sorted(got), sorted(want)))
    return nodes


def main():
    with open(BASE + ".py", "w") as f:
        f.write(SCRIPT)
    fails = []
    try:
        r = subprocess.run([BIN, "-run", BASE + ".py"], capture_output=True, text=True)
        built = [ln.split()[1] for ln in r.stdout.splitlines() if ln.startswith("BUILT")]
        if r.returncode != 0 or built != ["a", "b"]:
            fails.append("engine exit %d, built %s\n%s%s" % (r.returncode, built, r.stdout, r.stderr))
        else:
            na, pa = read_ans(BASE + "_a.ans")
            nb, pb = read_ans(BASE + "_b.ans")
            check("a", na, pa, fails)
            check("b", nb, pb, fails)
            # same potentials in both declaration orders
            bx = {(round(x, 9), round(y, 9)): a for x, y, a in nb}
            amax = max(abs(a) for _, _, a in na)
            dmax, missing = 0.0, 0
            for x, y, a in na:
                q = bx.get((round(x, 9), round(y, 9)))
                if q is None: missing += 1
                else: dmax = max(dmax, abs(a - q))
            print("  orders a vs b: %d unmatched nodes, max |dA| = %.3g of max |A|" % (missing, dmax / amax))
            if missing or dmax > 1e-9 * amax:
                fails.append("declaration order changes the solution: %d unmatched, %.3g of max|A|" % (missing, dmax / amax))
            # exact free-space field on the symmetry axis
            # Exact field evaluated AT the nearest node, not at the nominal
            # point: the mesh puts no node exactly on (0, r), and 0.2 mm of
            # offset is worth more than 1% here.
            for (px, py) in ((0, 12), (0, 15), (0, 19)):
                x, y, a = min(na, key=lambda t: (t[0] - px) ** 2 + (t[1] - py) ** 2)
                e = exact_A(x * 1e-3, y * 1e-3)
                rel = abs(a - e) / abs(e)
                print("  A at (%.2f,%.2f): femm %.5g, exact %.5g, %.2f%%" % (x, y, a, e, 100 * rel))
                if rel > 0.01:
                    fails.append("A at (%.2f,%.2f) is %.1f%% from the exact free-space value" % (x, y, 100 * rel))
    finally:
        for f in os.listdir(HERE):
            if f.startswith("kc_tmp"):
                try:
                    os.remove(os.path.join(HERE, f))
                except OSError:
                    pass
    if fails:
        for f in fails:
            print("  VIOLATION: " + f)
        print("KELVIN CORNER CHECK FAILED: %d violation(s)" % len(fails))
        sys.exit(1)
    print("kelvin corner check: every cut node paired, all four corner pairs, order-independent, exact field to 1%")


if __name__ == "__main__":
    main()
