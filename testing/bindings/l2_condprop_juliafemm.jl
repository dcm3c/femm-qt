# Layer 2, juliafemm: EOCondPropTest driven through the binding.
#
#   julia l2_condprop_juliafemm.jl        (with FEMM_DIR set)
#
# A line-for-line translation of testing/EOCondPropTest.py, so the existing
# CondPropCompare.py checks it against the same Original FEMM golden.

using Printf
const HERE = dirname(abspath(@__FILE__))
include(joinpath(HERE, "..", "..", "juliafemm", "juliafemm.jl"))
using .JuliaFEMM

const TESTS = abspath(joinpath(HERE, ".."))
const TMP = get(ENV, "TEMP", "C:/temp")

fmt(z) = @sprintf("%.10g %.10g", real(z), imag(z))

function runcase(fee, cond, tag)
    opendocument(replace(joinpath(TESTS, fee), "\\" => "/"))
    ei_saveas(replace(joinpath(TMP, "l2_jl_temp.fee"), "\\" => "/"))
    ei_analyze()
    ei_loadsolution()
    @printf("===  %s  ===
", tag)
    z = eo_getconductorproperties(cond)
    @printf("  %-14s = %s | %s
", cond, fmt(z[1]), fmt(z[2]))
    eo_close()
end

openfemm(headless=true)
try
    runcase("electrostatic1.fee", "+10000V", "electrostatic1 (axi)")
    runcase("electrostatic2.fee", "+10000V", "electrostatic2 (planar)")
    println("")
    println("Done.")
finally
    closefemm()
end
