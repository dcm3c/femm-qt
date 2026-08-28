"""Layer 2 phasecurrent, pyfemm: sends COMPLEX arguments.

See l2_phase_reference.py for why this scenario exists. The three currents are
complex and must arrive intact; the reply echoes them back, so a mangled
argument is caught by PhaseCompare.py directly.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "pyfemm"))
EX = os.path.abspath(os.path.join(HERE, "..", "..", "examples"))
TMP = os.environ.get("TEMP", "C:/temp")

import femm  # noqa: E402

PHASES = (("A", complex(1000, 0)),
          ("B", complex(-500, -866.0254038)),
          ("C", complex(-500, 866.0254038)))


def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)


femm.openfemm(1)
try:
    femm.opendocument(os.path.join(EX, "three-phase-line.FEM").replace("\\", "/"))
    femm.mi_saveas(os.path.join(TMP, "l2_phase_py.fem").replace("\\", "/"))
    femm.mi_probdef(60, "inches", "planar")
    for name, cur in PHASES:
        femm.mi_modifycircprop(name, 1, cur)
    femm.mi_analyze()
    femm.mi_loadsolution()
    print("===  three-phase-line balanced 1000A 60Hz  ===")
    for name, cur in PHASES:
        z = femm.mo_getcircuitproperties(name)
        print("  %-14s = %s | %s | %s" % (name, fmt(z[0]), fmt(z[1]), fmt(z[2])))
    femm.mo_close()
    print("")
    print("Done.")
finally:
    femm.closefemm()
