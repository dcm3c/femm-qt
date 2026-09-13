-- Layer 2, luafemm: EOCondPropTest driven through the binding.
--
--     FEMM_DIR=<build>/bin  lua4 l2_condprop_luafemm.lua > out.txt
--     python3 ../CondPropCompare.py ../OF_eic_result.txt out.txt
--
-- A line-for-line translation of testing/EOCondPropTest.py, which is one of the
-- 18 suites already validated against Original FEMM. The output format is
-- identical, so the EXISTING comparer checks this against the SAME OF golden --
-- the binding is measured against Original FEMM, not against an in-process run.
--
-- Run by lua4 directly rather than through femm-lua, because that is what the
-- other drivers do: each one opens its own connection so the runner can drive
-- every binding the same way. The launcher has its own coverage in
-- testing/LuaFemmTest.py.
--
-- Lua 4, not Lua 5: format() is a global and there is no string library, no
-- os.path, and no require. Every number is CComplex, so re() and im() pull the
-- parts out rather than a .real/.imag attribute.

dofile("../../luafemm/femm.lua")

TESTS = ".."

function fmt(z)
    return format("%.10g %.10g", re(z), im(z))
end

function run(fee, cond, tag)
    opendocument(TESTS .. "/" .. fee)
    ei_saveas(TMPNAME .. "_l2_lua_temp.fee")
    ei_analyze()
    ei_loadsolution()
    print("===  " .. tag .. "  ===")
    local V, q = eo_getconductorproperties(cond)
    print(format("  %-14s = %s | %s", cond, fmt(V), fmt(q)))
    eo_close()
end

-- Scratch beside the goldens, as the other drivers do: a couple of the
-- comparers read their inputs back by relative name.
TMPNAME = "."

openfemm(1)                      -- 1 = headless
run("electrostatic1.fee", "+10000V", "electrostatic1 (axi)")
run("electrostatic2.fee", "+10000V", "electrostatic2 (planar)")
print("")
print("Done.")
closefemm()
