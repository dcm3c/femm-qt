-- Layer 2, luafemm: MOCircuitPropTest driven through the binding.
--
--     FEMM_DIR=<build>/bin  lua4 l2_circuitprop_luafemm.lua > out.txt
--     python3 ../MOCircuitPropCompare.py ../OF_circ_result.txt out.txt
--
-- A line-for-line translation of testing/MOCircuitPropTest.py, handed to the
-- same comparer and the same Original FEMM golden, so the binding is measured
-- against Original FEMM rather than against an in-process run.
--
-- 105 components over twelve scenarios, and the AC ones are the point: every
-- current and every flux linkage comes back complex, which is the half of the
-- wire that a real-valued round trip never touches.
--
-- Lua 4: format() is a global, there is no string library, and every number is
-- CComplex -- so re() and im() take the parts, where the Python driver asks for
-- .real and .imag.

dofile("../../luafemm/femm.lua")

TESTS = ".."

function T(name) return TESTS .. "/" .. name end
function W(name) return name end        -- scratch beside the goldens

function fmt(z)
    return format("%.10g %.10g", re(z), im(z))
end

function report(circuits, tag)
    mi_analyze()
    mi_loadsolution()
    print("===  " .. tag .. "  ===")
    local i
    for i = 1, getn(circuits) do
        local c = circuits[i]
        local I, V, F = mo_getcircuitproperties(c)
        print(format("  %-14s = %s | %s | %s", c, fmt(I), fmt(V), fmt(F)))
    end
    mo_close()
end

function rmb(i1, i2, i3, i4)
    mi_modifycircprop("i1", 1, i1)
    mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3)
    mi_modifycircprop("i4", 1, i4)
end

RMBC = {"i1", "i2", "i3", "i4"}

openfemm(1)

-- ---- RadialMagneticBearing (i1-i4) ----
opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("temp.fem"))
rmb(12, 6, 0, 6); mi_probdef(0, "inches", "planar")
report(RMBC, "RMB (x, DC) i=(12,6,0,6)")
opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("temp.fem"))
rmb(12, 6, 0, 6); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (x, AC 60Hz) i=(12,6,0,6)")
opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("temp.fem"))
rmb(6, 12, 6, 0); mi_probdef(0, "inches", "planar")
report(RMBC, "RMB (y, DC) i=(6,12,6,0)")
opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("temp.fem"))
rmb(6, 12, 6, 0); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (y, AC 60Hz) i=(6,12,6,0)")
opendocument(T("RadialMagneticBearing.fem")); mi_saveas(W("bias.fem"))
rmb(6, 6, 6, 6); mi_probdef(0, "inches", "planar"); mi_analyze()
mi_saveas(W("incr.fem")); mi_setprevious(W("bias.ans"), 1)
rmb(1, 0, -1, 0); mi_probdef(60, "inches", "planar")
report(RMBC, "RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)")

-- ---- roters1b (Coil Current) ----
function rot(a) mi_modifycircprop("Coil Current", 1, a) end

ROTC = {"Coil Current"}
opendocument(T("roters1b.fem")); mi_saveas(W("rtemp.fem"))
rot(0.8); mi_probdef(0, "inches", "axi")
report(ROTC, "roters1b (DC) 0.8A")
opendocument(T("roters1b.fem")); mi_saveas(W("rtemp.fem"))
rot(0.8); mi_probdef(60, "inches", "axi")
report(ROTC, "roters1b (AC 60Hz) 0.8A")
opendocument(T("roters1b.fem")); mi_saveas(W("rbias.fem"))
rot(0.8); mi_probdef(0, "inches", "axi"); mi_analyze()
mi_saveas(W("rincr.fem")); mi_setprevious(W("rbias.ans"), 1)
rot(0.1); mi_probdef(60, "inches", "axi")
report(ROTC, "roters1b (incremental) bias 0.8A; AC 0.1A")

-- ---- torque (i1, i2) ----
function torq(a, b)
    mi_modifycircprop("i1", 1, a)
    mi_modifycircprop("i2", 1, b)
end

TQC = {"i1", "i2"}
opendocument(T("torque.fem")); mi_saveas(W("temp.fem"))
torq(2, 0); mi_probdef(0, "inches", "planar")
report(TQC, "torque (DC) i=(2,0)")
opendocument(T("torque.fem")); mi_saveas(W("temp.fem"))
torq(2, 0); mi_probdef(60, "inches", "planar")
report(TQC, "torque (AC 60Hz) i=(2,0)")
opendocument(T("torque.fem")); mi_saveas(W("bias.fem"))
torq(1, 1); mi_probdef(0, "inches", "planar"); mi_analyze()
mi_saveas(W("incr.fem")); mi_setprevious(W("bias.ans"), 1)
torq(1, -1); mi_probdef(60, "inches", "planar")
report(TQC, "torque (incremental) bias i=(1,1); AC i=(1,-1)")

-- ---- Lorentz coils (i1) ----
LOR = {
    {"AxisymmetricLorentzForce.fem", "axi",    "AxiLorentz"},
    {"PlanarLorentzForce.fem",       "planar", "PlanarLorentz-y"},
    {"PlanarLorentzForceAlt.fem",    "planar", "PlanarLorentz-x"},
}
local k
for k = 1, getn(LOR) do
    local fem, ptype, tag = LOR[k][1], LOR[k][2], LOR[k][3]
    opendocument(T(fem)); mi_saveas(W("temp.fem"))
    mi_modifycircprop("i1", 1, 16); mi_probdef(0, "inches", ptype)
    report({"i1"}, tag .. " (DC) 16A")
    opendocument(T(fem)); mi_saveas(W("temp.fem"))
    mi_modifycircprop("i1", 1, 16); mi_probdef(60, "inches", ptype)
    report({"i1"}, tag .. " (AC 60Hz) 16A")
end

print("")
print("Done.")
closefemm()
