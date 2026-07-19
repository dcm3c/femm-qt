# ElecBlockIntegralTest.py
# Exercises every electrostatics block-integral type (0-6) on two models:
#   electrostatic1 (axisymmetric) and electrostatic2 (planar).
# Types 0-4 (stored energy, cross-section area, volume, average D, average E)
# are evaluated over GROUP 1 -- the whole modeled domain. Types 5-6 (weighted-
# stress-tensor force and torque) are evaluated on the "+10000V" CONDUCTOR,
# which is the mesh-node selection a WST force/torque integral requires.
#
# Companion to ElecBlockIntegralTest.lua (run on Original FEMM) for golden-value
# comparison. eo_blockintegral returns an (re, im) pair; both components are
# printed for every type (for the scalar types im is ~0; for force, planar is
# (Fx, Fy) and axisymmetric is (Fr~0, Fz)).

INTEGRALS = [
    (0, "Stored energy",      "J"),
    (1, "Cross-section area", "m^2"),
    (2, "Volume",             "m^3"),
    (3, "Average D",          "C/m^2"),
    (4, "Average E",          "V/m"),
    (5, "WST force",          "N"),
    (6, "WST torque",         "N*m"),
]

def run(title):
    ei_saveas("temp.fee")   # analyze a throwaway copy so the .fee fixture stays pristine
    ei_analyze()
    ei_loadsolution()
    print("\n===================  " + title + "  ===================")
    for (n, label, units) in INTEGRALS:
        eo_clearblock()
        if n >= 5:
            eo_selectconductor("+10000V")   # WST force/torque acts on the conductor
        else:
            eo_groupselectblock(1)          # block quantities over group 1
        z = eo_blockintegral(n)
        print("  [%d] %-20s:   re=%.17g   im=%.17g" % (n, label, z[0], z[1]))
        eo_clearblock()
    eo_close()

open("electrostatic1.fee")
run("electrostatic1 (axisymmetric)")

open("electrostatic2.fee")
run("electrostatic2 (planar)")

print("\nDone.")
