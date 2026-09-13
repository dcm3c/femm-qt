-- Layer 2, luafemm: sends COMPLEX arguments.
--
--     FEMM_DIR=<build>/bin  lua4 l2_phase_luafemm.lua > out.txt
--     python3 ../PhaseCompare.py l2_phasecurrent_reference.txt out.txt
--
-- See l2_phase_reference.py for why this scenario exists: the three currents
-- are complex and must arrive intact, and the reply echoes them back, so a
-- mangled argument is caught directly rather than as a small numeric drift.
--
-- The most direct test of the bug that started the binding work, and the one
-- that matters most here: lua4 compiles CComplex as its NUMBER type, so a
-- complex current is not a wrapper construct that femm.lua has to marshal
-- specially -- it is an ordinary Lua number written as -500 - 866.0254038*I.
-- What is being checked is that format_arg() renders it in the I* notation the
-- wire expects, and that the reply parses back into a number whose re() and
-- im() are the ones that were sent.

dofile("../../luafemm/femm.lua")

EX = "../../examples"

PHASES = {
    {"A",  1000},
    {"B", -500 - 866.0254038*I},
    {"C", -500 + 866.0254038*I},
}

function fmt(z)
    return format("%.10g %.10g", re(z), im(z))
end

openfemm(1)

opendocument(EX .. "/three-phase-line.FEM")
mi_saveas("l2_phase_lua.fem")
mi_probdef(60, "inches", "planar")
local i
for i = 1, getn(PHASES) do
    mi_modifycircprop(PHASES[i][1], 1, PHASES[i][2])
end
mi_analyze()
mi_loadsolution()
print("===  three-phase-line balanced 1000A 60Hz  ===")
for i = 1, getn(PHASES) do
    local name = PHASES[i][1]
    local I_, V, F = mo_getcircuitproperties(name)
    print(format("  %-14s = %s | %s | %s", name, fmt(I_), fmt(V), fmt(F)))
end
mo_close()
print("")
print("Done.")
closefemm()
