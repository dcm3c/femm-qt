-- HeatBlockIntegralTest.lua  (FEMM 4.2 heat flow -- run on Original FEMM)
--
-- Companion to HeatBlockIntegralTest.py. Exercises every heat-flow block-integral
-- type (0-4) on heatFlow1 (axisymmetric) and heatFlow2 (planar). All heat block
-- integrals are block quantities (no force/torque), evaluated over group 1:
-- 0 = average temperature, 1 = cross-section area, 2 = volume, 3 = average heat
-- flux F, 4 = average temperature gradient G. Compare the printed re/im against
-- the femm-qt Python run.
--
-- NOTE: run this from the testing directory (OF does not chdir to the script's
-- folder). If needed, uncomment and edit the setcurrentdirectory line below.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

-- {number, label}
idefs = {
  {0, "Average temperature"},
  {1, "Cross-section area"},
  {2, "Volume"},
  {3, "Average F (heat flux)"},
  {4, "Average G (temp gradient)"}
}

function run(title)
  hi_saveas("temp.feh")   -- analyze a throwaway copy so the .feh fixture stays pristine
  hi_analyze(1)
  hi_loadsolution()
  print("===================  " .. title .. "  ===================")
  local i = 1
  while i <= 5 do
    local d = idefs[i]
    local n = d[1]
    ho_clearblock()
    ho_groupselectblock(1)
    zr, zi = ho_blockintegral(n)
    print("  [" .. n .. "] " .. d[2] .. ":   re=" .. zr .. "   im=" .. zi)
    ho_clearblock()
    i = i + 1
  end
  ho_close()
end

open("heatFlow1.feh")
run("heatFlow1 (axisymmetric)")

open("heatFlow2.feh")
run("heatFlow2 (planar)")

print("")
print("Done.")
