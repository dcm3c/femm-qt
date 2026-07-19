# MOCircuitPropTest.py
# Exercises mo_getcircuitproperties (current, voltage drop, flux linkage) across
# the magnetics block-integral fixtures and excitations -- the field-to-circuit
# terminal quantities (inductance = Flux/I, impedance = V/I, ...). Mirrors
# MOBlockIntegralTest.py's fixtures at DC, AC 60Hz, and incrementally:
#   RadialMagneticBearing (i1-i4), roters1b (Coil Current), torque (i1,i2),
#   the three Lorentz coils (i1). PMTest has no circuit, so it is omitted.
#
# Each circuit prints "I | V | Flux", three "re im" pairs, keyed by circuit name
# so MOCircuitPropCompare.py diffs field-for-field against Original FEMM.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def report(circuits, tag):
    mi_analyze(); mi_loadsolution()
    print("===  " + tag + "  ===")
    for c in circuits:
        I, V, F = mo_getcircuitproperties(c)
        print("  %-14s = %s | %s | %s" % (c, fmt(I), fmt(V), fmt(F)))
    mo_close()

def rmb(i1, i2, i3, i4):
    mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)

RMBC = ["i1", "i2", "i3", "i4"]

# ---- RadialMagneticBearing (i1-i4) ----
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(12, 6, 0, 6); mi_probdef(0, "inches", "planar")
report(RMBC, "RMB (x, DC) i=(12,6,0,6)")
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(12, 6, 0, 6); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (x, AC 60Hz) i=(12,6,0,6)")
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(0, "inches", "planar")
report(RMBC, "RMB (y, DC) i=(6,12,6,0)")
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (y, AC 60Hz) i=(6,12,6,0)")
open("RadialMagneticBearing.fem"); mi_saveas("bias.fem"); rmb(6, 6, 6, 6); mi_probdef(0, "inches", "planar"); mi_analyze()
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); rmb(1, 0, -1, 0); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)")

# ---- roters1b (Coil Current) ----
def rot(a): mi_modifycircprop("Coil Current", 1, a)
ROTC = ["Coil Current"]
open("roters1b.fem"); mi_saveas("rtemp.fem"); rot(0.8); mi_probdef(0, "inches", "axi")
report(ROTC, "roters1b (DC) 0.8A")
open("roters1b.fem"); mi_saveas("rtemp.fem"); rot(0.8); mi_probdef(60, "inches", "axi")
report(ROTC, "roters1b (AC 60Hz) 0.8A")
open("roters1b.fem"); mi_saveas("rbias.fem"); rot(0.8); mi_probdef(0, "inches", "axi"); mi_analyze()
mi_saveas("rincr.fem"); mi_setprevious("rbias.ans", 1); rot(0.1); mi_probdef(60, "inches", "axi")
report(ROTC, "roters1b (incremental) bias 0.8A; AC 0.1A")

# ---- torque (i1, i2) ----
def torq(a, b): mi_modifycircprop("i1", 1, a); mi_modifycircprop("i2", 1, b)
TQC = ["i1", "i2"]
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0); mi_probdef(0, "inches", "planar")
report(TQC, "torque (DC) i=(2,0)")
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0); mi_probdef(60, "inches", "planar")
report(TQC, "torque (AC 60Hz) i=(2,0)")
open("torque.fem"); mi_saveas("bias.fem"); torq(1, 1); mi_probdef(0, "inches", "planar"); mi_analyze()
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); torq(1, -1); mi_probdef(60, "inches", "planar")
report(TQC, "torque (incremental) bias i=(1,1); AC i=(1,-1)")

# ---- Lorentz coils (i1) ----
def lor(a): mi_modifycircprop("i1", 1, a)
for (fem, ptype, tag) in [("AxisymmetricLorentzForce.fem", "axi", "AxiLorentz"),
                          ("PlanarLorentzForce.fem", "planar", "PlanarLorentz-y"),
                          ("PlanarLorentzForceAlt.fem", "planar", "PlanarLorentz-x")]:
    open(fem); mi_saveas("temp.fem"); lor(16); mi_probdef(0, "inches", ptype)
    report(["i1"], tag + " (DC) 16A")
    open(fem); mi_saveas("temp.fem"); lor(16); mi_probdef(60, "inches", ptype)
    report(["i1"], tag + " (AC 60Hz) 16A")

print("\nDone.")
