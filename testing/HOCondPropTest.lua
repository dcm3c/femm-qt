-- HOCondPropTest.lua  (FEMM 4.2 heat flow -- run on Original FEMM)
-- Companion to HOCondPropTest.py. ho_getconductorproperties (temperature, heat
-- flow) on the "Bdry" conductor of heatFlow1/2. Prints "T | q" as two "re im"
-- pairs (compare with CondPropCompare.py). Run from the testing directory.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole(); clearconsole()
function fmt(z) return re(z) .. " " .. im(z) end
function cond(name)
  T, q = ho_getconductorproperties(name)
  print("  " .. name .. " = " .. fmt(T) .. " | " .. fmt(q))
end
function ho_run(feh, tag)
  open(feh); hi_saveas("temp.feh"); hi_analyze(1); hi_loadsolution()
  print("===  " .. tag .. "  ===")
end

ho_run("heatFlow1.feh", "heatFlow1 (axi)");    cond("Bdry"); ho_close()
ho_run("heatFlow2.feh", "heatFlow2 (planar)"); cond("Bdry"); ho_close()
print(""); print("Done.")
