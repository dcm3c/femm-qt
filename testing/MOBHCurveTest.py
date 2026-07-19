# MOBHCurveTest.py
# Mesh-independent B-H curve check. block.fem is a 1" cube of M-19 Steel with a
# prescribed-A gradient (Alo=0, Ahi=0.0254*Bdes), so B == Bdes uniformly through
# the whole block and mo_getpointvalues(0,0) isolates the B-H curve / permeability
# evaluation with ZERO mesh sensitivity. femm-qt and OF must be identical for
# every (frequency, Bdes) -- any difference is a genuine constitutive-evaluation
# discrepancy, not mesh.
#
# The material is laminated (LamFill=0.98), so there is no bulk eddy current and B
# stays uniform even at frequency; the AC rows therefore exercise the COMPLEX
# GetMu (the frequency-convolved effective permeability, complex mu, and the
# hysteresis/lamination loss Ph) with no skin effect. Sweep spans the linear
# region through deep saturation at DC, 60Hz and 400Hz.
#
# Each row prints the 14 point values (A, B1, B2, sig, E, H1, H2, Je, Js, mu1,
# mu2, Pe, Ph, ff) as "re im" pairs, keyed by "f=<freq> Bdes=<x>", for
# MOBHCurveCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.12g %.12g" % (r, i)

open("block.fem"); mi_saveas("temp.fem")
print("=== block.fem uniform-B sweep @ (0,0):  A|B1|B2|sig|E|H1|H2|Je|Js|mu1|mu2|Pe|Ph|ff ===")
for freq in [0, 60, 400]:
    mi_probdef(freq, "inches", "planar")
    for Bdes in [0.1, 0.5, 1.0, 1.3, 1.5, 1.7, 1.9, 2.1, 2.3]:
        mi_modifyboundprop("Ahi", 1, 0.0254 * Bdes)
        mi_analyze(); mi_loadsolution()
        v = mo_getpointvalues(0, 0)
        print("  f=%d Bdes=%.3f = %s" % (freq, Bdes, " | ".join(fmt(c) for c in v)))
        mo_close()
print("Done.")
