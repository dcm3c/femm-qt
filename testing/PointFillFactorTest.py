# PointFillFactorTest.py -- mo_getpointvalues' ff (index 13) in every kind of
# region.
#
#     build/bin/femm_headless -run testing/PointFillFactorTest.py
#
# Reported from the field: ff read -1 over most of a model, laminated iron
# included, although the manual describes it as the fill factor for laminated
# materials. A block label's FillFactor is the winding fill only for a wire
# material (LamType >= 3); for anything else it is OF's eddy-current flag, -1
# for a solid region and 1 for a multi-turn one, and getpointvalues returned it
# as-is. Now a laminated region reports its lamination fill. Wire regions and
# non-laminated regions keep exactly what they returned before, OF's values.
# The flag itself is unchanged and still decides where eddy currents flow:
# checked by Je in the three copper regions at 60 Hz.
#
# One 10 mm square per case in an air box, probed at the centres, DC and 60 Hz.
# Only the conductors carry current -- a laminated region is an open circuit
# along the problem's depth -- and on-edge lamination runs at DC only, since
# the AC solver rejects it.

import math

fails = []

S = 10.0                  # square side, mm
WIRE_D = 1.0              # magnet wire diameter, mm
WIRE_TURNS = 50
WIRE_FILL = WIRE_TURNS * math.pi * (WIRE_D / 2) ** 2 / (S * S)

# (block name, material args after the name, turns or 0 for no circuit,
#  expected ff, DC only)
CASES = [
    # in-plane laminated: thickness and fill
    ("lam_inplane", (1000, 1000, 0, 0, 2, 0.5, 0, 0.9, 0), 0, 0.9, False),
    # in-plane, fill below 1 but no thickness
    ("lam_fillonly", (1000, 1000, 0, 0, 2, 0, 0, 0.8, 0), 0, 0.8, False),
    # laminated along x (on edge): no AC solve
    ("lam_x", (1000, 1000, 0, 0, 2, 0, 0, 0.95, 1), 0, 0.95, True),
    # magnet wire: the winding fill, as before
    ("wire", (1, 1, 0, 0, 58, 0, 0, 1, 3, 0, 0, 1, WIRE_D), WIRE_TURNS,
     WIRE_FILL, False),
    # plain multi-turn copper: OF's flag, 1
    ("cu_turns", (1, 1, 0, 0, 58, 0, 0, 1, 0), 10, 1.0, False),
    # solid copper: OF's flag, -1
    ("cu_solid", (1, 1, 0, 0, 58, 0, 0, 1, 0), 1, -1.0, False),
    # solid unlaminated iron: OF's flag, -1
    ("iron", (1000, 1000, 0, 0, 2, 0, 0, 1, 0), 0, -1.0, False),
]

# Je at 60 Hz in the driven conductors: only the solid one carries eddy current.
EDDY = {"wire": False, "cu_turns": False, "cu_solid": True}


def centre(i):
    return (i * 2 * S + S / 2, S / 2)


def build(freq, cases):
    newdocument(0)
    mi_probdef(freq, "millimeters", "planar", 1e-8, 1, 30)
    mi_addmaterial("Air", 1, 1)
    mi_addboundprop("A0", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for name, args, turns, want, dconly in cases:
        mi_addmaterial(name, *args)
    x0, y0, x1, y1 = -S, -S, 2 * len(cases) * S, 2 * S
    mi_drawrectangle(x0, y0, x1, y1)
    for x, y in (((x0 + x1) / 2, y0), ((x0 + x1) / 2, y1), (x0, (y0 + y1) / 2),
                 (x1, (y0 + y1) / 2)):
        mi_selectsegment(x, y)
    mi_setsegmentprop("A0", 0, 1, 0, 0)
    mi_clearselected()
    mi_addblocklabel(x0 + S / 2, y0 + S / 4)
    mi_selectlabel(x0 + S / 2, y0 + S / 4)
    mi_setblockprop("Air", 1, 0, "<None>", 0, 0, 1)
    mi_clearselected()
    for i, (name, args, turns, want, dconly) in enumerate(cases):
        cx, cy = centre(i)
        mi_drawrectangle(cx - S / 2, cy - S / 2, cx + S / 2, cy + S / 2)
        mi_addblocklabel(cx, cy)
        mi_selectlabel(cx, cy)
        if turns:
            mi_addcircprop("c%d" % i, 1, 1)
            mi_setblockprop(name, 1, 0, "c%d" % i, 0, 0, turns)
        else:
            mi_setblockprop(name, 1, 0, "<None>", 0, 0, 1)
        mi_clearselected()
    mi_saveas("_pointfill.fem")
    mi_analyze()
    mi_loadsolution()


for freq in (0, 60):
    cases = [c for c in CASES if freq == 0 or not c[4]]
    build(freq, cases)
    for i, (name, args, turns, want, dconly) in enumerate(cases):
        v = mo_getpointvalues(*centre(i))
        ff = v[13]
        if abs(ff - want) > 1e-6 * max(1.0, abs(want)):
            fails.append("%d Hz %s: ff = %.9g, want %.9g" % (freq, name, ff, want))
        if freq and name in EDDY:
            je = abs(v[7])
            if EDDY[name] and je == 0:
                fails.append("%d Hz %s: no eddy current in a solid conductor"
                             % (freq, name))
            if not EDDY[name] and je != 0:
                fails.append("%d Hz %s: eddy current %.3g in a winding"
                             % (freq, name, je))
    mo_close()
    mi_close()

import os
for ext in (".fem", ".ans"):
    try:
        os.remove("_pointfill" + ext)
    except OSError:
        pass

if fails:
    for m in fails:
        print("   " + m)
    print("point fill factor: %d FAILED" % len(fails))
else:
    print("point fill factor: ff is the lamination fill in laminated regions, the "
          "winding fill in wire regions and OF's -1/1 elsewhere, and eddy "
          "current still flows only in the solid conductor")
