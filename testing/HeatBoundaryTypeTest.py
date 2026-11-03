# HeatBoundaryTypeTest.py -- the mesher reads heat-flow boundary types with
# heat-flow numbering (CPython; drives femm_headless).
#
# Heat flow numbers its boundary conditions 0 = fixed temperature, 1 = heat
# flux, 2 = convection, 3 = RADIATION, 4 = periodic, 5 = anti-periodic.
# Electrostatics and current flow have no radiation type, so there 3 is
# periodic and 4 anti-periodic. tangle 0.4.18 and earlier used the latter
# numbering for .feh files too, and hsolv applies whatever pairs the mesher
# hands it without looking at the type, so every heat model with one of
# these types got a silent wrong answer:
#
#   radiation on two equal-length faces -> temperatures tied node for node
#   periodic                            -> paired as anti-periodic, T = -T
#   anti-periodic                       -> not paired at all
#
# Nothing in the examples or the suite used heat types 3, 4 or 5, which is
# how it survived. Found 28 Oct 2026 from the spurious "PBC boundary ... has
# 1 segments" warning a reporter's radiation model produced; fixed in
# tangle 0.4.19.
#
# Three 20 x 2 cm slabs, probed at (3, +0.98) and (3, -0.98):
#   A. radiation top and bottom, heat generated in the upper half only:
#      the top face must be warmer than the bottom (tied = equal).
#   B. periodic top and bottom, 400 K left, 300 K right: 1-D conduction,
#      335 K at x = 3 on both faces (anti-periodic pairing gives ~0 K).
#   C. anti-periodic top and bottom, same ends: T_top = -T_bottom, both
#      near 0 K (unpaired gives 335 K).
#
# Usage:  python3 HeatBoundaryTypeTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes hbt_tmp* scratch files.
# Exits nonzero on any violation.

import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _default_engine(name):
    # Windows and Linux leave the engine in build/bin. macos_build.sh keeps
    # only the copy inside the app bundle, where the solvers sit beside it.
    b = os.path.join(HERE, "..", "build", "bin")
    loose = os.path.join(b, name + (".exe" if os.name == "nt" else ""))
    bundled = os.path.join(b, "femm_gui.app", "Contents", "MacOS", name)
    return bundled if not os.path.exists(loose) and os.path.exists(bundled) else loose


BIN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("femm_headless")
BASE = os.path.join(HERE, "hbt_tmp").replace("\\", "/")

SCRIPT = r'''
D = "%s"

def slab(split):
    newdocument(2)
    hi_probdef("centimeters", "planar", 1e-8, 100)
    hi_addmaterial("Gen", 200, 200, 100e3, 3)
    hi_addmaterial("NoGen", 200, 200, 0, 3)
    hi_addboundprop("Rad", 3, 0, 0, 0, 0, 0.95, 273)
    hi_addboundprop("Per", 4, 0, 0, 0, 0, 0, 0)
    hi_addboundprop("Anti", 5, 0, 0, 0, 0, 0, 0)
    hi_addboundprop("Hot", 0, 400, 0, 0, 0, 0, 0)
    hi_addboundprop("Cold", 0, 300, 0, 0, 0, 0, 0)
    hi_drawrectangle(-10, -1, 10, 1)
    if split:
        hi_addnode(-10, 0); hi_addnode(10, 0)
        hi_addsegment(-10, 0, 10, 0)

def setseg(x, y, prop):
    hi_clearselected()
    hi_selectsegment(x, y)
    hi_setsegmentprop(prop, 0, 1, 0, 0)
    hi_clearselected()

def setlabel(x, y, mat):
    hi_addblocklabel(x, y)
    hi_selectlabel(x, y)
    hi_setblockprop(mat)
    hi_clearselected()

def solve(tag):
    hi_saveas(D + "_" + tag + ".feh")
    hi_analyze()
    hi_loadsolution()
    print("RESULT", tag, ho_getpointvalues(3, 0.98)[0], ho_getpointvalues(3, -0.98)[0])
    ho_close()
    hi_close()

slab(True)
setlabel(0, 0.5, "Gen"); setlabel(0, -0.5, "NoGen")
setseg(0, 1, "Rad"); setseg(0, -1, "Rad")
setseg(-10, 0.5, "Cold"); setseg(-10, -0.5, "Cold")
solve("A")

slab(False)
setlabel(0, 0, "NoGen")
setseg(0, 1, "Per"); setseg(0, -1, "Per")
setseg(-10, 0, "Hot"); setseg(10, 0, "Cold")
solve("B")

slab(False)
setlabel(0, 0, "NoGen")
setseg(0, 1, "Anti"); setseg(0, -1, "Anti")
setseg(-10, 0, "Hot"); setseg(10, 0, "Cold")
solve("C")
''' % BASE


def main():
    with open(BASE + ".py", "w") as f:
        f.write(SCRIPT)
    r = subprocess.run([BIN, "-run", BASE + ".py"], capture_output=True, text=True)
    got = {}
    for ln in r.stdout.splitlines():
        v = ln.split()
        if v and v[0] == "RESULT":
            got[v[1]] = (float(v[2]), float(v[3]))
    fails = []
    if r.returncode != 0 or len(got) != 3:
        fails.append("engine exit %d, %d of 3 results\n%s%s" % (r.returncode, len(got), r.stdout, r.stderr))
    else:
        a, b, c = got["A"], got["B"], got["C"]
        print("  A radiation:     T_top=%.4f T_bot=%.4f  (top must be warmer)" % a)
        if not (a[0] - a[1] > 0.01):
            fails.append("radiation faces tied: top %.4f vs bottom %.4f" % a)
        print("  B periodic:      T_top=%.4f T_bot=%.4f  (335 on both)" % b)
        if abs(b[0] - 335) > 0.05 or abs(b[1] - 335) > 0.05:
            fails.append("periodic slab off 335 K: %.4f / %.4f" % b)
        print("  C anti-periodic: T_top=%.4f T_bot=%.4f  (|T| < 1 on both)" % c)
        if abs(c[0]) > 1 or abs(c[1]) > 1:
            fails.append("anti-periodic slab not paired: %.4f / %.4f" % c)
    for f in os.listdir(HERE):
        if f.startswith("hbt_tmp"):
            try:
                os.remove(os.path.join(HERE, f))
            except OSError:
                pass
    if fails:
        for f in fails:
            print("  VIOLATION: " + f)
        print("HEAT BOUNDARY TYPE CHECK FAILED: %d case(s)" % len(fails))
        sys.exit(1)
    print("heat boundary types: radiation left unpaired, periodic and anti-periodic paired with the right sign")


if __name__ == "__main__":
    main()
