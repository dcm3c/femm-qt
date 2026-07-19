# EOLineIntegralTest.py
# Exercises every electrostatics line-integral type (0-4) of eo_lineintegral on
#   electrostatic1 (axisymmetric) and electrostatic2 (planar), each with a
#   contour that wraps the "+10000V" conductor plus an E.t path to ground:
#   0 E.t (voltage drop), 1 D.n (== enclosed charge, Gauss), 2 length/area,
#   3 stress-tensor Force, 4 stress-tensor Torque.
#
# Electrostatics is static, so eo_lineintegral returns two real values per type
# (0/1: total, avg; 2: length, area; 3: Fx, Fy; 4: torque, 0). Each component is
# printed as "re im" (im == 0) so the femm-qt and OF runs compare field-for-field
# with EOLineIntegralCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def contour(pts):
    eo_clearcontour()
    for (x, y) in pts:
        eo_addcontour(x, y)

def line(t, label):
    z = eo_lineintegral(t)
    print("  [%d] %-14s = %s" % (t, label, " | ".join(fmt(v) for v in z)))

E1_BOX = [(0, 70), (35, 70), (35, 5), (0, 5)]
E1_ET  = [(0, 10), (0, 0)]
E2_BOX = [(120, 65), (180, 65), (180, 5), (120, 5), (120, 65)]
E2_ET  = [(150, 10), (150, 0)]

# ---- electrostatic1 (axi) ----
open("electrostatic1.fee"); ei_saveas("temp.fee")
ei_analyze(); ei_loadsolution()
print("===  electrostatic1 (axi)  ===")
contour(E1_BOX); line(1, "D.n (charge)"); line(3, "Force"); line(2, "length/area")
contour(E1_ET);  line(0, "E.t (Vdrop)")
eo_close()

# ---- electrostatic2 (planar) ----
open("electrostatic2.fee"); ei_saveas("temp.fee")
ei_analyze(); ei_loadsolution()
print("===  electrostatic2 (planar)  ===")
contour(E2_BOX); line(1, "D.n (charge)"); line(3, "Force"); line(4, "Torque"); line(2, "length/area")
contour(E2_ET);  line(0, "E.t (Vdrop)")
eo_close()

print("\nDone.")
