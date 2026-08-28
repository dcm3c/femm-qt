"""Layer 2, pyfemm: EOCondPropTest driven through the binding.

    FEMM_DIR=<build>/bin  python3 l2_condprop_pyfemm.py > out.txt
    python3 ../CondPropCompare.py ../OF_eic_result.txt out.txt

A line-for-line translation of testing/EOCondPropTest.py, which is one of the 18
suites already validated against Original FEMM. Because the output format is
identical, the EXISTING comparer checks this against the SAME OF golden -- so a
binding is measured against Original FEMM, not merely against an in-process run.

Everything here is the binding's own code path: argument marshalling out, reply
parsing back. The engine is the same one the in-process suite uses.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "pyfemm"))
TESTS = os.path.abspath(os.path.join(HERE, ".."))
TMP = os.environ.get("TEMP", "C:/temp")

import femm  # noqa: E402


def fmt(z):
    r = z.real if isinstance(z, complex) else float(z)
    i = z.imag if isinstance(z, complex) else 0.0
    return "%.10g %.10g" % (r, i)


def run(fee, conds, tag):
    femm.opendocument(os.path.join(TESTS, fee).replace("\\", "/"))
    femm.ei_saveas((TMP + "/l2_py_temp.fee").replace("\\", "/"))
    femm.ei_analyze()
    femm.ei_loadsolution()
    print("===  " + tag + "  ===")
    for c in conds:
        z = femm.eo_getconductorproperties(c)
        print("  %-14s = %s | %s" % (c, fmt(z[0]), fmt(z[1])))
    femm.eo_close()


femm.openfemm(1)                      # 1 = headless
try:
    run("electrostatic1.fee", ["+10000V"], "electrostatic1 (axi)")
    run("electrostatic2.fee", ["+10000V"], "electrostatic2 (planar)")
    print("")
    print("Done.")
finally:
    femm.closefemm()
