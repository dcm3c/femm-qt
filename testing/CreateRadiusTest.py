# mi_createradius: does the fillet actually touch both curves?
#
#     build/bin/femm_headless -run testing/CreateRadiusTest.py
#
# A "radius" replaces a corner with an arc TANGENT to both curves meeting
# there. Tangency is a property of the RESULT, so it can be checked without
# knowing how the result was produced -- which is what makes this a useful
# test rather than a restatement of the implementation:
#
#     fillet against an arc   distance between centres == |R -+ r|
#     fillet against a line   perpendicular distance from centre == r
#
# WHY IT EXISTS. femm-qt's CreateRadius was, until 2026-09-02, a rewrite rather
# than a port: instead of calling AddNode()/DeleteSelectedNodes() as FEMM 4.2
# does, it retargeted each curve's endpoint onto a new node itself. That lost
# two things AddNode does for free, and both broke tangency:
#
#   * AddNode attaches a tangency point to whichever curve it LIES ON. Choosing
#     by hand meant choosing after a swap that exists only to orient the new
#     arc -- so when that swap fired, the line was re-terminated on the arc's
#     tangency point and vice versa.
#   * AddNode SPLITS the curve it lands on, and a split recomputes both sweep
#     angles. An arc is (n0, n1, swept angle) with centre and radius DERIVED,
#     so moving an endpoint without recomputing the sweep silently moves the
#     arc onto a different circle.
#
# Against the pre-fix build this file reports 3 failures: one for line+arc and
# two for arc+arc. Line+line passed even then, because that branch swapped its
# two points and their two segments TOGETHER, keeping the pairing intact. (How
# many of a fillet's two ends come out non-tangent depends on the geometry --
# the model in the original bug report failed the other way round, two for
# line+arc and one for arc+arc. The count is not the invariant; tangency is.)
#
# The geometry is built here rather than loaded, so there is no fixture to ship
# and each case is minimal enough to reason about by hand.
import math
import os

fails = []


def hypot(a, b):
    # MicroPython math has no hypot()
    return math.sqrt(a * a + b * b)


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + (": " + detail if detail else ""))


def read_geometry(path):
    # _builtin_open, not open: a bare .fem path handed to open() opens a
    # DOCUMENT here, and the file opener takes no encoding= keyword.
    fh = _builtin_open(path)
    L = fh.read().split("\n")
    fh.close()

    def count(tag):
        i = [k for k, s in enumerate(L) if s.startswith(tag)][0]
        return i, int(L[i].split("=")[1])

    i, nn = count("[NumPoints]")
    pts = [tuple(float(v) for v in L[i + 1 + k].split()[:2]) for k in range(nn)]
    i, ns = count("[NumSegments]")
    segs = [tuple(int(v) for v in L[i + 1 + k].split()[:2]) for k in range(ns)]
    i, na = count("[NumArcSegments]")
    arcs = []
    for k in range(na):
        f = L[i + 1 + k].split()
        arcs.append((int(f[0]), int(f[1]), float(f[2])))
    return pts, segs, arcs


def circle(p0, p1, sweep_deg):
    """Centre and radius of the arc sweeping sweep_deg from p0 to p1 --
    FEMM's GetCircle, which is what makes centre and radius DERIVED
    quantities rather than stored ones."""
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    chord = hypot(dx, dy)
    half = math.radians(sweep_deg) / 2.0
    R = chord / (2.0 * math.sin(half))
    h = R * math.cos(half)
    mx, my = (p0[0] + p1[0]) / 2.0, (p0[1] + p1[1]) / 2.0
    return (mx - h * dy / chord, my + h * dx / chord), R


def check_fillet(label, path, r, tol=1e-9):
    pts, segs, arcs = read_geometry(path)

    fillet = None
    for idx, (n0, n1, s) in enumerate(arcs):
        c, R = circle(pts[n0], pts[n1], s)
        if abs(R - r) < 1e-7:
            fillet = (idx, n0, n1, c, R)
    if fillet is None:
        check(label + ": a fillet of the requested radius exists", False,
              "no arc of radius %g among %d" % (r, len(arcs)))
        return
    idx, fn0, fn1, fc, fR = fillet

    for end in (fn0, fn1):
        touched = 0
        for k, (n0, n1, s) in enumerate(arcs):
            if k == idx or end not in (n0, n1):
                continue
            c, R = circle(pts[n0], pts[n1], s)
            d = hypot(c[0] - fc[0], c[1] - fc[1])
            err = min(abs(d - abs(R - fR)), abs(d - (R + fR)))
            check("%s: tangent to arc at n%d" % (label, end), err < tol,
                  "centre distance %.9f, |R-r| %.9f, R+r %.9f" % (d, abs(R - fR), R + fR))
            touched += 1
        for k, (n0, n1) in enumerate(segs):
            if end not in (n0, n1):
                continue
            (x0, y0), (x1, y1) = pts[n0], pts[n1]
            ln = hypot(x1 - x0, y1 - y0)
            d = abs((x1 - x0) * (y0 - fc[1]) - (x0 - fc[0]) * (y1 - y0)) / ln
            check("%s: tangent to line at n%d" % (label, end), abs(d - fR) < tol,
                  "perpendicular distance %.9f, want %.9f" % (d, fR))
            touched += 1
        # A fillet endpoint attached to nothing means the corner was replaced
        # by a floating arc -- the shape is open even though the radius looks
        # right in isolation.
        check("%s: fillet endpoint n%d is attached" % (label, end), touched > 0)


def build(name, body, corner, r):
    """Build a model, fillet one corner, and hand back the saved geometry."""
    newdocument(0)
    body()
    ok = True
    try:
        mi_createradius(corner[0], corner[1], r)
    except Exception as e:
        ok = False
        check("%s: mi_createradius succeeded" % name, False, str(e))
    mi_saveas(name + ".fem")
    mi_close()
    if ok:
        check_fillet(name, name + ".fem", r)
    return name + ".fem"


written = []

# --- two lines ---------------------------------------------------------
# A right angle at the origin. The one case the rewrite got right, kept so a
# future change cannot break it unnoticed.
def two_lines():
    mi_addnode(0, 0)
    mi_addnode(10, 0)
    mi_addnode(0, 10)
    mi_addsegment(0, 0, 10, 0)
    mi_addsegment(0, 0, 0, 10)


written.append(build("tmp_rad_lines", two_lines, (0, 0), 2.0))

# --- a line and an arc -------------------------------------------------
# The case the bug report hit. The arc's swept angle must SHRINK when its
# endpoint moves to the tangency point, or it lands on a different circle.
def line_and_arc():
    mi_addnode(0, 0)
    mi_addnode(-10, 0)
    mi_addnode(6, 8)
    mi_addsegment(0, 0, -10, 0)
    mi_addarc(0, 0, 6, 8, 60, 1)


written.append(build("tmp_rad_linearc", line_and_arc, (0, 0), 1.5))

# --- two arcs ----------------------------------------------------------
def two_arcs():
    mi_addnode(0, 0)
    mi_addnode(8, 6)
    mi_addnode(-8, 6)
    mi_addarc(0, 0, 8, 6, 50, 1)
    mi_addarc(-8, 6, 0, 0, 50, 1)


written.append(build("tmp_rad_arcs", two_arcs, (0, 0), 1.0))

# --- a corner that is not a corner -------------------------------------
# Three curves at a node has no unique fillet; FEMM refuses. The refusal also
# guards a two-element array that the original FEMM fills before testing how
# many curves it found.
newdocument(0)
mi_addnode(0, 0)
mi_addnode(10, 0)
mi_addnode(0, 10)
mi_addnode(-10, -10)
mi_addsegment(0, 0, 10, 0)
mi_addsegment(0, 0, 0, 10)
mi_addsegment(0, 0, -10, -10)
refused = False
try:
    mi_createradius(0, 0, 1.0)
except Exception:
    refused = True
check("three curves at a node is refused", refused)
mi_close()

for f in written:
    for ext in ("", ".ans"):
        try:
            os.remove(f + ext)
        except OSError:
            pass

if fails:
    for m in fails:
        print("   " + m)
    print("CreateRadius: %d FAILED" % len(fails))
else:
    print("create radius check: the fillet is tangent to both curves in all "
          "three cases (line+line, line+arc, arc+arc), the touched arc keeps "
          "the circle it was on, and a node with three curves is refused")
