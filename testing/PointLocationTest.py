# PointLocationTest.py
# Point location: does InTriangleTest find points that are provably in the mesh?
#
#     build/bin/femm_headless -run testing/PointLocationTest.py
#
# The predicate tests each edge with an orientation-consistent cross product,
# with the endpoints ordered by node index so that two triangles sharing an edge
# evaluate the identical expression. Every probe point below is inside the mesh
# by construction, so any miss is a defect. Three probes, in increasing order of
# how much they lean on the arithmetic being exact:
#
#   mesh nodes      the query point IS a vertex, so both products come from the
#                   same pair of doubles and cancel exactly -- until a compiler
#                   contracts a*b - c*d into a fused multiply-add and rounds
#                   only one of them. clang/arm64 does that by default, which is
#                   how this was found on macOS: a degree-1 node fails 100% of
#                   the time, and one such node sat under a line-integral
#                   endpoint.
#   interior edges  the CONTROL. Two triangles share the edge and evaluate the
#                   identical expression with opposite reject conditions, so
#                   exactly one claims the point however z rounds. This passes
#                   with or without the tolerance; if it ever fails, the
#                   index-ordering itself has been broken.
#   boundary edges  one incident triangle and nothing to catch a wrong sign.
#                   Here the two products are equal only as REALS, so z is a
#                   rounding residual of arbitrary sign on every platform.
#                   Before libfemm/point_location.h this lost 23-35% of them,
#                   on Windows as much as on macOS. Axis-parallel edges are
#                   exempt (both products carry a zero factor), which is why
#                   contours hugging r=0 never exposed it -- so only slanted
#                   boundary edges are counted.

import os

fails = []
summary = []


# The per-physics commands exist only once a document of that type is open, so
# each case is a thunk that opens its file and hands back the accessors.

def heat(stem):
    open("heatFlow1.feh"); hi_saveas(stem + ".feh"); hi_analyze(); hi_loadsolution()
    return "anh", ho_getpointvalues, hi_close

def elec(stem):
    open("electrostatic1.fee"); ei_saveas(stem + ".fee"); ei_analyze(); ei_loadsolution()
    return "res", eo_getpointvalues, ei_close

def curr(stem):
    open("currentFlow1.fec"); ci_saveas(stem + ".fec"); ci_analyze(); ci_loadsolution()
    return "anc", co_getpointvalues, ci_close

def mag(stem):
    open("RadialMagneticBearing.fem"); mi_saveas(stem + ".fem")
    mi_analyze(); mi_loadsolution()
    return "ans", mo_getpointvalues, mi_close

CASES = [("heatFlow1", heat), ("electrostatic1", elec),
         ("currentFlow1", curr), ("RadialMagneticBearing", mag)]


def read_mesh(path):
    """Node coordinates and element connectivity from a solution file."""
    fh = _builtin_open(path, "r")
    try:
        line = fh.readline()
        while line and not line.strip().lower().startswith("[solution]"):
            line = fh.readline()
        if not line:
            return None, None
        nn = int(fh.readline().split()[0])
        nodes = []
        for _ in range(nn):
            f = fh.readline().split()
            nodes.append((float(f[0]), float(f[1])))
        ne = int(fh.readline().split()[0])
        elems = []
        for _ in range(ne):
            f = fh.readline().split()
            elems.append((int(f[0]), int(f[1]), int(f[2])))
        return nodes, elems
    finally:
        fh.close()


def classify_edges(elems, nn):
    """Split the mesh edges into interior (shared) and boundary (used once).

    Each undirected edge is packed into one integer, lo*nn+hi, and the whole
    list is sorted. A dict keyed by (lo, hi) tuples is the obvious way to write
    this and is twenty times slower in MicroPython -- 28 s against 1.6 s on a
    39k-element mesh, which is the difference between this test belonging in
    the suite and not. The packed key stays a small int for any mesh femm-qt
    can solve, so the sort is a machine-word sort.
    """
    keys = []
    for (a, b, c) in elems:
        for (u, v) in ((a, b), (b, c), (c, a)):
            keys.append(u * nn + v if u < v else v * nn + u)
    keys.sort()

    interior, boundary = [], []
    i, n = 0, len(keys)
    while i < n:
        j = i + 1
        while j < n and keys[j] == keys[i]:
            j += 1
        (boundary if j - i == 1 else interior).append(keys[i])
        i = j
    return interior, boundary


def sample(seq, cap):
    """A strided subset, so a big mesh stays affordable without going random."""
    if len(seq) <= cap:
        return seq
    return seq[::len(seq) // cap + 1]


def probe(getpv, pts, label, tag):
    lost = 0
    for (x, y) in pts:
        if getpv(x, y) is None:
            lost += 1
    summary.append("    %-16s %6d probed, %5d lost" % (label, len(pts), lost))
    if lost:
        fails.append("%s: %s -- %d of %d points inside the mesh were not found"
                     % (tag, label, lost, len(pts)))


def run(tag, setup):
    stem = "_ptloc_" + tag
    ext, getpv, closedoc = setup(stem)

    nodes, elems = read_mesh(stem + "." + ext)
    if nodes is None:
        fails.append("%s: no [Solution] section in %s.%s" % (tag, stem, ext))
        return
    nn = len(nodes)
    interior, boundary = classify_edges(elems, nn)

    def mids(keys, slanted_only):
        out = []
        for k in keys:
            x0, y0 = nodes[k // nn]
            x1, y1 = nodes[k % nn]
            if slanted_only and (x0 == x1 or y0 == y1):
                continue
            out.append((0.5 * (x0 + x1), 0.5 * (y0 + y1)))
        return out

    bnd = mids(boundary, True)
    summary.append("  %s: %d nodes, %d elements, %d boundary edges (%d slanted)"
                   % (tag, nn, len(elems), len(boundary), len(bnd)))

    # Boundary edges are few and are the whole point, so probe every one. Nodes
    # and interior edges are numerous and uniform, so a strided sample covers
    # the mesh without making the suite wait.
    probe(getpv, sample(nodes, 4000), "mesh nodes", tag)
    probe(getpv, sample(mids(interior, False), 4000), "interior edges", tag)
    probe(getpv, bnd, "boundary edges", tag)

    closedoc()
    for f in os.listdir("."):
        if f.startswith(stem + "."):
            os.remove(f)


for tag, setup in CASES:
    try:
        run(tag, setup)
    except Exception as e:
        fails.append("%s: %s" % (tag, e))

for s in summary:
    print(s)

if fails:
    for m in fails:
        print("   " + m)
    # verdict last: _verify_all.sh reports this test by its final line
    print("point location: %d FAILED" % len(fails))
else:
    print("point location: every mesh node and every edge midpoint was found, "
          "in all four physics -- including slanted boundary edges, where a "
          "single incident triangle has to get the sign right on its own")
