-- MOCircuitPropTest.lua  (FEMM 4.2 magnetics -- run on Original FEMM)
--
-- Companion to MOCircuitPropTest.py. Queries mo_getcircuitproperties (current,
-- voltage drop, flux linkage) across the magnetics block-integral fixtures at DC,
-- AC 60Hz, and incrementally: RadialMagneticBearing (i1-i4), roters1b (Coil
-- Current), torque (i1,i2), the three Lorentz coils (i1). Each circuit prints
-- "I | V | Flux" as three "re im" pairs, matching the femm-qt run field-for-field
-- (compare with MOCircuitPropCompare.py).
--
-- NOTE: run from the testing directory (OF does not chdir to the script folder).
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

function fmt(z) return re(z) .. " " .. im(z) end

function circ(name)
  I, V, F = mo_getcircuitproperties(name)
  print("  " .. name .. " = " .. fmt(I) .. " | " .. fmt(V) .. " | " .. fmt(F))
end

function rmb(i1, i2, i3, i4)
  mi_modifycircprop("i1", 1, i1); mi_modifycircprop("i2", 1, i2)
  mi_modifycircprop("i3", 1, i3); mi_modifycircprop("i4", 1, i4)
end
function rmbAll() circ("i1"); circ("i2"); circ("i3"); circ("i4") end

-- ---- RadialMagneticBearing ----
open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(12, 6, 0, 6)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (x, DC) i=(12,6,0,6)  ==="); rmbAll(); mo_close()

open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(12, 6, 0, 6)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (x, AC 60Hz) i=(12,6,0,6)  ==="); rmbAll(); mo_close()

open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (y, DC) i=(6,12,6,0)  ==="); rmbAll(); mo_close()

open("RadialMagneticBearing.fem"); mi_saveas("temp.fem"); rmb(6, 12, 6, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (y, AC 60Hz) i=(6,12,6,0)  ==="); rmbAll(); mo_close()

open("RadialMagneticBearing.fem"); mi_saveas("bias.fem"); rmb(6, 6, 6, 6)
mi_probdef(0, "inches", "planar"); mi_analyze(1)
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); rmb(1, 0, -1, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  RMB (x-incremental) bias 6A; AC i=(1,0,-1,0)  ==="); rmbAll(); mo_close()

-- ---- roters1b ----
function rot(a) mi_modifycircprop("Coil Current", 1, a) end
open("roters1b.fem"); mi_saveas("rtemp.fem"); rot(0.8)
mi_probdef(0, "inches", "axi"); mi_analyze(1); mi_loadsolution()
print("===  roters1b (DC) 0.8A  ==="); circ("Coil Current"); mo_close()

open("roters1b.fem"); mi_saveas("rtemp.fem"); rot(0.8)
mi_probdef(60, "inches", "axi"); mi_analyze(1); mi_loadsolution()
print("===  roters1b (AC 60Hz) 0.8A  ==="); circ("Coil Current"); mo_close()

open("roters1b.fem"); mi_saveas("rbias.fem"); rot(0.8)
mi_probdef(0, "inches", "axi"); mi_analyze(1)
mi_saveas("rincr.fem"); mi_setprevious("rbias.ans", 1); rot(0.1)
mi_probdef(60, "inches", "axi"); mi_analyze(1); mi_loadsolution()
print("===  roters1b (incremental) bias 0.8A; AC 0.1A  ==="); circ("Coil Current"); mo_close()

-- ---- torque ----
function torq(a, b) mi_modifycircprop("i1", 1, a); mi_modifycircprop("i2", 1, b) end
open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0)
mi_probdef(0, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  torque (DC) i=(2,0)  ==="); circ("i1"); circ("i2"); mo_close()

open("torque.fem"); mi_saveas("temp.fem"); torq(2, 0)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  torque (AC 60Hz) i=(2,0)  ==="); circ("i1"); circ("i2"); mo_close()

open("torque.fem"); mi_saveas("bias.fem"); torq(1, 1)
mi_probdef(0, "inches", "planar"); mi_analyze(1)
mi_saveas("incr.fem"); mi_setprevious("bias.ans", 1); torq(1, -1)
mi_probdef(60, "inches", "planar"); mi_analyze(1); mi_loadsolution()
print("===  torque (incremental) bias i=(1,1); AC i=(1,-1)  ==="); circ("i1"); circ("i2"); mo_close()

-- ---- Lorentz coils ----
function lorRun(fem, ptype, tag)
  function lor(a) mi_modifycircprop("i1", 1, a) end
  open(fem); mi_saveas("temp.fem"); lor(16)
  mi_probdef(0, "inches", ptype); mi_analyze(1); mi_loadsolution()
  print("===  " .. tag .. " (DC) 16A  ==="); circ("i1"); mo_close()
  open(fem); mi_saveas("temp.fem"); lor(16)
  mi_probdef(60, "inches", ptype); mi_analyze(1); mi_loadsolution()
  print("===  " .. tag .. " (AC 60Hz) 16A  ==="); circ("i1"); mo_close()
end
lorRun("AxisymmetricLorentzForce.fem", "axi",    "AxiLorentz")
lorRun("PlanarLorentzForce.fem",       "planar", "PlanarLorentz-y")
lorRun("PlanarLorentzForceAlt.fem",    "planar", "PlanarLorentz-x")

print("")
print("Done.")
