# coilgun.jl
#
# Note: requires coilgun.fem from ../pyfemm/examples/

if !isdefined(Main, :JuliaFEMM); include(joinpath(@__DIR__, "..", "juliafemm.jl")); using .JuliaFEMM; end

openfemm()
opendocument(joinpath(@__DIR__, "..", "..", "pyfemm", "examples", "coilgun.fem"))
mi_saveas("temp.fem")
mi_seteditmode("group")

z = Float64[]
f = Float64[]

for n in 0:15
    mi_analyze()
    mi_loadsolution()
    mo_groupselectblock(1)
    fz = mo_blockintegral(19)
    push!(z, n * 0.1)
    push!(f, fz)
    mi_selectgroup(1)
    mi_movetranslate(0, -0.1)
end

closefemm()

using Plots
plot(z, f, xlabel="Offset (in)", ylabel="Force (N)",
     title="Coilgun Force vs. Position", legend=false, marker=:circle)
gui()
