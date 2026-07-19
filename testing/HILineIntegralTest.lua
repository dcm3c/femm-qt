-- HILineIntegralTest.lua  (FEMM 4.2 heat flow -- run on Original FEMM)
--
-- Companion to HILineIntegralTest.py. Exercises every ho_lineintegral type (0-3)
-- on heatFlow1 (axi) and heatFlow2 (planar): a contour wrapping the "Bdry"
-- conductor gives F.n (== conductor heat flow); an axis path gives temperature
-- drop, length/area, and average temperature. Each returned component is printed
-- as "re im" (im == 0, heat is static) so the output matches the femm-qt run
-- field-for-field (compare with HILineIntegralCompare.py). NOTE: on OF the axi
-- avg-T (heatFlow1) runs along r=0 and comes back NaN (2*pi*r-weighted 0/0);
-- femm-qt returns the plain line average there -- the comparator skips the NaN.
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

function fmt(z)
  return re(z) .. " " .. im(z)
end

function line(t, label)
  local a, b = ho_lineintegral(t)
  print("  [" .. t .. "] " .. label .. " = " .. fmt(a) .. " | " .. fmt(b))
end

function contour(pts)
  ho_clearcontour()
  local i = 1
  while pts[i] ~= nil do
    ho_addcontour(pts[i][1], pts[i][2])
    i = i + 1
  end
end

BDRY_BOX = {{0, 70}, {35, 70}, {35, 5}, {0, 5}}
AXIS     = {{0, 10}, {0, 0}}

function run(feh, tag)
  open(feh)
  hi_saveas("temp.feh")
  hi_analyze(1); hi_loadsolution()
  print("===  " .. tag .. "  ===")
  contour(BDRY_BOX); line(1, "F.n (heatflow)")
  contour(AXIS);     line(0, "G.t (Tdrop)"); line(2, "length/area"); line(3, "avg T")
  ho_close()
end

run("heatFlow1.feh", "heatFlow1 (axi)")
run("heatFlow2.feh", "heatFlow2 (planar)")

print("")
print("Done.")
