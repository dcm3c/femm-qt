# CILineIntegralTest.py
# Exercises every current-flow line-integral type (0-3) of co_lineintegral:
#   0 E.t (voltage drop), 1 J.n (total current through contour), 2 length/area,
#   3 average voltage. Current flow has no stress-tensor force/torque.
#
# Fixtures span DC, AC, and lossy-dielectric regimes:
#   cf1 (planar, DC)          -- resistive; J.n caps the InputCurrent electrode
#   cf3 (axi, AC 60Hz)        -- electrostatic1 twin with a complex conductor V
#   cf4 (planar, AC 60Hz)     -- electrostatic2 twin
#   cf5 (axi, lossy 100kHz)   -- lossy dielectric; J.n cuts the mid-plane
# Per fixture a J.n contour gives the current; an E.t contour (between electrodes)
# gives voltage drop, contour length/area, and average voltage. Each component is
# printed as "re im" so the femm-qt and OF runs compare field-for-field with
# CILineIntegralCompare.py (co_lineintegral is complex for AC).

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def contour(pts):
    co_clearcontour()
    for (x, y) in pts:
        co_addcontour(x, y)

def line(t, label):
    z = co_lineintegral(t)
    print("  [%d] %-12s = %s | %s" % (t, label, fmt(z[0]), fmt(z[1])))

def run(fec, jn_pts, et_pts, tag):
    open(fec)
    ci_saveas("temp.fec")
    ci_analyze(); ci_loadsolution()
    print("===  " + tag + "  ===")
    contour(jn_pts); line(1, "J.n (current)")
    contour(et_pts); line(0, "E.t (Vdrop)"); line(2, "length/area"); line(3, "avg V")
    co_close()

run("currentFlow1.fec", [(-3, -1.9), (-2, -1.9)], [(-2.5, -2), (0.5, -2)],
    "cf1 (planar, DC)")
run("currentFlow3.fec", [(0, 70), (35, 70), (35, 5), (0, 5)], [(0, 10), (0, 0)],
    "cf3 (axi, AC 60Hz)")
run("currentFlow4.fec", [(120, 65), (180, 65), (180, 5), (120, 5), (120, 65)],
    [(150, 10), (150, 0)], "cf4 (planar, AC 60Hz)")
run("currentFlow5.fec", [(0, 0), (2, 0)], [(0, 3), (0, -3)],
    "cf5 (axi, lossy 100kHz)")
print("\nDone.")
