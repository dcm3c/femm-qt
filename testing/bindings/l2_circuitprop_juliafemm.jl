# Layer 2, juliafemm: MOCircuitPropTest driven through the binding.
# A line-for-line translation of testing/MOCircuitPropTest.py, checked by
# MOCircuitPropCompare.py against the same Original FEMM golden.

using Printf
const HERE = dirname(abspath(@__FILE__))
include(joinpath(HERE, "..", "..", "juliafemm", "juliafemm.jl"))
using .JuliaFEMM

const TESTS = abspath(joinpath(HERE, ".."))
# tempdir() is the platform's own temp directory: honours TMPDIR on POSIX and TEMP on Windows, so no per-platform branch is needed.
const TMP = tempdir()

T(name) = replace(joinpath(TESTS, name), "\\" => "/")
W(name) = replace(joinpath(TMP, name), "\\" => "/")
fmt(z) = @sprintf("%.10g %.10g", real(z), imag(z))

function report(circuits, tag)
    mi_analyze(); mi_loadsolution()
    @printf("===  %s  ===\n", tag)
    for c in circuits
        z = mo_getcircuitproperties(c)
        @printf("  %-14s = %s | %s | %s\n", c, fmt(z[1]), fmt(z[2]), fmt(z[3]))
    end
    mo_close()
end

setrmb(c) = for j in 1:4; mi_modifycircprop("i$j", 1, c[j]); end
settorq(a, b) = (mi_modifycircprop("i1", 1, a); mi_modifycircprop("i2", 1, b))

const RMBC = ["i1", "i2", "i3", "i4"]
const TQC = ["i1", "i2"]
const ROTC = ["Coil Current"]

openfemm(headless=true)
try
    # ---- RadialMagneticBearing ----
    for (cur, f, tag) in (([12, 6, 0, 6], 0, "RMB (x, DC) i=(12,6,0,6)"),
                          ([12, 6, 0, 6], 60, "RMB (x, AC 60Hz) i=(12,6,0,6)"),
                          ([6, 12, 6, 0], 0, "RMB (y, DC) i=(6,12,6,0)"),
                          ([6, 12, 6, 0], 60, "RMB (y, AC 60Hz) i=(6,12,6,0)"))
        opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("temp.fem"))
        setrmb(cur); mi_probdef(f, "inches", "planar")
        report(RMBC, tag)
    end
    opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("bias.fem"))
    setrmb([6, 6, 6, 6]); mi_probdef(0, "inches", "planar"); mi_analyze()
    mi_saveas(W("incr.fem")); mi_setprevious(W("bias.ans"), 1)
    setrmb([1, 0, -1, 0]); mi_probdef(60, "inches", "planar")
    report(RMBC, "RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)")

    # ---- roters1b ----
    opendocument(T("roters1b.fem")); mi_saveas(W("rtemp.fem"))
    mi_modifycircprop("Coil Current", 1, 0.8); mi_probdef(0, "inches", "axi")
    report(ROTC, "roters1b (DC) 0.8A")
    opendocument(T("roters1b.fem")); mi_saveas(W("rtemp.fem"))
    mi_modifycircprop("Coil Current", 1, 0.8); mi_probdef(60, "inches", "axi")
    report(ROTC, "roters1b (AC 60Hz) 0.8A")
    opendocument(T("roters1b.fem")); mi_saveas(W("rbias.fem"))
    mi_modifycircprop("Coil Current", 1, 0.8); mi_probdef(0, "inches", "axi"); mi_analyze()
    mi_saveas(W("rincr.fem")); mi_setprevious(W("rbias.ans"), 1)
    mi_modifycircprop("Coil Current", 1, 0.1); mi_probdef(60, "inches", "axi")
    report(ROTC, "roters1b (incremental) bias 0.8A; AC 0.1A")

    # ---- torque ----
    opendocument(T("torque.fem")); mi_saveas(W("temp.fem"))
    settorq(2, 0); mi_probdef(0, "inches", "planar")
    report(TQC, "torque (DC) i=(2,0)")
    opendocument(T("torque.fem")); mi_saveas(W("temp.fem"))
    settorq(2, 0); mi_probdef(60, "inches", "planar")
    report(TQC, "torque (AC 60Hz) i=(2,0)")
    opendocument(T("torque.fem")); mi_saveas(W("bias.fem"))
    settorq(1, 1); mi_probdef(0, "inches", "planar"); mi_analyze()
    mi_saveas(W("incr.fem")); mi_setprevious(W("bias.ans"), 1)
    settorq(1, -1); mi_probdef(60, "inches", "planar")
    report(TQC, "torque (incremental) bias i=(1,1); AC i=(1,-1)")

    # ---- Lorentz coils ----
    for (fem, ptype, tag) in (("AxisymmetricLorentzForce.fem", "axi", "AxiLorentz"),
                              ("PlanarLorentzForce.fem", "planar", "PlanarLorentz-y"),
                              ("PlanarLorentzForceAlt.fem", "planar", "PlanarLorentz-x"))
        opendocument(T(fem)); mi_saveas(W("temp.fem"))
        mi_modifycircprop("i1", 1, 16); mi_probdef(0, "inches", ptype)
        report(["i1"], tag * " (DC) 16A")
        opendocument(T(fem)); mi_saveas(W("temp.fem"))
        mi_modifycircprop("i1", 1, 16); mi_probdef(60, "inches", ptype)
        report(["i1"], tag * " (AC 60Hz) 16A")
    end

    println("")
    println("Done.")
finally
    closefemm()
end
