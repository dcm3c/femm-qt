# ReturnErrorMsg.jl

if !isdefined(Main, :JuliaFEMM); include(joinpath(@__DIR__, "..", "juliafemm.jl")); using .JuliaFEMM; end

openfemm()
callfemm("mi_nonexistentfunction()")
closefemm()
