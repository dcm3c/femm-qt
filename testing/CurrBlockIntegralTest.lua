-- CurrBlockIntegralTest.lua  (FEMM 4.2 current flow -- run on Original FEMM)
--
-- Companion to CurrBlockIntegralTest.py. Exercises every current-flow block-
-- integral type (0-11) across six models: currentFlow1/2 (planar/axi DC),
-- currentFlow3/4 (axi/planar force-torque, complex conductor voltage), and
-- currentFlow5/6 (axi/planar lossy dielectric at 100 kHz). Block quantities
-- (0-5) are taken over group 1; the weighted-stress-tensor force/torque (6-11)
-- act on the "+10000V" conductor (the force models 3 and 4). co_blockintegral
-- returns a single complex, so re()/im() split it. Compare against the femm-qt
-- Python run.
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

-- {number, label, "block"|"cond"}
idefs = {
  {0,  "Real power",             "block"},
  {1,  "Reactive power",         "block"},
  {2,  "Apparent power",         "block"},
  {3,  "Time-avg stored energy", "block"},
  {4,  "Cross-section area",     "block"},
  {5,  "Volume",                 "block"},
  {6,  "WST force x/r (SS)",     "cond"},
  {7,  "WST force y/z (SS)",     "cond"},
  {8,  "WST force x/r (2x)",     "cond"},
  {9,  "WST force y/z (2x)",     "cond"},
  {10, "WST torque (SS)",        "cond"},
  {11, "WST torque (2x)",        "cond"}
}

function run(title, lo, hi, conductor)
  ci_saveas("temp.fec")   -- analyze a throwaway copy so the .fec fixture stays pristine
  ci_analyze(1)
  ci_loadsolution()
  print("===================  " .. title .. "  ===================")
  local i = 1
  while i <= 12 do
    local d = idefs[i]
    local n = d[1]
    if (n >= lo) and (n <= hi) then
      co_clearblock()
      if d[3] == "cond" then
        co_selectconductor(conductor)
      else
        co_groupselectblock(1)
      end
      z = co_blockintegral(n)
      print("  [" .. n .. "] " .. d[2] .. ":   re=" .. re(z) .. "   im=" .. im(z))
      co_clearblock()
    end
    i = i + 1
  end
  co_close()
end

open("currentFlow1.fec")
run("currentFlow1 (planar, DC)", 0, 5, "")
open("currentFlow2.fec")
run("currentFlow2 (axisymmetric, DC)", 0, 5, "")

open("currentFlow3.fec")
run("currentFlow3 (axisymmetric, force/torque)", 0, 11, "+10000V")
open("currentFlow4.fec")
run("currentFlow4 (planar, force/torque)", 0, 11, "+10000V")

open("currentFlow5.fec")
run("currentFlow5 (axisymmetric, lossy dielectric)", 0, 5, "")
open("currentFlow6.fec")
run("currentFlow6 (planar, lossy dielectric)", 0, 5, "")

print("")
print("Done.")
