# MOBlockIntegralTest.py
# Exercises every mo_blockintegral type (0-30) at DC, AC, and incremental-AC on
# two models -- RadialMagneticBearing (planar) and roters1b (axisymmetric) --
# and reports each value and its Python type (real vs complex). Cross-checks the
# real/complex typing against femm-qt/docs/mo_blockintegral_cases.csv: at DC
# every integral is real; at AC a specific subset is complex.
#
# Per model:
#   ROTOR/ARMATURE = group 1  -- the steel body the weighted-stress-tensor
#            (Henrotte) force/torque acts on, plus its energy/area/volume/moment.
#   COIL           -- the current-carrying winding; A.J, total current,
#            resistive loss, and the Lorentz (J x B) forces.
#
# In the axisymmetric model, the physical force is AXIAL (the y/z-direction
# integrals); radial (x/r) force and torque net to ~0 by symmetry.

try:
    showconsole(); clearconsole()
except NameError:
    pass   # headless: no GUI console

# (number, label, units, selection) for every integral type.
INTEGRALS = [
    (0,  "A.J",                                   "H*A^2",    "coil"),
    (1,  "Integral of A over the block",          "H*A*m^2",  "coil"),
    (2,  "Stored energy",                         "J",        "rotor"),
    (3,  "Hysteresis & laminated eddy losses",    "W",        "rotor"),
    (4,  "Resistive losses",                      "W",        "coil"),
    (5,  "Cross-section area",                    "m^2",      "rotor"),
    (6,  "Total losses",                          "W",        "rotor"),
    (7,  "Total current",                         "A",        "coil"),
    (8,  "Integral of Bx (or Br)",                "T*m^2",    "rotor"),
    (9,  "Integral of By (or Bz)",                "T*m^2",    "rotor"),
    (10, "Volume",                                "m^3",      "rotor"),
    (11, "Lorentz force, x/r, steady-state",      "N",        "coil"),
    (12, "Lorentz force, y/z, steady-state",      "N",        "coil"),
    (13, "Lorentz force, x/r, 2x",                "N",        "coil"),
    (14, "Lorentz force, y/z, 2x",                "N",        "coil"),
    (15, "Lorentz torque, steady-state",          "N*m",      "coil"),
    (16, "Lorentz torque, 2x",                    "N*m",      "coil"),
    (17, "Coenergy",                              "J",        "rotor"),
    (18, "Henrotte force, x/r, steady-state",     "N",        "rotor"),
    (19, "Henrotte force, y/z, steady-state",     "N",        "rotor"),
    (20, "Henrotte force, x/r, 2x",               "N",        "rotor"),
    (21, "Henrotte force, y/z, 2x",               "N",        "rotor"),
    (22, "Henrotte torque, steady-state",         "N*m",      "rotor"),
    (23, "Henrotte torque, 2x",                   "N*m",      "rotor"),
    (24, "Moment-of-inertia-like integral",       "kg*m^2",   "rotor"),
    (25, "Henrotte force, x/r, 1x (incremental)", "N",        "rotor"),
    (26, "Henrotte force, y/z, 1x (incremental)", "N",        "rotor"),
    (27, "Henrotte torque, 1x (incremental)",     "N*m",      "rotor"),
    (28, "Lorentz force, x/r, 1x (incremental)",  "N",        "coil"),
    (29, "Lorentz force, y/z, 1x (incremental)",  "N",        "coil"),
    (30, "Lorentz torque, 1x (incremental)",      "N*m",      "coil"),
]

def run(title, nums, selector):
    mi_analyze()
    mi_loadsolution()
    print("\n===================  " + title + "  ===================")
    for (n, label, units, kind) in INTEGRALS:
        if n not in nums:
            continue
        selector(kind, n)
        z = mo_blockintegral(n)
        kindstr = "complex" if isinstance(z, complex) else "real"
        print("  [%2d] %-42s = %-24s %-8s (%s)" % (n, label, repr(z), units, kindstr))
        mo_clearblock()
    mo_close()

# ==========================================================================
# Model 1: RadialMagneticBearing  (planar, 4 coil circuits i1-i4)
# ==========================================================================
def rmb_select(kind, n=None):
    mo_clearblock()
    if kind == "rotor":
        mo_groupselectblock(1)   # steel rotor washer
    else:
        mo_groupselectblock(2)   # i1 coils (current-carrying)

def rmb_currents(i1, i2, i3, i4):
    mi_modifycircprop("i1", 1, i1)
    mi_modifycircprop("i2", 1, i2)
    mi_modifycircprop("i3", 1, i3)
    mi_modifycircprop("i4", 1, i4)

# -- x-direction excitation: i1 (the ~0 deg / +x pole) dominant --
open("RadialMagneticBearing.fem")
mi_saveas("temp.fem")
rmb_currents(12, 6, 0, 6)
mi_probdef(0, "inches", "planar")
run("RMB (planar, x)  DC   i1=12 i2=6 i3=0 i4=6", range(0, 25), rmb_select)

mi_probdef(60, "inches", "planar")
run("RMB (planar, x)  AC 60Hz   i1=12 i2=6 i3=0 i4=6", range(0, 24), rmb_select)

open("RadialMagneticBearing.fem")
mi_saveas("bias.fem")
rmb_currents(6, 6, 6, 6)
mi_probdef(0, "inches", "planar")
mi_analyze()                 # -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
rmb_currents(1, 0, -1, 0)     # i1/i3 differential -> x-direction perturbation
mi_probdef(60, "inches", "planar")
run("RMB (planar, x)  Incremental AC   bias 6A all; AC i1=1 i2=0 i3=-1 i4=0",
    range(18, 31), rmb_select)

# -- y-direction excitation: i2 (the ~90 deg / +y pole) dominant --
open("RadialMagneticBearing.fem")
mi_saveas("temp.fem")
rmb_currents(6, 12, 6, 0)
mi_probdef(0, "inches", "planar")
run("RMB (planar, y)  DC   i1=6 i2=12 i3=6 i4=0", range(0, 25), rmb_select)

mi_probdef(60, "inches", "planar")
run("RMB (planar, y)  AC 60Hz   i1=6 i2=12 i3=6 i4=0", range(0, 24), rmb_select)

open("RadialMagneticBearing.fem")
mi_saveas("bias.fem")
rmb_currents(6, 6, 6, 6)
mi_probdef(0, "inches", "planar")
mi_analyze()                 # -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
rmb_currents(0, 1, 0, -1)     # i2/i4 differential -> y-direction perturbation
mi_probdef(60, "inches", "planar")
run("RMB (planar, y)  Incremental AC   bias 6A all; AC i1=0 i2=1 i3=0 i4=-1",
    range(18, 31), rmb_select)

# ==========================================================================
# Model 2: roters1b  (axisymmetric, single coil "Coil Current")
# ==========================================================================
def rot_select(kind, n=None):
    mo_clearblock()
    if kind == "rotor":
        mo_groupselectblock(1)        # steel armature (the moving body)
    else:
        mo_selectblock(1.125, 3.75)   # the coil block (group 0 -> point-select)

def rot_current(amps):
    mi_modifycircprop("Coil Current", 1, amps)

open("roters1b.fem")
mi_saveas("rtemp.fem")
rot_current(0.8)             # nominal
mi_probdef(0, "inches", "axi")
run("roters1b (axi)  DC   coil=0.8A", range(0, 25), rot_select)

mi_probdef(60, "inches", "axi")
run("roters1b (axi)  AC 60Hz   coil=0.8A", range(0, 24), rot_select)

open("roters1b.fem")
mi_saveas("rbias.fem")
rot_current(0.8)             # DC bias
mi_probdef(0, "inches", "axi")
mi_analyze()                 # -> rbias.ans
mi_saveas("rincr.fem")
mi_setprevious("rbias.ans", 1)
rot_current(0.1)             # small-signal AC perturbation about the bias
mi_probdef(60, "inches", "axi")
run("roters1b (axi)  Incremental AC   bias 0.8A; AC 0.1A", range(18, 31), rot_select)

# ==========================================================================
# Model 3: torque  (planar, purpose-built for torque; rotor = group 1)
#   Two magnets drive circuits i1/i2; the steel rotor (group 1) sees a torque
#   about the origin. Torque on the rotor is the Henrotte (weighted stress
#   tensor) torque -- 22/23/27; the Lorentz torque (15/16/30) is ~0 because the
#   rotor carries no source current. (Axisymmetric torque is identically zero,
#   so there is no axi torque case.) Runs cover the force/torque integrals.
# ==========================================================================
def torque_select(kind, n=None):
    mo_clearblock()
    mo_groupselectblock(1)        # the steel rotor (no separate coil group here)

def torque_currents(a, b):
    mi_modifycircprop("i1", 1, a)
    mi_modifycircprop("i2", 1, b)

open("torque.fem")
mi_saveas("temp.fem")
torque_currents(2, 0)
mi_probdef(0, "inches", "planar")
run("torque (planar)  DC   i1=2 i2=0", range(11, 25), torque_select)

mi_probdef(60, "inches", "planar")
run("torque (planar)  AC 60Hz   i1=2 i2=0", range(11, 24), torque_select)

open("torque.fem")
mi_saveas("bias.fem")
torque_currents(1, 1)
mi_probdef(0, "inches", "planar")
mi_analyze()                 # -> bias.ans
mi_saveas("incr.fem")
mi_setprevious("bias.ans", 1)
torque_currents(1, -1)
mi_probdef(60, "inches", "planar")
run("torque (planar)  Incremental AC   bias i1=1 i2=1; AC i1=1 i2=-1",
    range(18, 31), torque_select)

# ==========================================================================
# Models 4-6: Lorentz force / torque on a current-carrying coil (group 2).
#   A coil (group 2) sits near an iron piece (group 1) whose induced
#   magnetization gives the coil a net Lorentz (J x B) force -- the quantity
#   the earlier models couldn't exercise (their rotor carried no current).
#     AxisymmetricLorentzForce : axial (z) Lorentz force [12] (radial and torque
#                                are 0 by symmetry in axisymmetric).
#     PlanarLorentzForce       : coil 4 in off-axis -> y-direction force [12]
#                                and torque [15].
#     PlanarLorentzForceAlt    : same geometry rotated 90 deg -> x-direction
#                                force [11] and torque [15].
#   DC/AC use i1=16; incremental uses bias i1=16, perturbation i1=1.
# ==========================================================================
def lor_select(kind, n=None):
    mo_clearblock()
    mo_groupselectblock(2)        # the coil

def lor_current(a):
    mi_modifycircprop("i1", 1, a)

def run_lorentz(name, femfile, ptype):
    open(femfile)
    mi_saveas("temp.fem")
    lor_current(16)
    mi_probdef(0, "inches", ptype)
    run(name + "  DC   i1=16", range(0, 25), lor_select)

    mi_probdef(60, "inches", ptype)
    run(name + "  AC 60Hz   i1=16", range(0, 24), lor_select)

    open(femfile)
    mi_saveas("bias.fem")
    lor_current(16)
    mi_probdef(0, "inches", ptype)
    mi_analyze()                 # -> bias.ans
    mi_saveas("incr.fem")
    mi_setprevious("bias.ans", 1)
    lor_current(1)
    mi_probdef(60, "inches", ptype)
    run(name + "  Incremental AC   bias i1=16; AC i1=1", range(18, 31), lor_select)

run_lorentz("AxiLorentz (axi)",        "AxisymmetricLorentzForce.fem", "axi")
run_lorentz("PlanarLorentz-y (planar)", "PlanarLorentzForce.fem",      "planar")
run_lorentz("PlanarLorentz-x (planar)", "PlanarLorentzForceAlt.fem",   "planar")

# ==========================================================================
# Model 7: PMTest  (planar, nonlinear permanent magnets = group 1)
#   Knock-off of QuickField's magn1 benchmark: two mildly-nonlinear magnets
#   (group 1, H_c>0 with a BH curve) drive flux through steel, with graded-
#   permeability shells approximating an open boundary. This is the only model
#   that exercises the PM stored-energy second-quadrant correction (type 2, via
#   DoEnergy/Nrg/GetB) and the coenergy path (type 17) on a magnet. There are no
#   coils/circuits, so the current-carrying integrals are ~0, and DC is the only
#   meaningful case (a permanent magnet is a static source).
# ==========================================================================
# Weighted-stress-tensor (Henrotte) force/torque types -- only meaningful when
# the selected region is surrounded by air.
WST_TYPES = {18, 19, 20, 21, 22, 23, 25, 26, 27}

def pm_select(kind, n=None):
    mo_clearblock()
    # The magnets (group 1) sit on their steel keeper (group 3). Group 1 alone is
    # bounded by iron, so its WST force is meaningless; the assembly 1+3 is the
    # air-surrounded movable body. Select 1+3 for the force/torque integrals and
    # just the magnets (group 1) for energy/coenergy and the rest.
    mo_groupselectblock(1)
    if n in WST_TYPES:
        mo_groupselectblock(3)   # add the steel keeper -> air-surrounded assembly

open("PMTest.fem")
mi_saveas("temp.fem")
mi_probdef(0, "centimeters", "planar")
run("PMTest (planar)  DC   nonlinear magnets, group 1", range(0, 25), pm_select)

print("\nDone.")
