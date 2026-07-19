# HILineIntegralTest.py
# Exercises every heat-flow line-integral type (0-3) of ho_lineintegral on
#   heatFlow1 (axisymmetric) and heatFlow2 (planar), both derived from the
#   electrostatic1 geometry (a hemisphere/semicircle "Bdry" conductor):
#   0 G.t (temperature drop), 1 F.n (total heat flow across contour), 2 length/
#   area, 3 average temperature. Heat flow has no stress-tensor force/torque.
#
# A contour wrapping the "Bdry" conductor gives F.n == the conductor heat flow;
# an axis path gives temperature drop, length/area, and average temperature. The
# axi avg-T (heatFlow1) runs along r=0, exercising the plain-average fallback
# (where the 2*pi*r weight degenerates). Heat is static, so ho_lineintegral
# returns two reals per type; each component is printed as "re im" (im == 0) so
# the femm-qt and OF runs compare field-for-field with HILineIntegralCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def contour(pts):
    ho_clearcontour()
    for (x, y) in pts:
        ho_addcontour(x, y)

def line(t, label):
    z = ho_lineintegral(t)
    print("  [%d] %-12s = %s | %s" % (t, label, fmt(z[0]), fmt(z[1])))

BDRY_BOX = [(0, 70), (35, 70), (35, 5), (0, 5)]
AXIS     = [(0, 10), (0, 0)]

def run(feh, tag):
    open(feh)
    hi_saveas("temp.feh")
    hi_analyze(); hi_loadsolution()
    print("===  " + tag + "  ===")
    contour(BDRY_BOX); line(1, "F.n (heatflow)")
    contour(AXIS);     line(0, "G.t (Tdrop)"); line(2, "length/area"); line(3, "avg T")
    ho_close()

run("heatFlow1.feh", "heatFlow1 (axi)")
run("heatFlow2.feh", "heatFlow2 (planar)")
print("\nDone.")
