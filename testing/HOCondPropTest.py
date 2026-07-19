# HOCondPropTest.py
# Exercises ho_getconductorproperties (temperature, heat flow) on the heat-flow
# block-integral fixtures -- the terminal quantities of the "Bdry" conductor (its
# isothermal temperature and its total heat flow; thermal conductance = q/dT).
# Heat is static, so values are real. Each conductor prints "T | q" as two "re im"
# pairs (im == 0), keyed by name, for CondPropCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def run(feh, conds, tag):
    open(feh); hi_saveas("temp.feh"); hi_analyze(); hi_loadsolution()
    print("===  " + tag + "  ===")
    for c in conds:
        T, q = ho_getconductorproperties(c)
        print("  %-14s = %s | %s" % (c, fmt(T), fmt(q)))
    ho_close()

run("heatFlow1.feh", ["Bdry"], "heatFlow1 (axi)")
run("heatFlow2.feh", ["Bdry"], "heatFlow2 (planar)")
print("\nDone.")
