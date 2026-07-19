# MOPointValueTest.py
# Exercises mo_getpointvalues (14 values: A, B1, B2, sigma, E, H1, H2, Je, Js,
# mu1, mu2, Pe, Ph, ff) at hand-picked interior points across the magnetics
# fixtures, covering every material type: air gap, high-flux iron, coil, magnet.
# Points sit well inside their region (away from interfaces/corners) so both the
# tangle and Triangle meshes interpolate nearly the same value. Coil/magnet points
# are block-label positions; iron/gap points were chosen in high-flux locations.
# DC exercises the static field; AC 60Hz adds the complex B/H, eddy current Je,
# and the loss densities Pe/Ph.
#
# Each point prints its 14 values as "re im" pairs joined by " | ", keyed by a
# material-type label, for MOPointValueCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def report(points, tag):
    mi_analyze(); mi_loadsolution()
    print("===  " + tag + "  ===")
    for (label, x, y) in points:
        v = mo_getpointvalues(x, y)
        if v is None:
            print("  %-14s = (no mesh)" % label)
        else:
            print("  %-14s = %s" % (label, " | ".join(fmt(c) for c in v)))
    mo_close()

def rmb(i1, i2, i3, i4):
    mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)

RMB_PTS = [("coilA", 1.313, 0.933), ("coilB", 1.563, -0.273),
           ("rotor-iron", 0, 0.9), ("stator-pole", -0.47, 1.17),
           ("air-gap", 0.395674, 0.942145)]
ROT_PTS = [("coil", 1.125, 3.75), ("armature-iron", 1.65633, 4.64947),
           ("gap", 0.610868, 5.71208)]
TQ_PTS  = [("coilA", 1.665, 1.915), ("coilB", 2.795, 1.585),
           ("rotor-iron", 3, 0), ("stator-iron", 3, 3), ("air-gap", 3.75, 0.275)]
PM_PTS  = [("magnetL", -15, -10), ("magnetR", 15, -10), ("air-mid", 0, 0),
           ("keeper", 0, -20), ("air-gap", 15, 0)]

# ---- RadialMagneticBearing (y-excite) ----
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(0, "inches", "planar")
report(RMB_PTS, "RMB (DC, y-excite)")
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(60, "inches", "planar")
report(RMB_PTS, "RMB (AC 60Hz, y-excite)")

# ---- roters1b ----
open("roters1b.fem"); mi_saveas("rtemp.fem"); mi_modifycircprop("Coil Current", 1, 0.8); mi_probdef(0, "inches", "axi")
report(ROT_PTS, "roters1b (DC, 0.8A)")
open("roters1b.fem"); mi_saveas("rtemp.fem"); mi_modifycircprop("Coil Current", 1, 0.8); mi_probdef(60, "inches", "axi")
report(ROT_PTS, "roters1b (AC 60Hz, 0.8A)")

# ---- torque ----
def torq(a, b): mi_modifycircprop("i1", 1, a); mi_modifycircprop("i2", 1, b)
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0); mi_probdef(0, "inches", "planar")
report(TQ_PTS, "torque (DC, i=(2,0))")
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0); mi_probdef(60, "inches", "planar")
report(TQ_PTS, "torque (AC 60Hz, i=(2,0))")

# ---- PMTest (permanent magnets, static -> DC only) ----
open("PMTest.fem"); mi_saveas("temp.fem"); mi_probdef(0, "centimeters", "planar")
report(PM_PTS, "PMTest (DC, magnets)")

print("\nDone.")
