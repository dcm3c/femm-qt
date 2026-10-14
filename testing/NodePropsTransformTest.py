# NodePropsTransformTest.py
# A node's properties must survive any move or copy -- of itself or of anything
# else in the model.
#
#     build/bin/femm_headless -run testing/NodePropsTransformTest.py
#
# Reported from the field: assign a point to a group, translate anything, and
# every point's group reads 0. Every transform ends in EnforcePSLG(), which
# rebuilds the node list by re-adding each node -- and AddNode(FNode&) re-added
# it from its coordinates alone, so every node, moved or not, came back with
# its group, point property, conductor and local mesh size reset. OF's
# AddNode(CNode&) adds the node itself (bd_movecopy.cpp:1308).
#
# So each case checks ALL nodes, not just the selected one, and the copy cases
# check that the copies carry the original's properties too. There is no
# node-property getter, so the values are read back from a saved file, which is
# also where a user would notice.

import os

fails = []


def points(path, ncols):
    """Point records of a saved file, as tuples rounded for comparison."""
    out = []
    fh = _builtin_open(path, "r")
    try:
        line = fh.readline()
        while line and not line.strip().lower().startswith("[numpoints]"):
            line = fh.readline()
        n = int(line.split("=")[1])
        for _ in range(n):
            f = fh.readline().split()
            # magnetics:       x  y  prop  group  [lfs]
            # electrostatics:  x  y  prop  group  conductor  lfs
            out.append(tuple([round(float(f[0]), 6), round(float(f[1]), 6)] +
                             [int(v) for v in f[2:2 + ncols]]))
        return sorted(out)
    finally:
        fh.close()


def expect(label, got, want):
    if got != sorted(want):
        fails.append("%s:\n      got  %s\n      want %s" % (label, got, sorted(want)))


# Three nodes on the x axis. A carries a point property and group 3, B (the one
# that gets selected) group 5, C group 7. Nothing selected stays put; B moves.
A, B, C = (0.0, 0.0), (10.0, 0.0), (30.0, 0.0)
PA, PB, PC = (1, 3), (0, 5), (0, 7)       # (point-property index, group)


def build_mag():
    newdocument(0)
    mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    mi_addpointprop("P1", 0, 1)
    for (x, y), (prop, grp) in ((A, PA), (B, PB), (C, PC)):
        mi_addnode(x, y)
        mi_selectnode(x, y)
        mi_setnodeprop("P1" if prop else "<None>", grp)
        mi_clearselected()
    mi_selectnode(B[0], B[1])


def run_mag(label, op, b_where):
    """b_where: where B's properties must appear afterwards (one or more points)."""
    build_mag()
    op()
    mi_clearselected()
    p = "_nodeprops_mag.fem"
    mi_saveas(p)
    got = points(p, 2)
    os.remove(p)
    mi_close()
    want = [A + PA, C + PC] + [w + PB for w in b_where]
    expect("magnetics " + label, got, want)


run_mag("translate", lambda: mi_movetranslate(3, 4), [(13.0, 4.0)])
run_mag("rotate", lambda: mi_moverotate(0, 0, 90), [(0.0, 10.0)])
run_mag("scale", lambda: mi_scale(0, 0, 2), [(20.0, 0.0)])
run_mag("mirror (copy)", lambda: mi_mirror(0, 5, 1, 5), [B, (10.0, 10.0)])
run_mag("copy translate", lambda: mi_copytranslate(0, 10, 2),
        [B, (10.0, 10.0), (10.0, 20.0)])
run_mag("copy rotate", lambda: mi_copyrotate(0, 0, 90, 1), [B, (0.0, 10.0)])

# Electrostatics carries one more property on a node: the conductor.
EB = (0, 5, 1)                            # (prop, group, conductor index)


def run_elec(label, op, b_where):
    newdocument(1)
    ei_probdef("millimeters", "planar", 1e-8, 1, 30)
    ei_addconductorprop("C1", 10, 0, 1)
    for (x, y), grp, cond in ((A, 3, ""), (B, 5, "C1"), (C, 7, "")):
        ei_addnode(x, y)
        ei_selectnode(x, y)
        ei_setnodeprop("<None>", grp, cond if cond else "<None>")
        ei_clearselected()
    ei_selectnode(B[0], B[1])
    op()
    ei_clearselected()
    p = "_nodeprops_elec.fee"
    ei_saveas(p)
    got = points(p, 3)
    os.remove(p)
    ei_close()
    want = [A + (0, 3, 0), C + (0, 7, 0)] + [w + EB for w in b_where]
    expect("electrostatics " + label, got, want)


run_elec("translate", lambda: ei_movetranslate(3, 4), [(13.0, 4.0)])
run_elec("copy translate", lambda: ei_copytranslate(0, 10, 1), [B, (10.0, 10.0)])

if fails:
    for m in fails:
        print("   " + m)
    print("node props: %d FAILED" % len(fails))
else:
    print("node props: group, point property and conductor survive translate, "
          "rotate, scale, mirror and copy -- on the node moved and on every "
          "node that was not")
