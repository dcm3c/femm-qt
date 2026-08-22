# MagDirTransformTest.py
# A permanent magnet's magnetization direction must follow the geometry.
#
#     build/bin/femm_headless -run testing/MagDirTransformTest.py
#
# Rotating or mirroring a magnet moves the body; if MagDir does not come along,
# the material ends up somewhere new with its field still pointing the old way.
# Reported from the field, and it had been missing since the port: OF does it in
# six places (MOVECOPY.CPP:663, 726, 913, 1051, 1949, 2047) and femm-qt did it
# in none.
#
# The guard is coercivity, as in OF: MagDir is meaningless for a material that
# is not a permanent magnet, and turning it there would dirty the file for no
# reason. So every case below is checked twice -- once on a magnet, which must
# move, and once on air, which must not.
#
# mi_getblockprop is not a command, so the values are read back from a saved
# .fem rather than queried. That also checks the value survives a save/load,
# which is what a user would actually notice.

import os

fails = []


def check(label, got, want, tol=1e-9):
    if abs(got - want) > tol:
        fails.append("%s: got %.6f, want %.6f" % (label, got, want))


def labels(path):
    """(x, y, MagDir) for every block label in a saved .fem, in file order."""
    out = []
    fh = _builtin_open(path, "r")
    try:
        line = fh.readline()
        while line and not line.strip().lower().startswith("[numblocklabels]"):
            line = fh.readline()
        n = int(line.split("=")[1])
        for _ in range(n):
            f = fh.readline().split()
            # x  y  BlockType  MaxArea  InCircuit  MagDir  InGroup  Turns ...
            out.append((float(f[0]), float(f[1]), float(f[5])))
        return out
    finally:
        fh.close()


def build(magnet_first=True):
    """Two labels on a 40 mm square: one magnet, one air, both at MagDir 10."""
    newdocument(0)
    mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)   # magnetics takes frequency first
    mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
    # H_c = 900000 A/m makes this a permanent magnet; that is the only field
    # the guard looks at.
    mi_addmaterial("NdFeB", 1.05, 1.05, 900000, 0, 0.667, 0, 0, 1, 0, 0, 0)
    for (x, y) in ((0, 0), (40, 0), (40, 40), (0, 40)):
        mi_addnode(x, y)
    for (a, b) in (((0, 0), (40, 0)), ((40, 0), (40, 40)),
                   ((40, 40), (0, 40)), ((0, 40), (0, 0))):
        mi_addsegment(a[0], a[1], b[0], b[1])
    mi_addblocklabel(10, 10)
    mi_selectlabel(10, 10)
    mi_setblockprop("NdFeB", 1, 0, "<None>", 10, 0, 0)
    mi_clearselected()
    mi_addblocklabel(30, 30)
    mi_selectlabel(30, 30)
    mi_setblockprop("Air", 1, 0, "<None>", 10, 0, 0)
    mi_clearselected()


def save_and_read(tag):
    p = "_magdir_%s.fem" % tag
    mi_saveas(p)
    got = labels(p)
    os.remove(p)
    return got


# --- rotate in place -------------------------------------------------------
build()
mi_selectlabel(10, 10)
mi_selectlabel(30, 30)
mi_moverotate(0, 0, 35)          # 35 degrees about the origin
mi_clearselected()
got = save_and_read("rot")
check("rotate: magnet MagDir follows", got[0][2], 45.0)   # 10 + 35
check("rotate: air MagDir untouched",  got[1][2], 10.0)
mi_close()

# --- rotate-copy, three copies of a 30 degree sweep ------------------------
build()
mi_selectlabel(10, 10)
mi_selectlabel(30, 30)
mi_copyrotate(0, 0, 30, 3)
mi_clearselected()
got = save_and_read("copyrot")
# original magnet, original air, then the copies in sweep order
check("copyrotate: original magnet unchanged", got[0][2], 10.0)
check("copyrotate: original air unchanged",    got[1][2], 10.0)
mag = [g[2] for g in got[2:] if abs(g[2] - 10.0) > 1e-9]
if sorted(mag) != [40.0, 70.0, 100.0]:
    fails.append("copyrotate: magnet copies at %s, want [40.0, 70.0, 100.0] "
                 "(10 + 30, 60, 90 -- each copy's own cumulative angle, not "
                 "the per-copy step)" % sorted(mag))
air = [g[2] for g in got[2:] if abs(g[2] - 10.0) <= 1e-9]
check("copyrotate: air copies untouched", len(air), 3)
mi_close()

# --- mirror about the y axis ------------------------------------------------
# Reflecting a direction of +10 degrees in the vertical line x = 20 gives
# 180 - 10 = 170.  A sign error here points a magnet backwards, which is worse
# than an angle error, so it is worth pinning separately.
build()
mi_selectlabel(10, 10)
mi_selectlabel(30, 30)
mi_mirror(20, 0, 20, 40)
mi_clearselected()
got = save_and_read("mirror")
check("mirror: original magnet unchanged", got[0][2], 10.0)
mirrored = [g[2] for g in got[2:] if abs(g[2] - 10.0) > 1e-9]
if len(mirrored) != 1:
    fails.append("mirror: expected one magnet copy with a changed MagDir, "
                 "got %s" % mirrored)
else:
    check("mirror: magnet MagDir reflected", mirrored[0], 170.0, 1e-6)
mi_close()

if fails:
    for m in fails:
        print("   " + m)
    print("magdir transform: %d FAILED" % len(fails))
else:
    print("magdir transform: MagDir follows rotate, rotate-copy and mirror, "
          "and is left alone on materials that are not permanent magnets")
