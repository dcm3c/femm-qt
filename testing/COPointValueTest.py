# COPointValueTest.py -- co_getpointvalues (V, Jx, Jy, Kx, Ky, Ex, Ey, epsx, epsy,
# Jdx, Jdy, sigx, sigy, Jcx, Jcy; complex for AC/lossy) at interior points on the
# current-flow fixtures: cf1 (DC), cf3 (AC), cf5/cf6 (lossy dielectric).
def fmt(z):
    r = z.real if isinstance(z, complex) else float(z); i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)
def report(fec, pts, tag):
    open(fec); ci_saveas("temp.fec"); ci_analyze(); ci_loadsolution()
    print("===  " + tag + "  ===")
    for (label, x, y) in pts:
        v = co_getpointvalues(x, y)
        print("  %-12s = %s" % (label, ("(no mesh)" if v is None else " | ".join(fmt(c) for c in v))))
    ci_close()
report("currentFlow1.fec", [("srcSide",-2.5,-1.5),("gndSide",0.5,-1.5)], "cf1 (planar, DC)")
report("currentFlow3.fec", [("near",40,20),("mid",60,92),("shell",148.17,29.47)], "cf3 (axi, AC 60Hz)")
report("currentFlow5.fec", [("diel",1,0),("diel2",0.5,0)], "cf5 (axi, lossy 100kHz)")
report("currentFlow6.fec", [("diel",-4,0),("diel2",-4.5,0)], "cf6 (planar, lossy 100kHz)")
print("\nDone.")
