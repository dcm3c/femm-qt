-- EOCondPropTest.lua  (FEMM 4.2 electrostatics -- run on Original FEMM)
-- Companion to EOCondPropTest.py. eo_getconductorproperties (voltage, charge) on
-- the "+10000V" conductor of electrostatic1/2. Prints "V | q" as two "re im"
-- pairs (compare with CondPropCompare.py). Run from the testing directory.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function cond(name)
  V, q = eo_getconductorproperties(name)
  print("  " .. name .. " = " .. fmt(V) .. " | " .. fmt(q))
end
function eo_run(fee, tag)
  open(fee); ei_saveas("temp.fee"); ei_analyze(1); ei_loadsolution()
  print("===  " .. tag .. "  ===")
end

eo_run("electrostatic1.fee", "electrostatic1 (axi)");    cond("+10000V"); eo_close()
eo_run("electrostatic2.fee", "electrostatic2 (planar)"); cond("+10000V"); eo_close()
print(""); print("Done.")
