# HeatBlockIntegralTest.py
# Exercises every heat-flow block-integral type (0-4) on two models:
#   heatFlow1 (axisymmetric) and heatFlow2 (planar).
# All heat block integrals are block quantities (heat flow has no force/torque),
# so each is evaluated over GROUP 1. Types 0, 3, 4 are volume averages
# (temperature, heat flux F, temperature gradient G); 1 and 2 are the block's
# area and volume.
#
# Companion to HeatBlockIntegralTest.lua (run on Original FEMM) for golden-value
# comparison. ho_blockintegral returns an (re, im) pair; both components are
# printed (temperature/area/volume are scalar with im ~ 0; F and G are 2-vectors
# whose re/im are the x/y, or r/z, components).

INTEGRALS = [
    (0, "Average temperature",       "K"),
    (1, "Cross-section area",        "m^2"),
    (2, "Volume",                    "m^3"),
    (3, "Average F (heat flux)",     "W/m^2"),
    (4, "Average G (temp gradient)", "K/m"),
]

def run(title):
    hi_saveas("temp.feh")   # analyze a throwaway copy so the .feh fixture stays pristine
    hi_analyze()
    hi_loadsolution()
    print("\n===================  " + title + "  ===================")
    for (n, label, units) in INTEGRALS:
        ho_clearblock()
        ho_groupselectblock(1)
        z = ho_blockintegral(n)
        print("  [%d] %-26s:   re=%.17g   im=%.17g" % (n, label, z[0], z[1]))
        ho_clearblock()
    ho_close()

open("heatFlow1.feh")
run("heatFlow1 (axisymmetric)")

open("heatFlow2.feh")
run("heatFlow2 (planar)")

print("\nDone.")
