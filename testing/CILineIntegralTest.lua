-- CILineIntegralTest.lua  (FEMM 4.2 current flow -- run on Original FEMM)
--
-- Companion to CILineIntegralTest.py. Exercises every co_lineintegral type (0-3)
-- across cf1 (planar DC), cf3 (axi AC), cf4 (planar AC), cf5 (axi lossy): a J.n
-- contour gives the current through it, and an E.t contour between electrodes
-- gives voltage drop, contour length/area, and average voltage. Each returned
-- component is printed as "re im" so the output matches the femm-qt run field-
-- for-field (compare with CILineIntegralCompare.py). co_lineintegral is complex
-- for the AC/lossy cases.
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

function fmt(z)
  return re(z) .. " " .. im(z)
end

function line(t, label)
  local a, b = co_lineintegral(t)
  print("  [" .. t .. "] " .. label .. " = " .. fmt(a) .. " | " .. fmt(b))
end

function contour(pts)
  co_clearcontour()
  local i = 1
  while pts[i] ~= nil do
    co_addcontour(pts[i][1], pts[i][2])
    i = i + 1
  end
end

function run(fec, jn_pts, et_pts, tag)
  open(fec)
  ci_saveas("temp.fec")
  ci_analyze(1); ci_loadsolution()
  print("===  " .. tag .. "  ===")
  contour(jn_pts); line(1, "J.n (current)")
  contour(et_pts); line(0, "E.t (Vdrop)"); line(2, "length/area"); line(3, "avg V")
  co_close()
end

run("currentFlow1.fec", {{-3, -1.9}, {-2, -1.9}}, {{-2.5, -2}, {0.5, -2}},
    "cf1 (planar, DC)")
run("currentFlow3.fec", {{0, 70}, {35, 70}, {35, 5}, {0, 5}}, {{0, 10}, {0, 0}},
    "cf3 (axi, AC 60Hz)")
run("currentFlow4.fec", {{120, 65}, {180, 65}, {180, 5}, {120, 5}, {120, 65}},
    {{150, 10}, {150, 0}}, "cf4 (planar, AC 60Hz)")
run("currentFlow5.fec", {{0, 0}, {2, 0}}, {{0, 3}, {0, -3}},
    "cf5 (axi, lossy 100kHz)")

print("")
print("Done.")
