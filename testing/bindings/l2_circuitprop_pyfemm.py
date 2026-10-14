"""Layer 2, pyfemm: MOCircuitPropTest driven through the binding.

A line-for-line translation of testing/MOCircuitPropTest.py, so
MOCircuitPropCompare.py checks it against the same Original FEMM golden.

This is the scenario that matters most for a binding: every AC case returns
COMPLEX current, voltage and flux linkage, so it exercises complex results
coming back through the reply parser -- three per circuit, 105 components in
all, of which the comparer checks 72 (the harmonic cases with iron are not
compared against OF; see of_ac_curve.py). (Complex arguments GOING OUT are still uncovered: no accuracy suite passes
one, which is exactly why octavefemm's num.m survived five months.)

Paths are absolute here. The in-process test relies on -run having chdir'd into
testing/, which a binding does not do -- the engine's working directory is
wherever the host launched it.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "pyfemm"))
TESTS = os.path.abspath(os.path.join(HERE, ".."))
# tempfile.gettempdir() is the platform's own temp directory: honours TMPDIR on POSIX and TEMP on Windows,
# so this no longer needs a per-platform branch.
TMP = tempfile.gettempdir()

import femm  # noqa: E402


def T(name):
    return os.path.join(TESTS, name).replace("\\", "/")


def W(name):
    return os.path.join(TMP, name).replace("\\", "/")


def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)


def report(circuits, tag):
    femm.mi_analyze()
    femm.mi_loadsolution()
    print("===  " + tag + "  ===")
    for c in circuits:
        z = femm.mo_getcircuitproperties(c)
        print("  %-14s = %s | %s | %s" % (c, fmt(z[0]), fmt(z[1]), fmt(z[2])))
    femm.mo_close()


def rmb(i1, i2, i3, i4):
    femm.mi_modifycircprop("i1", 1, i1)
    femm.mi_modifycircprop("i2", 1, i2)
    femm.mi_modifycircprop("i3", 1, i3)
    femm.mi_modifycircprop("i4", 1, i4)


RMBC = ["i1", "i2", "i3", "i4"]

femm.openfemm(1)
try:
    # ---- RadialMagneticBearing (i1-i4) ----
    femm.opendocument(T("RadialMagneticBearing.fem")); femm.mi_saveas(W("temp.fem"))
    rmb(12, 6, 0, 6); femm.mi_probdef(0, "inches", "planar")
    report(RMBC, "RMB (x, DC) i=(12,6,0,6)")
    femm.opendocument(T("RadialMagneticBearing.fem")); femm.mi_saveas(W("temp.fem"))
    rmb(12, 6, 0, 6); femm.mi_probdef(60, "inches", "planar")
    report(RMBC, "RMB (x, AC 60Hz) i=(12,6,0,6)")
    femm.opendocument(T("RadialMagneticBearing.fem")); femm.mi_saveas(W("temp.fem"))
    rmb(6, 12, 6, 0); femm.mi_probdef(0, "inches", "planar")
    report(RMBC, "RMB (y, DC) i=(6,12,6,0)")
    femm.opendocument(T("RadialMagneticBearing.fem")); femm.mi_saveas(W("temp.fem"))
    rmb(6, 12, 6, 0); femm.mi_probdef(60, "inches", "planar")
    report(RMBC, "RMB (y, AC 60Hz) i=(6,12,6,0)")
    femm.opendocument(T("RadialMagneticBearing.fem")); femm.mi_saveas(W("bias.fem"))
    rmb(6, 6, 6, 6); femm.mi_probdef(0, "inches", "planar"); femm.mi_analyze()
    femm.mi_saveas(W("incr.fem")); femm.mi_setprevious(W("bias.ans"), 1)
    rmb(1, 0, -1, 0); femm.mi_probdef(60, "inches", "planar")
    report(RMBC, "RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)")

    # ---- roters1b (Coil Current) ----
    def rot(a):
        femm.mi_modifycircprop("Coil Current", 1, a)

    ROTC = ["Coil Current"]
    femm.opendocument(T("roters1b.fem")); femm.mi_saveas(W("rtemp.fem"))
    rot(0.8); femm.mi_probdef(0, "inches", "axi")
    report(ROTC, "roters1b (DC) 0.8A")
    femm.opendocument(T("roters1b.fem")); femm.mi_saveas(W("rtemp.fem"))
    rot(0.8); femm.mi_probdef(60, "inches", "axi")
    report(ROTC, "roters1b (AC 60Hz) 0.8A")
    femm.opendocument(T("roters1b.fem")); femm.mi_saveas(W("rbias.fem"))
    rot(0.8); femm.mi_probdef(0, "inches", "axi"); femm.mi_analyze()
    femm.mi_saveas(W("rincr.fem")); femm.mi_setprevious(W("rbias.ans"), 1)
    rot(0.1); femm.mi_probdef(60, "inches", "axi")
    report(ROTC, "roters1b (incremental) bias 0.8A; AC 0.1A")

    # ---- torque (i1, i2) ----
    def torq(a, b):
        femm.mi_modifycircprop("i1", 1, a)
        femm.mi_modifycircprop("i2", 1, b)

    TQC = ["i1", "i2"]
    femm.opendocument(T("torque.fem")); femm.mi_saveas(W("temp.fem"))
    torq(2, 0); femm.mi_probdef(0, "inches", "planar")
    report(TQC, "torque (DC) i=(2,0)")
    femm.opendocument(T("torque.fem")); femm.mi_saveas(W("temp.fem"))
    torq(2, 0); femm.mi_probdef(60, "inches", "planar")
    report(TQC, "torque (AC 60Hz) i=(2,0)")
    femm.opendocument(T("torque.fem")); femm.mi_saveas(W("bias.fem"))
    torq(1, 1); femm.mi_probdef(0, "inches", "planar"); femm.mi_analyze()
    femm.mi_saveas(W("incr.fem")); femm.mi_setprevious(W("bias.ans"), 1)
    torq(1, -1); femm.mi_probdef(60, "inches", "planar")
    report(TQC, "torque (incremental) bias i=(1,1); AC i=(1,-1)")

    # ---- Lorentz coils (i1) ----
    for fem, ptype, tag in (("AxisymmetricLorentzForce.fem", "axi", "AxiLorentz"),
                            ("PlanarLorentzForce.fem", "planar", "PlanarLorentz-y"),
                            ("PlanarLorentzForceAlt.fem", "planar", "PlanarLorentz-x")):
        femm.opendocument(T(fem)); femm.mi_saveas(W("temp.fem"))
        femm.mi_modifycircprop("i1", 1, 16); femm.mi_probdef(0, "inches", ptype)
        report(["i1"], tag + " (DC) 16A")
        femm.opendocument(T(fem)); femm.mi_saveas(W("temp.fem"))
        femm.mi_modifycircprop("i1", 1, 16); femm.mi_probdef(60, "inches", ptype)
        report(["i1"], tag + " (AC 60Hz) 16A")

    print("")
    print("Done.")
finally:
    femm.closefemm()
