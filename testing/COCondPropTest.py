# COCondPropTest.py
# Exercises co_getconductorproperties (voltage, current) across the current-flow
# block-integral fixtures currentFlow1-6 -- the terminal quantities of each named
# conductor (a fixed-voltage electrode reports its current; a fixed-current source
# reports its voltage). Complex for the AC/lossy cases. Each conductor prints
# "V | I" as two "re im" pairs, keyed by name, for CondPropCompare.py.

def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)

def run(fec, conds, tag):
    open(fec); ci_saveas("temp.fec"); ci_analyze(); ci_loadsolution()
    print("===  " + tag + "  ===")
    for c in conds:
        V, I = co_getconductorproperties(c)
        print("  %-14s = %s | %s" % (c, fmt(V), fmt(I)))
    ci_close()

run("currentFlow1.fec", ["Gnd", "InputCurrent"], "cf1 (planar, DC)")
run("currentFlow2.fec", ["Gnd", "Isrc"],         "cf2 (axi, DC)")
run("currentFlow3.fec", ["+10000V"],             "cf3 (axi, AC 60Hz)")
run("currentFlow4.fec", ["+10000V"],             "cf4 (planar, AC 60Hz)")
run("currentFlow5.fec", ["Vhi", "Vlo"],          "cf5 (axi, lossy 100kHz)")
run("currentFlow6.fec", ["Vhi", "Vlo"],          "cf6 (planar, lossy 100kHz)")
print("\nDone.")
