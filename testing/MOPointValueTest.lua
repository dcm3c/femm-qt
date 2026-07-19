-- MOPointValueTest.lua  (FEMM 4.2 magnetics -- run on Original FEMM)
--
-- Companion to MOPointValueTest.py. mo_getpointvalues (A, B1, B2, sigma, E, H1,
-- H2, Je, Js, mu1, mu2, Pe, Ph, ff) at interior points covering air gap, high-
-- flux iron, coil, and magnet across RMB / roters1b / torque / PMTest, at DC and
-- AC 60Hz. Each point prints its 14 values as "re im" pairs, keyed by a material-
-- type label, matching the femm-qt run (compare with MOPointValueCompare.py).
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole(); clearconsole()

function fmt(z) return re(z) .. " " .. im(z) end

function pt(label, x, y)
  A, B1, B2, sig, E, H1, H2, Je, Js, mu1, mu2, Pe, Ph, ff = mo_getpointvalues(x, y)
  print("  " .. label .. " = " .. fmt(A) .. " | " .. fmt(B1) .. " | " .. fmt(B2) ..
        " | " .. fmt(sig) .. " | " .. fmt(E) .. " | " .. fmt(H1) .. " | " .. fmt(H2) ..
        " | " .. fmt(Je) .. " | " .. fmt(Js) .. " | " .. fmt(mu1) .. " | " .. fmt(mu2) ..
        " | " .. fmt(Pe) .. " | " .. fmt(Ph) .. " | " .. fmt(ff))
end

function rmb(i1, i2, i3, i4)
  mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
  mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)
end
function rmbPts()
  pt("coilA", 1.313, 0.933); pt("coilB", 1.563, -0.273)
  pt("rotor-iron", 0, 0.9); pt("stator-pole", -0.47, 1.17)
  pt("air-gap", 0.395674, 0.942145)
end
function rotPts()
  pt("coil", 1.125, 3.75); pt("armature-iron", 1.65633, 4.64947); pt("gap", 0.610868, 5.71208)
end
function tqPts()
  pt("coilA", 1.665, 1.915); pt("coilB", 2.795, 1.585)
  pt("rotor-iron", 3, 0); pt("stator-iron", 3, 3); pt("air-gap", 3.75, 0.275)
end
function pmPts()
  pt("magnetL", -15, -10); pt("magnetR", 15, -10); pt("air-mid", 0, 0)
  pt("keeper", 0, -20); pt("air-gap", 15, 0)
end

open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (DC, y-excite)  ==="); rmbPts(); mo_close()

open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (AC 60Hz, y-excite)  ==="); rmbPts(); mo_close()

open("roters1b.fem"); mi_saveas("rtemp.fem"); mi_modifycircprop("Coil Current", 1, 0.8)
mi_probdef(0, "inches", "axi"); mi_analyze(1); mi_loadsolution()
print("===  roters1b (DC, 0.8A)  ==="); rotPts(); mo_close()

open("roters1b.fem"); mi_saveas("rtemp.fem"); mi_modifycircprop("Coil Current", 1, 0.8)
mi_probdef(60, "inches", "axi"); mi_analyze(1); mi_loadsolution()
print("===  roters1b (AC 60Hz, 0.8A)  ==="); rotPts(); mo_close()

function torq(a, b) mi_modifycircprop("i1", 1, a); mi_modifycircprop("i2", 1, b) end
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  torque (DC, i=(2,0))  ==="); tqPts(); mo_close()

open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  torque (AC 60Hz, i=(2,0))  ==="); tqPts(); mo_close()

open("PMTest.fem"); mi_saveas("temp.fem")
mi_probdef(0, "centimeters", "planar"); mi_analyze(1); mi_loadsolution()
print("===  PMTest (DC, magnets)  ==="); pmPts(); mo_close()

print(""); print("Done.")
