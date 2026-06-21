# Phase3.jl

if !isdefined(Main, :JuliaFEMM); include(joinpath(@__DIR__, "..", "juliafemm.jl")); using .JuliaFEMM; end

openfemm()
newdocument(0)

# define some parameters.  These can then
# be used to draw the geometry parametrically
r = 1/2
R = 2
h = 1/0.0254

# define some materials
mi_addmaterial("Air")
mi_addmaterial("Cu", 1, 1, 0, 0, 58)

# draw geometry of interest
for k in 0:2
    tta = k * 2 * pi / 3
    mi_drawarc((R+r)*cos(tta), (R+r)*sin(tta), (R-r)*cos(tta), (R-r)*sin(tta), 180, 1)
    mi_drawarc((R-r)*cos(tta), (R-r)*sin(tta), (R+r)*cos(tta), (R+r)*sin(tta), 180, 1)
    mi_addcircprop("i" * string(k), 1000 * exp(2im * pi * k / 3), 1)
    mi_addblocklabel(R*cos(tta), R*sin(tta))
    mi_selectlabel(R*cos(tta), R*sin(tta))
    mi_setblockprop("Cu", 1, 0, "i" * string(k), 0, 0, 1)
    mi_clearselected()
end

mi_addblocklabel(0, 0)
mi_selectlabel(0, 0)
mi_setblockprop("Air", 1, 0, "<None>", 0, 0, 1)
mi_clearselected()

# draw boundary
mi_makeABC(3, 2*R, 0, 0, 0)

# Save, analyze, and view results
mi_probdef(60, "inches", "planar", 1e-8, h, 30)
mi_saveas("Phase3.fem")
mi_analyze()
mi_loadsolution()

z = mo_getcircuitproperties("i0")
z = z[2] / z[1]
println("The per-meter 60Hz impedance of each line is $(round(z, sigdigits=4)) Ohms")

print("Press <ENTER> to continue"); readline()
closefemm()
