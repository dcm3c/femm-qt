package.path = package.path .. ";../?.lua"
dofile("../femm.lua")

openfemm(1)

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
