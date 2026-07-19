-- ElecBlockIntegralTest.lua  (FEMM 4.2 electrostatics -- run on Original FEMM)
--
-- Companion to ElecBlockIntegralTest.py. Exercises every electrostatics
-- block-integral type (0-6) on electrostatic1 (axisymmetric) and electrostatic2
-- (planar). Types 0-4 (stored energy, cross-section area, volume, average D,
-- average E) are evaluated over group 1 (the whole modeled domain); types 5-6
-- (weighted-stress-tensor force/torque) on the "+10000V" conductor. Compare the
-- printed re/im against the femm-qt Python run.
--
-- NOTE: run this from the testing directory (OF does not chdir to the script's
-- folder). If needed, uncomment and edit the setcurrentdirectory line below.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

-- {number, label}
idefs = {
  {0, "Stored energy"},
  {1, "Cross-section area"},
  {2, "Volume"},
  {3, "Average D"},
  {4, "Average E"},
  {5, "WST force"},
  {6, "WST torque"}
}

function run(title)
  ei_saveas("temp.fee")   -- analyze a throwaway copy so the .fee fixture stays pristine
  ei_analyze(1)
  ei_loadsolution()
  print("===================  " .. title .. "  ===================")
  local i = 1
  while i <= 7 do
    local d = idefs[i]
    local n = d[1]
    eo_clearblock()
    if n >= 5 then
      eo_selectconductor("+10000V")   -- WST force/torque acts on the conductor
    else
      eo_groupselectblock(1)          -- block quantities over group 1
    end
    zr, zi = eo_blockintegral(n)
    print("  [" .. n .. "] " .. d[2] .. ":   re=" .. zr .. "   im=" .. zi)
    eo_clearblock()
    i = i + 1
  end
  eo_close()
end

open("electrostatic1.fee")
run("electrostatic1 (axisymmetric)")

open("electrostatic2.fee")
run("electrostatic2 (planar)")

print("")
print("Done.")
