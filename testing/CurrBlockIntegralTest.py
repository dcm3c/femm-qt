# CurrBlockIntegralTest.py
# Exercises every current-flow block-integral type (0-11) across six models:
#   currentFlow1 (planar,  DC)         currentFlow2 (axisymmetric, DC)
#   currentFlow3 (axi,  force/torque)  currentFlow4 (planar, force/torque)
#   currentFlow5 (axi,  lossy diel.)   currentFlow6 (planar, lossy diel.)
#
# co_blockintegral returns a plain float for real-valued types and a complex for
# the genuinely-complex ones (the 2x force/torque parts, 8/9/11, at AC), so this
# prints repr(z) and the Python type -- cross-checking the real/complex classifier
# as well as the value. Companion to CurrBlockIntegralTest.lua (run on Original
# FEMM) for a golden-value comparison.
#
# Block quantities (0-5: real/reactive/apparent power, stored energy, area,
# volume) are evaluated over GROUP 1. The weighted-stress-tensor force/torque
# (6-11) act on a conductor, so they select the "+10000V" conductor -- exercised
# on the two force models (3, 4). The DC models exercise ohmic loss; the lossy-
# dielectric models (100 kHz) make reactive/apparent power non-trivial.

INTEGRALS = [
    (0,  "Real power",              "W",    "block"),
    (1,  "Reactive power",          "VAr",  "block"),
    (2,  "Apparent power",          "VA",   "block"),
    (3,  "Time-avg stored energy",  "J",    "block"),
    (4,  "Cross-section area",      "m^2",  "block"),
    (5,  "Volume",                  "m^3",  "block"),
    (6,  "WST force x/r (SS)",      "N",    "cond"),
    (7,  "WST force y/z (SS)",      "N",    "cond"),
    (8,  "WST force x/r (2x)",      "N",    "cond"),
    (9,  "WST force y/z (2x)",      "N",    "cond"),
    (10, "WST torque (SS)",         "N*m",  "cond"),
    (11, "WST torque (2x)",         "N*m",  "cond"),
]

def run(title, nums, conductor):
    ci_saveas("temp.fec")   # analyze a throwaway copy so the .fec fixture stays pristine
    ci_analyze()
    ci_loadsolution()
    print("\n===================  " + title + "  ===================")
    for (n, label, units, kind) in INTEGRALS:
        if n not in nums:
            continue
        co_clearblock()
        if kind == "cond":
            co_selectconductor(conductor)   # WST force/torque acts on the conductor
        else:
            co_groupselectblock(1)          # block quantities over group 1
        z = co_blockintegral(n)
        kindstr = "complex" if isinstance(z, complex) else "real"
        print("  [%2d] %-24s = %-26s %-5s (%s)" % (n, label, repr(z), units, kindstr))
        co_clearblock()
    co_close()

# DC current flow: ohmic loss (real power), area, volume (reactive/apparent ~0).
open("currentFlow1.fec")
run("currentFlow1 (planar, DC)", range(0, 6), None)
open("currentFlow2.fec")
run("currentFlow2 (axisymmetric, DC)", range(0, 6), None)

# Force/torque cases (AC 60 Hz, complex conductor voltage): WST force/torque on
# the "+10000V" conductor plus the block quantities.
open("currentFlow3.fec")
run("currentFlow3 (axisymmetric, force/torque)", range(0, 12), "+10000V")
open("currentFlow4.fec")
run("currentFlow4 (planar, force/torque)", range(0, 12), "+10000V")

# Lossy dielectric (AC 100 kHz): displacement current dominates, so real,
# reactive, and apparent power and stored energy are all non-trivial.
open("currentFlow5.fec")
run("currentFlow5 (axisymmetric, lossy dielectric)", range(0, 6), None)
open("currentFlow6.fec")
run("currentFlow6 (planar, lossy dielectric)", range(0, 6), None)

print("\nDone.")
