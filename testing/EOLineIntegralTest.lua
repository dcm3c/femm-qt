-- EOLineIntegralTest.lua  (FEMM 4.2 electrostatics -- run on Original FEMM)
--
-- Companion to EOLineIntegralTest.py. Exercises every eo_lineintegral type (0-4)
-- on electrostatic1 (axi) and electrostatic2 (planar): a contour wrapping the
-- "+10000V" conductor gives D.n (== enclosed charge), stress-tensor Force and
-- Torque, and contour length/area; an E.t path to ground gives the voltage drop.
-- Each returned component is printed as "re im" (im == 0, electrostatics is
-- static) so the output matches the femm-qt run field-for-field (compare with
-- EOLineIntegralCompare.py).
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

function fmt(z)
  return re(z) .. " " .. im(z)
end

function line(t, label)
  local a, b = eo_lineintegral(t)
  print("  [" .. t .. "] " .. label .. " = " .. fmt(a) .. " | " .. fmt(b))
end

function contour(pts)
  eo_clearcontour()
  local i = 1
  while pts[i] ~= nil do
    eo_addcontour(pts[i][1], pts[i][2])
    i = i + 1
  end
end

E1_BOX = {{0, 70}, {35, 70}, {35, 5}, {0, 5}}
E1_ET  = {{0, 10}, {0, 0}}
E2_BOX = {{120, 65}, {180, 65}, {180, 5}, {120, 5}, {120, 65}}
E2_ET  = {{150, 10}, {150, 0}}

-- ---- electrostatic1 (axi) ----
open("electrostatic1.fee"); ei_saveas("temp.fee")
ei_analyze(1); ei_loadsolution()
print("===  electrostatic1 (axi)  ===")
contour(E1_BOX); line(1, "D.n (charge)"); line(3, "Force"); line(2, "length/area")
contour(E1_ET);  line(0, "E.t (Vdrop)")
eo_close()

-- ---- electrostatic2 (planar) ----
open("electrostatic2.fee"); ei_saveas("temp.fee")
ei_analyze(1); ei_loadsolution()
print("===  electrostatic2 (planar)  ===")
contour(E2_BOX); line(1, "D.n (charge)"); line(3, "Force"); line(4, "Torque"); line(2, "length/area")
contour(E2_ET);  line(0, "E.t (Vdrop)")
eo_close()

print("")
print("Done.")
