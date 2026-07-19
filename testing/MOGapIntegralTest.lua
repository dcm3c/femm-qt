-- MOGapIntegralTest.lua  (FEMM 4.2 magnetics -- run on Original FEMM)
--
-- Companion to MOGapIntegralTest.py. Air-gap-element postprocessing on two
-- fixtures with an "AGE" boundary: RadialMagneticBearingAGE.fem (gap FORCE, ~0
-- torque) and rolled.fem (a rotating machine -- gap TORQUE). Each runs at DC, AC
-- 60Hz, and incrementally, covering mo_gapintegral types 0-6, mo_getgapharmonics,
-- mo_getgapb and mo_getgapa. Each returned component is printed as "re im", keyed
-- by label, matching the femm-qt run field-for-field (see MOGapIntegralCompare.py).
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

function fmt(z) return re(z) .. " " .. im(z) end
function e1(l, a)                print("  " .. l .. " = " .. fmt(a)) end
function e2(l, a, b)             print("  " .. l .. " = " .. fmt(a) .. " | " .. fmt(b)) end
function e6(l, a, b, c, d, e, f)
  print("  " .. l .. " = " .. fmt(a) .. " | " .. fmt(b) .. " | " .. fmt(c) ..
        " | " .. fmt(d) .. " | " .. fmt(e) .. " | " .. fmt(f))
end

function gapCommon()
  e1("nharm", mo_getgapharmonics("AGE"))
  e6("harm1", mo_getgapharmonics("AGE", 1))
  e6("harm2", mo_getgapharmonics("AGE", 2))
  e6("harm3", mo_getgapharmonics("AGE", 3))
  e6("harm4", mo_getgapharmonics("AGE", 4))
  e2("gapB@0",   mo_getgapb("AGE", 0))
  e2("gapB@45",  mo_getgapb("AGE", 45))
  e2("gapB@90",  mo_getgapb("AGE", 90))
  e2("gapB@135", mo_getgapb("AGE", 135))
  e1("gapA@90", mo_getgapa("AGE", 90))
end

function steady()
  e1("torque", mo_gapintegral("AGE", 0))
  e2("force",  mo_gapintegral("AGE", 1))
  e1("energy", mo_gapintegral("AGE", 2))
end

function rmb(i1, i2, i3, i4)
  mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
  mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)
end

-- ============ RadialMagneticBearingAGE (force) ============
open("RadialMagneticBearingAGE.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB-AGE (DC, y-excite)  ==="); steady(); gapCommon(); mo_close()

open("RadialMagneticBearingAGE.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB-AGE (AC 60Hz, y-excite)  ===")
steady()
e1("torque2x", mo_gapintegral("AGE", 3))
e2("force2x",  mo_gapintegral("AGE", 4))
gapCommon(); mo_close()

open("RadialMagneticBearingAGE.fem"); mi_saveas("bias.fem"); rmb(6, 6, 6, 6)
mi_probdef(0, "inches", "planar"); mi_analyze(1)
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); rmb(0, 1, 0, -1)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB-AGE (Incremental AC, y-perturb)  ===")
e1("torqueinc", mo_gapintegral("AGE", 5))
e2("forceinc",  mo_gapintegral("AGE", 6))
gapCommon(); mo_close()

-- ============ rolled (torque) ============
open("rolled.fem"); mi_saveas("temp.fem"); mi_modifycircprop("icoil", 1, 20)
mi_probdef(0, "millimeters", "planar"); mi_analyze(1); mi_loadsolution()
print("===  rolled (DC, icoil=20)  ==="); steady(); gapCommon(); mo_close()

open("rolled.fem"); mi_saveas("temp.fem"); mi_modifycircprop("icoil", 1, 20)
mi_probdef(60, "millimeters", "planar"); mi_analyze(1); mi_loadsolution()
print("===  rolled (AC 60Hz, icoil=20)  ===")
steady()
e1("torque2x", mo_gapintegral("AGE", 3))
e2("force2x",  mo_gapintegral("AGE", 4))
gapCommon(); mo_close()

open("rolled.fem"); mi_saveas("bias.fem"); mi_modifycircprop("icoil", 1, 20)
mi_probdef(0, "millimeters", "planar"); mi_analyze(1)
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); mi_modifycircprop("icoil", 1, 1)
mi_probdef(60, "millimeters", "planar"); mi_analyze(1); mi_loadsolution()
print("===  rolled (Incremental AC, icoil=1)  ===")
e1("torqueinc", mo_gapintegral("AGE", 5))
e2("forceinc",  mo_gapintegral("AGE", 6))
gapCommon(); mo_close()

print("")
print("Done.")
