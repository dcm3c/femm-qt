-- coilgun.lua
--
-- Run it with the launcher, which connects to FEMM before the first line
-- below executes:
--
--     femm-lua coilgun.lua
--
-- There is deliberately no dofile("../femm.lua") and no openfemm() here.
-- femm-lua.sh / femm-lua.bat supply both, and a second openfemm() would open
-- a second connection to a server that serves one client per port -- it would
-- be refused. A script for the Lua binding looks like a script for FEMM 4.2,
-- which is the whole point of the binding.

newdocument(0)

mi_probdef(0, "millimeters", "axi", 1e-8, 0, 30)

-- Coil geometry: inner radius=5mm, outer=15mm, half-height=10mm
local ri, ro, h = 5, 15, 10

-- Draw the coil region
mi_drawrectangle(ri, -h, ro, h)

-- Draw a bounding air region
mi_drawrectangle(0, -40, 40, 40)

-- Add block labels
mi_addblocklabel((ri+ro)/2, 0)  -- coil center
mi_addblocklabel(30, 30)         -- air region

-- Materials
mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0)
mi_addmaterial("Copper", 1, 1, 0, 0, 58, 0, 0, 1, 0)

-- Circuit with 100 A-turns
mi_addcircprop("Coil", 100, 1)

-- Assign air block
mi_selectlabel(30, 30)
mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
mi_clearselected()

-- Assign coil block
mi_selectlabel((ri+ro)/2, 0)
mi_setblockprop("Copper", 1, 0, "Coil", 0, 0, 1)
mi_clearselected()

-- Boundary condition on outer edge (top, right, bottom)
mi_addboundprop("A=0", 0, 0, 0, 0, 0, 0, 0, 0, 0)
mi_selectsegment(20, -40)   -- bottom
mi_selectsegment(40, 0)     -- right
mi_selectsegment(20, 40)    -- top
mi_setsegmentprop("A=0", 0, 1, 0, 0)
mi_clearselected()

mi_saveas("coilgun_test.fem")

-- Solve
mi_analyze(1)
mi_loadsolution()

-- Read flux density at origin
-- mo_getpointvalues is the FEMM 4.2 way: mo_getb is a convenience the newer
-- bindings added and 4.2 never had, so it is not in this binding either.
local A, Bx, By = mo_getpointvalues(0, 0)
print(format("B at origin: Bx=%.6f T, By=%.6f T", Bx, By))

-- Block integral for stored energy
mo_selectblock((ri+ro)/2, 0)
local energy = mo_blockintegral(2)
print(format("Stored energy in coil: %.6e J", energy))
mo_clearblock()

mo_close()
mi_close()
closefemm()

print("coilgun.lua completed successfully")
