# HOPointValueTest.py -- ho_getpointvalues (T, Fx, Fy, Gx, Gy, kx, ky) at interior
# points on heatFlow1 (axi) and heatFlow2 (planar), both on the [0,150]^2 domain.
# "Bdry" is a boundary, so points sample the material bulk (Plaster) near/mid/far
# from the source. Heat is static -> 7 real values. Keyed by label for
# ScalarPointValueCompare.py.
def fmt(z):
    r = z.real if isinstance(z, complex) else float(z); i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)
def report(feh, pts, tag):
    open(feh); hi_saveas("temp.feh"); hi_analyze(); hi_loadsolution()
    print("===  " + tag + "  ===")
    for (label, x, y) in pts:
        v = ho_getpointvalues(x, y)
        print("  %-12s = %s" % (label, ("(no mesh)" if v is None else " | ".join(fmt(c) for c in v))))
    ho_close()
PTS = [("near", 40, 20), ("mid", 60, 92), ("far", 100, 40)]
report("heatFlow1.feh", PTS, "heatFlow1 (axi)")
report("heatFlow2.feh", PTS, "heatFlow2 (planar)")
print("\nDone.")
