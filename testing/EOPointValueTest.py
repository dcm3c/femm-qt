# EOPointValueTest.py
# Exercises eo_getpointvalues (V, Dx, Dy, Ex, Ey, epsx, epsy, energy) at interior
# points on electrostatic1 (axi) and electrostatic2 (planar). The "+10000V"
# electrode is a boundary condition, so the domain is Air + the IABC open-boundary
# shells (e1..e7); points sample the bulk field near the source and one shell.
# Electrostatics is static -> 8 real values. Each point prints "re im" pairs keyed
# by a label, for ScalarPointValueCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def report(pts, tag):
    ei_saveas("temp.fee"); ei_analyze(); ei_loadsolution()
    print("===  " + tag + "  ===")
    for (label, x, y) in pts:
        v = eo_getpointvalues(x, y)
        print("  %-12s = %s" % (label, ("(no mesh)" if v is None else " | ".join(fmt(c) for c in v))))
    eo_close()

open("electrostatic1.fee")
report([("near", 40, 20), ("mid", 60, 92), ("far", 20, 150), ("shell", 148.17, 29.47)],
       "electrostatic1 (axi)")
open("electrostatic2.fee")
report([("near", 190, 20), ("mid", 210, 92), ("far", 170, 150), ("shell", 298.17, 29.47)],
       "electrostatic2 (planar)")
print("\nDone.")
