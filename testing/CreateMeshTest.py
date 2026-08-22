# *_createmesh: meshes real geometry, refuses degenerate geometry, crashes on
# neither.
#
#     build/bin/femm_headless -run testing/CreateMeshTest.py
#
# The four *_createmesh commands had no coverage at all until 2026-08-22, and
# what that hid was a SEGFAULT: a document with fewer than three points took
# tangle's buildDelaunay straight into pts[0] on an empty vector. A hard crash,
# not a catchable error -- a GUI user lost their work. Fixed in tangle 0.4.18,
# which rejects such input with TANGLE_ERR_MESH (4) and says why on stderr.
#
# Two traps are pinned here deliberately:
#
#   * An ALL-ARC model has no straight segments at all, and is perfectly valid.
#     A degenerate-input guard written against the .fem's [NumSegments] would
#     reject every circular model in the library. It is safe because readFemFile
#     discretises arcs into mesh.segments before the guard sees them -- this
#     test is what keeps that true.
#
#   * A guard that refuses everything would pass every negative case, so each
#     physics also has to mesh something real.

fails = []


def refuses(label, fn):
    """Degenerate input must raise, not crash. If it crashes, this script never
    reaches its summary -- which is itself the signal."""
    try:
        fn()
        fails.append("%s: createmesh accepted a document with no geometry" % label)
    except Exception as e:
        if "Mesher failed" not in str(e):
            fails.append("%s: refused, but not as a mesher failure: %r"
                         % (label, str(e)[:110]))


def meshes(label, fn):
    try:
        fn()
    except Exception as e:
        fails.append("%s: createmesh refused valid geometry: %r"
                     % (label, str(e)[:110]))


import tempfile

D = tempfile.mkdtemp()

# --- empty documents: the case that used to segfault ------------------------
for label, doctype, ext, saveas, mesh, close in (
        ("magnetics", 0, ".fem", lambda p: mi_saveas(p),
         lambda: mi_createmesh(), lambda: mi_close()),
        ("electrostatics", 1, ".fee", lambda p: ei_saveas(p),
         lambda: ei_createmesh(), lambda: ei_close()),
        ("heat flow", 2, ".feh", lambda p: hi_saveas(p),
         lambda: hi_createmesh(), lambda: hi_close()),
        ("current flow", 3, ".fec", lambda p: ci_saveas(p),
         lambda: ci_createmesh(), lambda: ci_close())):
    newdocument(doctype)
    saveas(D + "/empty" + ext)
    refuses(label + " (empty)", mesh)
    close()

# --- one and two points: still fewer than a triangle needs ------------------
newdocument(0)
mi_addnode(0.0, 0.0)
mi_saveas(D + "/one.fem")
refuses("magnetics (1 point)", lambda: mi_createmesh())
mi_close()

newdocument(0)
mi_addnode(0.0, 0.0)
mi_addnode(1.0, 0.0)
mi_addsegment(0.0, 0.0, 1.0, 0.0)
mi_saveas(D + "/two.fem")
refuses("magnetics (2 points, 1 segment)", lambda: mi_createmesh())
mi_close()

# --- a closed region meshes, in every physics -------------------------------
def square(add_node, add_seg):
    for x, y in ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)):
        add_node(x, y)
    add_seg(0.0, 0.0, 1.0, 0.0)
    add_seg(1.0, 0.0, 1.0, 1.0)
    add_seg(1.0, 1.0, 0.0, 1.0)
    add_seg(0.0, 1.0, 0.0, 0.0)


for label, doctype, ext, node, seg, saveas, mesh, close in (
        ("magnetics", 0, ".fem", lambda x, y: mi_addnode(x, y),
         lambda a, b, c, d: mi_addsegment(a, b, c, d), lambda p: mi_saveas(p),
         lambda: mi_createmesh(), lambda: mi_close()),
        ("electrostatics", 1, ".fee", lambda x, y: ei_addnode(x, y),
         lambda a, b, c, d: ei_addsegment(a, b, c, d), lambda p: ei_saveas(p),
         lambda: ei_createmesh(), lambda: ei_close()),
        ("heat flow", 2, ".feh", lambda x, y: hi_addnode(x, y),
         lambda a, b, c, d: hi_addsegment(a, b, c, d), lambda p: hi_saveas(p),
         lambda: hi_createmesh(), lambda: hi_close()),
        ("current flow", 3, ".fec", lambda x, y: ci_addnode(x, y),
         lambda a, b, c, d: ci_addsegment(a, b, c, d), lambda p: ci_saveas(p),
         lambda: ci_createmesh(), lambda: ci_close())):
    newdocument(doctype)
    square(node, seg)
    saveas(D + "/square" + ext)
    meshes(label + " (closed square)", mesh)
    close()

# --- all arcs, no straight segments -----------------------------------------
newdocument(0)
for x, y in ((1.0, 0.0), (0.0, 1.0), (-1.0, 0.0), (0.0, -1.0)):
    mi_addnode(x, y)
mi_addarc(1.0, 0.0, 0.0, 1.0, 90, 1)
mi_addarc(0.0, 1.0, -1.0, 0.0, 90, 1)
mi_addarc(-1.0, 0.0, 0.0, -1.0, 90, 1)
mi_addarc(0.0, -1.0, 1.0, 0.0, 90, 1)
mi_saveas(D + "/circle.fem")
meshes("magnetics (circle of four arcs, no straight segments)",
       lambda: mi_createmesh())
mi_close()

if fails:
    for m in fails:
        print("   " + m)
    print("createmesh: %d FAILED" % len(fails))
else:
    print("createmesh check: degenerate input is refused rather than crashing "
          "(empty, 1 point, 2 points) in all four physics, a closed region "
          "meshes, and so does a model built entirely from arcs")
