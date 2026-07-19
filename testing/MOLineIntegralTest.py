# MOLineIntegralTest.py
# Exercises every magnetics line-integral type (0-5) of mo_lineintegral across a
# set of models, each with a purpose-built contour:
#   tutorial        (axi, DC)     H.t around the coil == enclosed amp-turns (1000);
#                                 B.n flux across an axis cut; contour length/area.
#   tutorial_planar (planar, DC)  same, planar.
#   RMB             (planar, AC)  stress-tensor Force on a CW gap circle (r=1.020");
#                                 (B.n)^2 around the same circle.
#   roters1b        (axi, DC/AC)  Force on a corner-avoiding gap contour.
#   torque          (planar, AC)  Force and Torque on a rectangle wrapping the rotor.
#
# mo_lineintegral returns a tuple whose width depends on type/frequency:
#   0 B.n         -> (total B.n,  avg B.n)
#   1 H.t         -> (total H.t,  avg H.t)             [total == enclosed A-turns]
#   2 length      -> (length,     swept area)
#   3 Force  DC   -> (Fx, Fy)     AC -> (DC Fx, DC Fy, 2x Fx, 2x Fy)
#   4 Torque DC   -> (T, 0)       AC -> (DC T, 2x T)
#   5 (B.n)^2     -> (total, avg)
# Each component is printed via fmt() so the femm-qt (Python) and OF (Lua) runs
# emit an identical, field-by-field parseable format for MOLineIntegralCompare.py.
import math

def fmt(z):
    # Each component printed as "re im" (space-separated) so the femm-qt and OF
    # runs emit an identical, unambiguous format for MOLineIntegralCompare.py.
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def contour(pts):
    mo_clearcontour()
    for (x, y) in pts:
        mo_addcontour(x, y)

def line(t, label):
    z = mo_lineintegral(t)
    print("  [%d] %-18s = %s" % (t, label, " | ".join(fmt(v) for v in z)))

def cw_circle(R, dth=10.0):
    n = int(round(360.0 / dth))
    return [(R * math.cos(math.radians(-k * dth)),
             R * math.sin(math.radians(-k * dth))) for k in range(n + 1)]

COIL_LOOP = [(0.3, 1.5), (2, 1.5), (2, -1.5), (0.3, -1.5), (0.3, 1.5)]
ROT_GAP   = [(0, 7), (0.5, 7), (0.672, 4.9375), (0.672, 4), (0.672, 3),
             (0.672, 2), (0.672, 1), (0.672, 0), (0.672, -1), (0.672, -1.4), (0, -1.4)]
TORQ_BOX  = [(-1.25, 0.275), (5.25, 0.275), (5.25, -0.275), (-1.25, -0.275), (-1.25, 0.275)]

# ---- tutorial (axi, DC): Ampere's law + B.n flux ----
open("tutorial.fem"); mi_saveas("temp.fem"); mi_probdef(0, "inches", "axi")
mi_analyze(); mi_loadsolution()
print("===  tutorial (axi, DC)  ===")
contour(COIL_LOOP);        line(1, "H.t (Ampere)")
contour([(0, 0), (0.5, 0)]); line(0, "B.n flux"); line(2, "length/area")
mo_close()

# ---- tutorial_planar (planar, DC) ----
open("tutorial_planar.fem"); mi_saveas("temp.fem"); mi_probdef(0, "inches", "planar")
mi_analyze(); mi_loadsolution()
print("===  tutorial_planar (planar, DC)  ===")
contour(COIL_LOOP);          line(1, "H.t (Ampere)")
contour([(-0.5, 0), (0.5, 0)]); line(0, "B.n flux"); line(2, "length/area")
mo_close()

# ---- RMB (planar, AC 60Hz, y-excite i=(6,12,6,0)): Force + (B.n)^2 ----
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem")
mi_modifycircprop("i1", 1, 6); mi_modifycircprop("i2", 1, 12)
mi_modifycircprop("i3", 1, 6); mi_modifycircprop("i4", 1, 0)
mi_probdef(60, "inches", "planar")
mi_analyze(); mi_loadsolution()
print("===  RMB (planar, AC 60Hz, y-excite)  ===")
contour(cw_circle(1.020)); line(3, "Force"); line(5, "(B.n)^2")
mo_close()

# ---- roters1b (axi): Force, DC and AC ----
for freq in (0, 60):
    open("roters1b.fem"); mi_saveas("rtemp.fem")
    mi_modifycircprop("Coil Current", 1, 0.8)
    mi_probdef(freq, "inches", "axi")
    mi_analyze(); mi_loadsolution()
    print("===  roters1b (axi, %s)  ===" % ("DC" if freq == 0 else "AC 60Hz"))
    contour(ROT_GAP); line(3, "Force")
    mo_close()

# ---- torque (planar, AC 60Hz, i1=2 i2=0): Force + Torque ----
open("torque.fem"); mi_saveas("temp.fem")
mi_modifycircprop("i1", 1, 2); mi_modifycircprop("i2", 1, 0)
mi_probdef(60, "inches", "planar")
mi_analyze(); mi_loadsolution()
print("===  torque (planar, AC 60Hz)  ===")
contour(TORQ_BOX); line(3, "Force"); line(4, "Torque")
mo_close()

print("\nDone.")
