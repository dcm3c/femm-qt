# Roters1b: Simulation of a Tapered Plunger Magnet
# David Meeker
# dmeeker@ieee.org
#
# This geometry comes from Chap. IX, Figure 7 of Herbert Roters
# classic book, Electromagnetic Devices.  The program moves
# the plunger of the magnet over a stroke of 1.5in at 1/10in increments
# solving for the field and evaluating the force on the plunger at
# each position.  When all positions have been evaluated, the program
# plots a curve of the finite element force predictions.
#
# Note: requires roters1b.fem from ../pyfemm/examples/

if !isdefined(Main, :JuliaFEMM); include(joinpath(@__DIR__, "..", "juliafemm.jl")); using .JuliaFEMM; end

openfemm()
opendocument(joinpath(@__DIR__, "..", "..", "pyfemm", "examples", "roters1b.fem"))
mi_saveas("temp.fem")

n = 16
stroke = 1.5
x = Float64[]
f = Float64[]

for k in 0:n-1
    println("iteration $k of $(n - 1)")
    mi_analyze()
    mi_loadsolution()
    mo_groupselectblock(1)
    push!(x, stroke * k / (n - 1))
    push!(f, mo_blockintegral(19) / 4.4481)
    mi_selectgroup(1)
    mi_movetranslate(0, -stroke / (n - 1))
    mi_clearselected()
end

closefemm()

# Print results (plotting requires a separate package like Plots.jl)
println("Displacement (in) | Force (Lbf)")
for i in eachindex(x)
    println("  $(x[i])  $(f[i])")
end
