# Wound Copper Coil with an Iron Core
# David Meeker
# dmeeker@ieee.org
#
# This program considers an axisymmetric magnetostatic problem
# of a cylindrical coil with an axial length of 100 mm, an
# inner radius of 50 mm, and an outer radius of 100 mm.  The
# coil has 200 turns and the coil current is 20 Amps. There is
# an iron bar 80 mm long with a radius of 10 mm centered co-
# axially with the coil.  The objective of the analysis is to
# determine the flux density at the center of the iron bar,
# and to plot the field along the r=0 axis. This analysis
# defines a nonlinear B-H curve for the iron and employs an
# asymptotic boundary condition to approximate an "open"
# boundary condition on the edge of the solution domain.

if !isdefined(Main, :JuliaFEMM); include(joinpath(@__DIR__, "..", "juliafemm.jl")); using .JuliaFEMM; end

# The package must be initialized with the openfemm command.
openfemm()

# We need to create a new Magnetostatics document to work on.
newdocument(0)

# Define the problem type.  Magnetostatic; Units of mm; Axisymmetric;
# Precision of 10^(-8) for the linear solver; a placeholder of 0 for
# the depth dimension, and an angle constraint of 30 degrees
mi_probdef(0, "millimeters", "axi", 1e-8, 0, 30)

# Draw a rectangle for the steel bar on the axis;
mi_drawrectangle(0, -40, 10, 40)

# Draw a rectangle for the coil;
mi_drawrectangle(50, -50, 100, 50)

# Define an "open" boundary condition using the built-in function:
mi_makeABC()

# Add block labels, one to each the steel, coil, and air regions.
mi_addblocklabel(5, 0)
mi_addblocklabel(75, 0)
mi_addblocklabel(30, 100)

# Add some block labels materials properties
mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
mi_addmaterial("Coil", 1, 1, 0, 0, 58*0.65, 0, 0, 1, 0, 0, 0)
mi_addmaterial("LinearIron", 2100, 2100, 0, 0, 0, 0, 0, 1, 0, 0, 0)

# A more interesting material to add is the iron with a nonlinear
# BH curve.  First, we create a material in the same way as if we
# were creating a linear material, except the values used for
# permeability are merely placeholders.
mi_addmaterial("Iron", 2100, 2100, 0, 0, 0, 0, 0, 1, 0, 0, 0)

# A set of points defining the BH curve is then specified.
bdata = [0.0, 0.3, 0.8, 1.12, 1.32, 1.46, 1.54, 1.62, 1.74, 1.87, 1.99, 2.046, 2.08]
hdata = [0, 40, 80, 160, 318, 796, 1590, 3380, 7960, 15900, 31800, 55100, 79600]
for n in 1:length(bdata)
    mi_addbhpoint("Iron", bdata[n], hdata[n])
end

# Add a "circuit property" so that we can calculate the properties of the
# coil as seen from the terminals.
mi_addcircprop("icoil", 20, 1)

# Apply the materials to the appropriate block labels
mi_selectlabel(5, 0)
mi_setblockprop("Iron", 0, 1, "<None>", 0, 0, 0)
mi_clearselected()

mi_selectlabel(75, 0)
mi_setblockprop("Coil", 0, 1, "icoil", 0, 0, 200)
mi_clearselected()

mi_selectlabel(30, 100)
mi_setblockprop("Air", 0, 1, "<None>", 0, 0, 0)
mi_clearselected()

# Now, the finished input geometry can be displayed.
mi_zoomnatural()

# We have to give the geometry a name before we can analyze it.
mi_saveas("coil.fem")

# Now, analyze the problem and load the solution when the analysis is finished
mi_analyze()
mi_loadsolution()

# If we were interested in the flux density at specific positions,
# we could inquire at specific points directly:
b0 = mo_getb(0, 0)
println("Flux density at the center of the bar is $(b0[2]) T")
b1 = mo_getb(0, 50)
println("Flux density at r=0,z=50 is $(b1[2]) T")

# The program will report the terminal properties of the circuit:
# current, voltage, and flux linkage
vals = mo_getcircuitproperties("icoil")

# If we were interested in inductance, it could be obtained by
# dividing flux linkage by current
L = 1000 * vals[3] / vals[1]
println("The self-inductance of the coil is $L mH")

# Or we could, for example, plot the results along a line
zee = Float64[]
bee = Float64[]
for n in -100:100
    b = mo_getb(0, n)
    push!(zee, n)
    push!(bee, b[2])
end

# Plot results
using Plots
plot(zee, bee, xlabel="z (mm)", ylabel="Bz (T)",
     title="Axial Flux vs. Position", legend=false, marker=:circle)
gui()

# When the analysis is completed, FEMM can be shut down.
closefemm()
