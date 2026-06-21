package.path = package.path .. ";../?.lua"
dofile("../femm.lua")

-- Demonstrate complex number support with AC magnetics analysis

-- Verify complex arithmetic works
local z1 = Complex(3, 4)
local z2 = Complex(1, -2)
print("z1 = " .. tostring(z1))
print("z2 = " .. tostring(z2))
print("z1 + z2 = " .. tostring(z1 + z2))
print("z1 * z2 = " .. tostring(z1 * z2))
print("|z1| = " .. abs(z1))
print("arg(z1) = " .. arg(z1) .. " rad")

-- Euler's identity: e^(i*pi) + 1 = 0
local euler = exp(I * PI) + 1
print(string.format("e^(i*pi) + 1 = %g + %g*i (should be ~0)", re(euler), im(euler)))

-- AC magnetics problem: rectangular coil at 60 Hz
openfemm(1)
newdocument(0)
mi_probdef(60, "millimeters", "axi", 1e-8, 0, 30)

-- Coil region (inner radius=5, outer=15, half-height=10)
mi_drawrectangle(5, -10, 15, 10)

-- Bounding air region
mi_drawrectangle(0, -40, 40, 40)

mi_addblocklabel(10, 0)   -- coil center
mi_addblocklabel(30, 30)  -- air

-- Materials
mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0)
mi_addmaterial("Copper", 1, 1, 0, 0, 58, 0, 0, 1, 0)

-- Complex-valued current: 10 A at 45 degrees phase
local Icoil = 10 * exp(I * PI/4)
print(string.format("Coil current: %s A (magnitude %.2f, phase %.1f deg)",
    tostring(Icoil), abs(Icoil), arg(Icoil)*180/PI))

mi_addcircprop("AC_Coil", Icoil, 1)

mi_selectlabel(10, 0)
mi_setblockprop("Copper", 1, 0, "AC_Coil", 0, 0, 1)
mi_clearselected()

mi_selectlabel(30, 30)
mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
mi_clearselected()

-- Boundary: A=0 on top, right, and bottom of bounding box
mi_addboundprop("A=0", 0, 0, 0, 0, 0, 0, 0, 0, 0)
mi_selectsegment(20, -40)   -- bottom
mi_selectsegment(40, 0)     -- right
mi_selectsegment(20, 40)    -- top
mi_setsegmentprop("A=0", 0, 1, 0, 0)
mi_clearselected()

mi_saveas("ac_test.fem")
mi_analyze(1)
mi_loadsolution()

-- Get circuit properties (complex-valued results)
local i_val, v_val, flux = mo_getcircuitproperties("AC_Coil")
print(string.format("Circuit current: %s", tostring(i_val)))
print(string.format("Circuit voltage: %s", tostring(v_val)))
print(string.format("Circuit flux: %s", tostring(flux)))

mo_close()
mi_close()
closefemm()

print("ac_magnetics.lua completed successfully")
