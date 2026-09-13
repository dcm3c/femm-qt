-- hello_femm.lua
--
-- Run it with the launcher, which connects to FEMM before the first line
-- below executes:
--
--     femm-lua hello_femm.lua
--
-- There is deliberately no dofile("../femm.lua") and no openfemm() here.
-- femm-lua.sh / femm-lua.bat supply both, and a second openfemm() would open
-- a second connection to a server that serves one client per port -- it would
-- be refused. A script for the Lua binding looks like a script for FEMM 4.2,
-- which is the whole point of the binding.

newdocument(0)
mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)

mi_drawrectangle(0, 0, 10, 10)
mi_addblocklabel(5, 5)

mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0)
mi_selectlabel(5, 5)
mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
mi_clearselected()

mi_saveas("hello_test.fem")
mi_close()
closefemm()

print("hello_femm.lua completed successfully")
