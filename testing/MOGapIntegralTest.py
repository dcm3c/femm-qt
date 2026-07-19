# MOGapIntegralTest.py
# Exercises the air-gap-element postprocessing (a planar-magnetics-only feature)
# on two fixtures with an air gap element boundary "AGE":
#   RadialMagneticBearingAGE.fem -- a radial bearing: strong gap FORCE, ~0 torque
#   rolled.fem                   -- a rotating machine: strong gap TORQUE
# Between them they exercise every gap force and torque case.
#
#   mo_gapintegral("AGE", t):  0 DC torque, 1 DC force, 2 stored energy,
#       3 2X torque, 4 2X force, 5 incremental torque, 6 incremental force.
#   mo_getgapharmonics("AGE")[,n]:  highest harmonic, or the 6 Fourier coeffs of n
#   mo_getgapb / mo_getgapa("AGE", ang):  (Bn,Bt) / A in the gap at ang degrees
#
# Each fixture runs at DC, AC 60Hz (adds the 2X parts), and incrementally (small-
# signal AC about a DC bias; types 5/6). Each component is printed as "re im" and
# lines are keyed by label so MOGapIntegralCompare.py diffs field-for-field
# against Original FEMM.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def emit(label, vals):
    print("  %-14s = %s" % (label, " | ".join(fmt(v) for v in vals)))

def gap_common():
    emit("nharm", (mo_getgapharmonics("AGE"),))
    for n in (1, 2, 3, 4):
        emit("harm%d" % n, mo_getgapharmonics("AGE", n))
    for ang in (0, 45, 90, 135):
        emit("gapB@%d" % ang, mo_getgapb("AGE", ang))
    emit("gapA@90", (mo_getgapa("AGE", 90),))

def report(gap_types, tag):
    mi_analyze(); mi_loadsolution()
    print("===  " + tag + "  ===")
    for (label, t) in gap_types:
        emit(label, mo_gapintegral("AGE", t))
    gap_common()
    mo_close()

STEADY = [("torque", 0), ("force", 1), ("energy", 2)]
AC     = STEADY + [("torque2x", 3), ("force2x", 4)]
INCR   = [("torqueinc", 5), ("forceinc", 6)]

def rmb(i1, i2, i3, i4):
    mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)

def rolled(a):
    mi_modifycircprop("icoil", 1, a)

# ============ RadialMagneticBearingAGE (force) ============
FEM, U = "RadialMagneticBearingAGE.fem", "inches"
open(FEM); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(0, U, "planar")
report(STEADY, "RMB-AGE (DC, y-excite)")
open(FEM); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(60, U, "planar")
report(AC, "RMB-AGE (AC 60Hz, y-excite)")
open(FEM); mi_saveas("bias.fem"); rmb(6, 6, 6, 6); mi_probdef(0, U, "planar"); mi_analyze()
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); rmb(0, 1, 0, -1); mi_probdef(60, U, "planar")
report(INCR, "RMB-AGE (Incremental AC, y-perturb)")

# ============ rolled (torque) ============
FEM, U = "rolled.fem", "millimeters"
open(FEM); mi_saveas("temp.fem"); rolled(20); mi_probdef(0, U, "planar")
report(STEADY, "rolled (DC, icoil=20)")
open(FEM); mi_saveas("temp.fem"); rolled(20); mi_probdef(60, U, "planar")
report(AC, "rolled (AC 60Hz, icoil=20)")
open(FEM); mi_saveas("bias.fem"); rolled(20); mi_probdef(0, U, "planar"); mi_analyze()
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); rolled(1); mi_probdef(60, U, "planar")
report(INCR, "rolled (Incremental AC, icoil=1)")

print("\nDone.")
