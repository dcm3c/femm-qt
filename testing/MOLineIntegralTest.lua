-- MOLineIntegralTest.lua  (FEMM 4.2 magnetics -- run on Original FEMM)
--
-- Companion to MOLineIntegralTest.py. Exercises every mo_lineintegral type (0-5)
-- across tutorial/tutorial_planar (H.t == amp-turns, B.n flux, length),
-- RadialMagneticBearing (stress-tensor force on a CW gap circle + (B.n)^2),
-- roters1b (force on a corner-avoiding gap contour, DC and AC), and torque
-- (force + torque on a rectangle wrapping the rotor). Each returned component is
-- printed as "re im" so the output matches the femm-qt Python run field-for-field
-- (compare with MOLineIntegralCompare.py).
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

pi = 3.141592653589793

function fmt(z)
  return re(z) .. " " .. im(z)
end

function line(t, label)
  local a, b, c, d = mo_lineintegral(t)
  local s = "  [" .. t .. "] " .. label .. " = " .. fmt(a) .. " | " .. fmt(b)
  if c ~= nil then
    s = s .. " | " .. fmt(c) .. " | " .. fmt(d)
  end
  print(s)
end

function contour(pts)
  mo_clearcontour()
  local i = 1
  while pts[i] ~= nil do
    mo_addcontour(pts[i][1], pts[i][2])
    i = i + 1
  end
end

function cw_circle(R, dth)
  mo_clearcontour()
  local n = 360 / dth
  local k = 0
  while k <= n do
    local th = -k * dth * pi / 180
    mo_addcontour(R * cos(th), R * sin(th))
    k = k + 1
  end
end

COIL_LOOP = {{0.3, 1.5}, {2, 1.5}, {2, -1.5}, {0.3, -1.5}, {0.3, 1.5}}
ROT_GAP   = {{0, 7}, {0.5, 7}, {0.672, 4.9375}, {0.672, 4}, {0.672, 3},
             {0.672, 2}, {0.672, 1}, {0.672, 0}, {0.672, -1}, {0.672, -1.4}, {0, -1.4}}
TORQ_BOX  = {{-1.25, 0.275}, {5.25, 0.275}, {5.25, -0.275}, {-1.25, -0.275}, {-1.25, 0.275}}

-- ---- tutorial (axi, DC): Ampere's law + B.n flux ----
open("tutorial.fem"); mi_saveas("temp.fem"); mi_probdef(0, "inches", "axi")
mi_analyze(1); mi_loadsolution()
print("===  tutorial (axi, DC)  ===")
contour(COIL_LOOP);          line(1, "H.t (Ampere)")
contour({{0, 0}, {0.5, 0}}); line(0, "B.n flux"); line(2, "length/area")
mo_close()

-- ---- tutorial_planar (planar, DC) ----
open("tutorial_planar.fem"); mi_saveas("temp.fem"); mi_probdef(0, "inches", "planar")
mi_analyze(1); mi_loadsolution()
print("===  tutorial_planar (planar, DC)  ===")
contour(COIL_LOOP);            line(1, "H.t (Ampere)")
contour({{-0.5, 0}, {0.5, 0}}); line(0, "B.n flux"); line(2, "length/area")
mo_close()

-- ---- RMB (planar, AC 60Hz, y-excite i=(6,12,6,0)): Force + (B.n)^2 ----
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem")
mi_modifycircprop("i1", 1, 6); mi_modifycircprop("i2", 1, 12)
mi_modifycircprop("i3", 1, 6); mi_modifycircprop("i4", 1, 0)
mi_probdef(60, "inches", "planar")
mi_analyze(1); mi_loadsolution()
print("===  RMB (planar, AC 60Hz, y-excite)  ===")
cw_circle(1.020, 10); line(3, "Force"); line(5, "(B.n)^2")
mo_close()

-- ---- roters1b (axi): Force, DC and AC ----
freqs = {0, 60}
fi = 1
while freqs[fi] ~= nil do
  local fr = freqs[fi]
  open("roters1b.fem"); mi_saveas("rtemp.fem")
  mi_modifycircprop("Coil Current", 1, 0.8)
  mi_probdef(fr, "inches", "axi")
  mi_analyze(1); mi_loadsolution()
  if fr == 0 then
    print("===  roters1b (axi, DC)  ===")
  else
    print("===  roters1b (axi, AC 60Hz)  ===")
  end
  contour(ROT_GAP); line(3, "Force")
  mo_close()
  fi = fi + 1
end

-- ---- torque (planar, AC 60Hz, i1=2 i2=0): Force + Torque ----
open("torque.fem"); mi_saveas("temp.fem")
mi_modifycircprop("i1", 1, 2); mi_modifycircprop("i2", 1, 0)
mi_probdef(60, "inches", "planar")
mi_analyze(1); mi_loadsolution()
print("===  torque (planar, AC 60Hz)  ===")
contour(TORQ_BOX); line(3, "Force"); line(4, "Torque")
mo_close()

print("")
print("Done.")
