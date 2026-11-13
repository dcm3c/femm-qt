# CreateMeshReturnTest.py -- *_createmesh() returns the node count of the mesh
# it made, in all four physics (CPython; drives femm_headless).
#
# FEMM 4.2 pushed meshnode.GetSize() back to the script from mi_createmesh()
# and its siblings (its manual says "elements"; the code counts nodes, and
# scripts written against it expect nodes). The port's shared createmesh
# returned nothing, so a script got None where it used to get a number
# (a femm group report, November 2026, against ci_createmesh).
#
# For one shipped example per physics: open it, save a scratch copy, call
# createmesh, then analyze and compare with the postprocessor's node count.
# The two must agree exactly (no periodic or air-gap boundary in these
# models, so the solver keeps every mesher node) and be a positive int.
#
# Usage:  python3 CreateMeshReturnTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes cmr_tmp* scratch files.
# Exits nonzero on any violation.

import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _default_engine(name):
    b = os.path.join(HERE, "..", "build", "bin")
    loose = os.path.join(b, name + (".exe" if os.name == "nt" else ""))
    bundled = os.path.join(b, "femm_gui.app", "Contents", "MacOS", name)
    return bundled if not os.path.exists(loose) and os.path.exists(bundled) else loose

BIN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("femm_headless")
BASE = os.path.join(HERE, "cmr_tmp").replace("\\", "/")
EX = os.path.abspath(os.path.join(HERE, "..", "examples")).replace("\\", "/")

CASES = [  # tag, example, editor prefix, postprocessor prefix, scratch extension
    ("mi", "coil.fem", "mi", "mo", ".fem"),
    ("ei", "bdemo1.fee", "ei", "eo", ".fee"),
    ("hi", "htutor.feh", "hi", "ho", ".feh"),
    ("ci", "cdemo2.fec", "ci", "co", ".fec"),
]

SCRIPT = r'''
EX = "%s"
D = "%s"
def run(tag, example, i, o, ext):
    opendocument(EX + "/" + example)
    g = globals()
    g[i + "_saveas"](D + "_" + tag + ext)
    n = g[i + "_createmesh"]()
    g[i + "_analyze"](1)
    g[i + "_loadsolution"]()
    m = g[o + "_numnodes"]()
    print("RESULT", tag, type(n).__name__, n, m)
    g[o + "_close"]()
    g[i + "_close"]()
''' % (EX, BASE)
for tag, example, i, o, ext in CASES:
    SCRIPT += 'run("%s", "%s", "%s", "%s", "%s")\n' % (tag, example, i, o, ext)


def main():
    with open(BASE + ".py", "w") as f:
        f.write(SCRIPT)
    r = subprocess.run([BIN, "-run", BASE + ".py"], capture_output=True, text=True)
    got = {}
    for ln in r.stdout.splitlines():
        v = ln.split()
        if v and v[0] == "RESULT":
            got[v[1]] = (v[2], v[3], v[4])
    fails = []
    if r.returncode != 0 or len(got) != len(CASES):
        fails.append("engine exit %d, %d of %d results\n%s%s" % (r.returncode, len(got), len(CASES), r.stdout, r.stderr))
    else:
        for tag, example, i, o, ext in CASES:
            typ, n, m = got[tag]
            print("  %s_createmesh() on %-12s -> %s %s   (%s_numnodes() = %s)" % (i, example, typ, n, o, m))
            if typ != "int":
                fails.append("%s_createmesh() returned a %s, not an int" % (i, typ))
            elif int(n) <= 0 or n != m:
                fails.append("%s_createmesh() returned %s, %s_numnodes() says %s" % (i, n, o, m))
    for f in os.listdir(HERE):
        if f.startswith("cmr_tmp"):
            try:
                os.remove(os.path.join(HERE, f))
            except OSError:
                pass
    if fails:
        for f in fails:
            print("  VIOLATION: " + f)
        print("CREATEMESH RETURN CHECK FAILED: %d case(s)" % len(fails))
        sys.exit(1)
    print("createmesh returns the mesh node count in all four physics, equal to the postprocessor's count")


if __name__ == "__main__":
    main()
