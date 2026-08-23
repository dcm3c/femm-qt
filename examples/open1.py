"""open1.py -- rebuild open1.fem, the Kelvin Transformation example.

The model is the one described in Appendix A.3.3 of the FEMM manual and
pictured in figures open1 and open1b. The .fem itself had gone missing from
the distribution; this script reconstructs it, so it cannot go missing again.

Stated in the manual text:
    centre leg 0.5", outer legs 0.25", slot depth 0.75",
    core is linear with relative permeability 2500,
    coil bulk current density 2 MA/m^2.

Recovered from figure open1.png, which renders at exactly 70 px per inch:
    slots 0.5" wide, yoke 0.25" thick, so the E-core is 2.0" x 1.0";
    each circular region has radius 1.5" and is centred on the core;
    each circle is split at its left and right extremes into a top and a
    bottom semicircle -- which is what the two periodic boundary conditions
    are applied to.

The separation between the two circles is the one quantity that carries no
physics: the regions are joined by the periodic boundary conditions, not by
proximity. 3.5" keeps them clear of each other and close to the figure.
"""
import os

import femm

# This example runs under CPython through pyfemm, not inside femm-qt,
# so nothing has chdir'd anywhere: a bare relative path would land
# wherever python happened to be started. Write beside the script.
HERE = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")

R = 1.5           # radius of each circular region, inches
SEP = 3.5         # centre-to-centre spacing of the two regions
JCOIL = 2.0       # coil current density, MA/m^2
MUR = 2500        # relative permeability of the core

femm.openfemm(1)
femm.newdocument(0)
femm.mi_probdef(0, "inches", "planar", 1e-8, 1, 30)

# ---- E-core outline, centred on the origin ------------------------------
# Yoke spans y = -0.5 .. -0.25; the three legs rise from there to y = +0.5.
core = [(-1.0, -0.5), (1.0, -0.5), (1.0, 0.5), (0.75, 0.5), (0.75, -0.25),
        (0.25, -0.25), (0.25, 0.5), (-0.25, 0.5), (-0.25, -0.25),
        (-0.75, -0.25), (-0.75, 0.5), (-1.0, 0.5)]
for x, y in core:
    femm.mi_addnode(x, y)
for i in range(len(core)):
    x1, y1 = core[i]
    x2, y2 = core[(i + 1) % len(core)]
    femm.mi_addsegment(x1, y1, x2, y2)

# Close the top of each slot, so the coils are bounded regions.
femm.mi_addsegment(-0.75, 0.5, -0.25, 0.5)
femm.mi_addsegment(0.25, 0.5, 0.75, 0.5)

# ---- the two circular regions -------------------------------------------
# Split at the left and right extremes; each half is a 180 degree arc drawn
# counter-clockwise, so the two circles are traversed identically and the
# periodic boundary matches interior angle theta to exterior angle theta.
for cx in (0.0, SEP):
    femm.mi_addnode(cx - R, 0)
    femm.mi_addnode(cx + R, 0)
    femm.mi_addarc(cx + R, 0, cx - R, 0, 180, 2.5)   # top half
    femm.mi_addarc(cx - R, 0, cx + R, 0, 180, 2.5)   # bottom half

# The centre of the exterior region is the image of infinity, so it is where
# the potential reference belongs. Without it the solution is unique only to
# within a constant.
femm.mi_addnode(SEP, 0)

# ---- materials ----------------------------------------------------------
femm.mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
femm.mi_addmaterial("Iron", MUR, MUR, 0, 0, 0, 0, 0, 1, 0, 0, 0)
femm.mi_addmaterial("J+", 1, 1, 0, JCOIL, 0, 0, 0, 1, 0, 0, 0)
femm.mi_addmaterial("J-", 1, 1, 0, -JCOIL, 0, 0, 0, 1, 0, 0, 0)

# ---- boundary and point properties --------------------------------------
# BdryFormat 4 = periodic. Each periodic condition links exactly two entities,
# so the two halves of the boundary need one condition each.
femm.mi_addboundprop("Periodic BC 1", 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0)
femm.mi_addboundprop("Periodic BC 2", 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0)
femm.mi_addpointprop("A=0", 0, 0)

for cx in (0.0, SEP):
    femm.mi_selectarcsegment(cx, R)          # top half of this circle
femm.mi_setarcsegmentprop(2.5, "Periodic BC 1", 0, 0)
femm.mi_clearselected()

for cx in (0.0, SEP):
    femm.mi_selectarcsegment(cx, -R)         # bottom half
femm.mi_setarcsegmentprop(2.5, "Periodic BC 2", 0, 0)
femm.mi_clearselected()

femm.mi_selectnode(SEP, 0)
femm.mi_setnodeprop("A=0", 0)
femm.mi_clearselected()

# ---- block labels -------------------------------------------------------
for x, y, mat in ((0.0, 0.0, "Iron"),        # centre leg
                  (-0.5, 0.0, "J+"),         # left slot
                  (0.5, 0.0, "J-"),          # right slot
                  (0.0, 1.0, "Air"),         # inside the interior circle
                  (SEP, 1.0, "Air")):        # inside the exterior circle
    femm.mi_addblocklabel(x, y)
    femm.mi_selectlabel(x, y)
    femm.mi_setblockprop(mat, 1, 0, "<None>", 0, 0, 0)
    femm.mi_clearselected()

femm.mi_saveas(HERE + "/open1.fem")
print("wrote " + HERE + "/open1.fem")
femm.closefemm()
