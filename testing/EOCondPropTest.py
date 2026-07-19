# EOCondPropTest.py
# Exercises eo_getconductorproperties (voltage, charge) on the electrostatics
# block-integral fixtures -- the terminal quantities of the "+10000V" conductor
# (its prescribed voltage and its total induced charge; capacitance = q/V).
# Electrostatics is static, so values are real. Each conductor prints "V | q" as
# two "re im" pairs (im == 0), keyed by name, for CondPropCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def run(fee, conds, tag):
    open(fee); ei_saveas("temp.fee"); ei_analyze(); ei_loadsolution()
    print("===  " + tag + "  ===")
    for c in conds:
        V, q = eo_getconductorproperties(c)
        print("  %-14s = %s | %s" % (c, fmt(V), fmt(q)))
    eo_close()

run("electrostatic1.fee", ["+10000V"], "electrostatic1 (axi)")
run("electrostatic2.fee", ["+10000V"], "electrostatic2 (planar)")
print("\nDone.")
