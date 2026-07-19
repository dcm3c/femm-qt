-- MOBlockIntegralTest.lua  (FEMM 4.2 / Lua 4 -- run on Original FEMM)
--
-- Companion to MOBlockIntegralTest.py. Exercises every mo_blockintegral type
-- (0-30) at DC, AC, and incremental-AC on the RadialMagneticBearing (planar)
-- and roters1b (axisymmetric) models, printing re and im of each. Compare the
-- printed values against the femm-qt Python run to set pass/fail tolerances:
-- OF returns a complex for every integral, so a femm-qt "real" result should
-- match re here (with im negligible), and a "complex" result should match
-- re + I*im.
--
-- NOTE: run this from the testing directory (OF does not chdir to the script's
-- folder). If needed, uncomment and edit the setcurrentdirectory line below.
-- setcurrentdirectory("C:/Claude/uPython/femm-qt/testing")

showconsole()
clearconsole()

-- {number, label, kind}  (kind = which region to select)
idefs = {
  { 0, "A.J",                                   "coil"},
  { 1, "Integral of A over the block",          "coil"},
  { 2, "Stored energy",                         "rotor"},
  { 3, "Hysteresis & laminated eddy losses",    "rotor"},
  { 4, "Resistive losses",                      "coil"},
  { 5, "Cross-section area",                    "rotor"},
  { 6, "Total losses",                          "rotor"},
  { 7, "Total current",                         "coil"},
  { 8, "Integral of Bx (or Br)",                "rotor"},
  { 9, "Integral of By (or Bz)",                "rotor"},
  {10, "Volume",                                "rotor"},
  {11, "Lorentz force, x/r, steady-state",      "coil"},
  {12, "Lorentz force, y/z, steady-state",      "coil"},
  {13, "Lorentz force, x/r, 2x",                "coil"},
  {14, "Lorentz force, y/z, 2x",                "coil"},
  {15, "Lorentz torque, steady-state",          "coil"},
  {16, "Lorentz torque, 2x",                    "coil"},
  {17, "Coenergy",                              "rotor"},
  {18, "Henrotte force, x/r, steady-state",     "rotor"},
  {19, "Henrotte force, y/z, steady-state",     "rotor"},
  {20, "Henrotte force, x/r, 2x",               "rotor"},
  {21, "Henrotte force, y/z, 2x",               "rotor"},
  {22, "Henrotte torque, steady-state",         "rotor"},
  {23, "Henrotte torque, 2x",                   "rotor"},
  {24, "Moment-of-inertia-like integral",       "rotor"},
  {25, "Henrotte force, x/r, 1x (incremental)", "rotor"},
  {26, "Henrotte force, y/z, 1x (incremental)", "rotor"},
  {27, "Henrotte torque, 1x (incremental)",     "rotor"},
  {28, "Lorentz force, x/r, 1x (incremental)",  "coil"},
  {29, "Lorentz force, y/z, 1x (incremental)",  "coil"},
  {30, "Lorentz torque, 1x (incremental)",      "coil"}
}

function run(title, lo, hi, selector)
  mi_analyze(1)
  mi_loadsolution()
  print("===================  " .. title .. "  ===================")
  local i = 1
  while i <= 31 do
    local d = idefs[i]
    local n = d[1]
    if (n >= lo) and (n <= hi) then
      selector(d[3], n)
      z = mo_blockintegral(n)
      print("  [" .. n .. "] " .. d[2] .. ":   re=" .. re(z) .. "   im=" .. im(z))
      mo_clearblock()
    end
    i = i + 1
  end
  mo_close()
end

-- =========================================================================
-- Model 1: RadialMagneticBearing  (planar, 4 coil circuits i1-i4)
-- =========================================================================
function rmb_select(kind)
  mo_clearblock()
  if kind == "rotor" then
    mo_groupselectblock(1)   -- steel rotor washer
  else
    mo_groupselectblock(2)   -- i1 coils (current-carrying)
  end
end

function rmb_currents(i1, i2, i3, i4)
  mi_modifycircprop("i1", 1, i1)
  mi_modifycircprop("i2", 1, i2)
  mi_modifycircprop("i3", 1, i3)
  mi_modifycircprop("i4", 1, i4)
end

-- x-direction excitation: i1 (the ~0 deg / +x pole) dominant
open("RadialMagneticBearing.fem")
mi_saveas("temp.fem")
rmb_currents(12, 6, 0, 6)
mi_probdef(0, "inches", "planar")
run("RMB (planar, x)  DC   i1=12 i2=6 i3=0 i4=6", 0, 24, rmb_select)

mi_probdef(60, "inches", "planar")
run("RMB (planar, x)  AC 60Hz   i1=12 i2=6 i3=0 i4=6", 0, 23, rmb_select)

open("RadialMagneticBearing.fem")
mi_saveas("bias.fem")
rmb_currents(6, 6, 6, 6)
mi_probdef(0, "inches", "planar")
mi_analyze(1)                -- -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
rmb_currents(1, 0, -1, 0)    -- i1/i3 differential -> x-direction perturbation
mi_probdef(60, "inches", "planar")
run("RMB (planar, x)  Incremental AC   bias 6A all; AC i1=1 i2=0 i3=-1 i4=0",
    18, 30, rmb_select)

-- y-direction excitation: i2 (the ~90 deg / +y pole) dominant
open("RadialMagneticBearing.fem")
mi_saveas("temp.fem")
rmb_currents(6, 12, 6, 0)
mi_probdef(0, "inches", "planar")
run("RMB (planar, y)  DC   i1=6 i2=12 i3=6 i4=0", 0, 24, rmb_select)

mi_probdef(60, "inches", "planar")
run("RMB (planar, y)  AC 60Hz   i1=6 i2=12 i3=6 i4=0", 0, 23, rmb_select)

open("RadialMagneticBearing.fem")
mi_saveas("bias.fem")
rmb_currents(6, 6, 6, 6)
mi_probdef(0, "inches", "planar")
mi_analyze(1)                -- -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
rmb_currents(0, 1, 0, -1)    -- i2/i4 differential -> y-direction perturbation
mi_probdef(60, "inches", "planar")
run("RMB (planar, y)  Incremental AC   bias 6A all; AC i1=0 i2=1 i3=0 i4=-1",
    18, 30, rmb_select)

-- =========================================================================
-- Model 2: roters1b  (axisymmetric, single coil "Coil Current")
-- =========================================================================
function rot_select(kind)
  mo_clearblock()
  if kind == "rotor" then
    mo_groupselectblock(1)        -- steel armature (the moving body)
  else
    mo_selectblock(1.125, 3.75)   -- the coil block (group 0 -> point-select)
  end
end

function rot_current(a)
  mi_modifycircprop("Coil Current", 1, a)
end

open("roters1b.fem")
mi_saveas("rtemp.fem")
rot_current(0.8)             -- nominal
mi_probdef(0, "inches", "axi")
run("roters1b (axi)  DC   coil=0.8A", 0, 24, rot_select)

mi_probdef(60, "inches", "axi")
run("roters1b (axi)  AC 60Hz   coil=0.8A", 0, 23, rot_select)

open("roters1b.fem")
mi_saveas("rbias.fem")
rot_current(0.8)             -- DC bias
mi_probdef(0, "inches", "axi")
mi_analyze(1)                -- -> rbias.ans
mi_saveas("rincr.fem")
mi_setprevious("rbias.ans", 1)
rot_current(0.1)             -- small-signal AC perturbation about the bias
mi_probdef(60, "inches", "axi")
run("roters1b (axi)  Incremental AC   bias 0.8A; AC 0.1A", 18, 30, rot_select)

-- =========================================================================
-- Model 3: torque  (planar, purpose-built for torque; rotor = group 1)
--   Two magnets drive circuits i1/i2; the steel rotor (group 1) sees a torque
--   about the origin. Torque on the rotor is the Henrotte torque 22/23/27;
--   Lorentz torque (15/16/30) is ~0 since the rotor carries no source current.
--   (Axisymmetric torque is identically zero, so there is no axi torque case.)
-- =========================================================================
function torque_select(kind)
  mo_clearblock()
  mo_groupselectblock(1)        -- the steel rotor (no separate coil group here)
end

function torque_currents(a, b)
  mi_modifycircprop("i1", 1, a)
  mi_modifycircprop("i2", 1, b)
end

open("torque.fem")
mi_saveas("temp.fem")
torque_currents(2, 0)
mi_probdef(0, "inches", "planar")
run("torque (planar)  DC   i1=2 i2=0", 11, 24, torque_select)

mi_probdef(60, "inches", "planar")
run("torque (planar)  AC 60Hz   i1=2 i2=0", 11, 23, torque_select)

open("torque.fem")
mi_saveas("bias.fem")
torque_currents(1, 1)
mi_probdef(0, "inches", "planar")
mi_analyze(1)                -- -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
torque_currents(1, -1)
mi_probdef(60, "inches", "planar")
run("torque (planar)  Incremental AC   bias i1=1 i2=1; AC i1=1 i2=-1",
    18, 30, torque_select)

-- =========================================================================
-- Models 4-6: Lorentz force / torque on a current-carrying coil (group 2).
--   A coil (group 2) sits near an iron piece (group 1) whose induced
--   magnetization gives the coil a net Lorentz force.
--     AxisymmetricLorentzForce : axial (z) Lorentz force [12]
--     PlanarLorentzForce       : coil 4in off-axis -> y-force [12] + torque [15]
--     PlanarLorentzForceAlt    : rotated 90deg -> x-force [11] + torque [15]
--   DC/AC use i1=16; incremental uses bias i1=16, perturbation i1=1.
-- =========================================================================
function lor_select(kind)
  mo_clearblock()
  mo_groupselectblock(2)        -- the coil
end

function lor_current(a)
  mi_modifycircprop("i1", 1, a)
end

function run_lorentz(name, femfile, ptype)
  open(femfile)
  mi_saveas("temp.fem")
  lor_current(16)
  mi_probdef(0, "inches", ptype)
  run(name .. "  DC   i1=16", 0, 24, lor_select)

  mi_probdef(60, "inches", ptype)
  run(name .. "  AC 60Hz   i1=16", 0, 23, lor_select)

  open(femfile)
  mi_saveas("bias.fem")
  lor_current(16)
  mi_probdef(0, "inches", ptype)
  mi_analyze(1)                -- -> bias.ans
  mi_saveas("incr.fem")
  mi_setprevious("bias.ans", 1)
  lor_current(1)
  mi_probdef(60, "inches", ptype)
  run(name .. "  Incremental AC   bias i1=16; AC i1=1", 18, 30, lor_select)
end

run_lorentz("AxiLorentz (axi)",        "AxisymmetricLorentzForce.fem", "axi")
run_lorentz("PlanarLorentz-y (planar)", "PlanarLorentzForce.fem",      "planar")
run_lorentz("PlanarLorentz-x (planar)", "PlanarLorentzForceAlt.fem",   "planar")

-- =========================================================================
-- Model 7: PMTest  (planar, nonlinear permanent magnets = group 1)
--   Knock-off of QuickField's magn1: two mildly-nonlinear magnets (group 1,
--   H_c>0 with a BH curve) drive flux through steel, with graded-permeability
--   shells approximating an open boundary. The only model that exercises the PM
--   stored-energy second-quadrant correction (type 2, via DoEnergy/Nrg/GetB)
--   and the coenergy path (type 17) on a magnet. No coils/circuits, so the
--   current-carrying integrals are ~0 and DC is the only meaningful case.
-- =========================================================================
-- Weighted-stress-tensor (Henrotte) force/torque types: only meaningful when
-- the selected region is surrounded by air.
wst_types = {[18]=1,[19]=1,[20]=1,[21]=1,[22]=1,[23]=1,[25]=1,[26]=1,[27]=1}

function pm_select(kind, n)
  mo_clearblock()
  -- The magnets (group 1) sit on their steel keeper (group 3). Group 1 alone is
  -- bounded by iron, so its WST force is meaningless; the assembly 1+3 is the
  -- air-surrounded movable body. Select 1+3 for the force/torque integrals and
  -- just the magnets (group 1) for energy/coenergy and the rest.
  mo_groupselectblock(1)
  if wst_types[n]==1 then
    mo_groupselectblock(3)      -- add the steel keeper -> air-surrounded assembly
  end
end

open("PMTest.fem")
mi_saveas("temp.fem")
mi_probdef(0, "centimeters", "planar")
run("PMTest (planar)  DC   nonlinear magnets, group 1", 0, 24, pm_select)

print("")
print("Done.")
