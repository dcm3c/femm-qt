# Bulk getters: shapes, dtypes, and agreement with the scalar getter each one
# summarises.
#
#     build/bin/femm_headless -run testing/BulkGetterTest.py
#
# These return typed ulab ARRAYS in process and are flattened to tab-separated
# text for the wire by format_value (libfemm/py_state.cpp), so one
# implementation serves both callers. The wire bytes are pinned separately, by
# byte-comparing against the pre-change binary over a real socket -- that check
# cannot live here, because in process there is no text to compare.
#
# What this file pins instead:
#   * shapes and dtypes, including the DC/AC rule (real without a frequency,
#     complex with one) and int32 for connectivity and group
#   * agreement with the scalar getters, element by element
#   * that 1-based connectivity actually works as a gather index, which is the
#     thing an off-by-one would silently break
import numpy as np

# Fixtures are the tracked models in THIS directory, analyzed into a throwaway
# copy so the fixture stays pristine -- the same pattern the block-integral
# suites use. It used to open four files under an absolute path on the author's
# machine and assume their solutions were already sitting beside them, so it
# could only ever pass here: on a fresh checkout the path does not exist, one of
# the four models was never tracked, and solution files are gitignored.
CODE = {100: "f8", 105: "i4", 99: "c16"}
bad = []


def dt(a):
    return CODE.get(int(a.dtype), str(int(a.dtype)))


def check(label, cond, detail=""):
    print("  %-46s %s %s" % (label, "ok" if cond else "FAIL",
                             detail if not cond else ""))
    if not cond:
        bad.append(label)


def geom(pfx, doc, loadfn, nodefn, elemfn, numnodes):
    ed = loadfn[:2]                       # mi / ei / hi / ci
    print("%s  (%s)" % (pfx, doc))
    opendocument(doc)
    globals()[ed + "_saveas"]("temp" + doc[doc.rfind("."):])
    globals()[ed + "_analyze"]()
    globals()[loadfn]()
    nodes = globals()[pfx + "_getallnodes"]()
    tup = globals()[pfx + "_getallelements"]()
    conn, ctr, area, grp = tup
    nn = int(globals()[numnodes]())
    check("nodes shape/dtype", nodes.shape == (nn, 2) and dt(nodes) == "f8",
          "%s %s" % (nodes.shape, dt(nodes)))
    check("conn is int32", dt(conn) == "i4", dt(conn))
    check("grp is int32", dt(grp) == "i4", dt(grp))
    check("ctr/area are f8", dt(ctr) == "f8" and dt(area) == "f8")
    # agreement with the scalar getters
    ref = globals()[nodefn](1)
    check("node 1 matches the scalar getter",
          nodes[0][0] == ref[0] and nodes[0][1] == ref[1])
    e = globals()[elemfn](1)
    check("element 1 matches the scalar getter",
          conn[0][0] == int(e[0]) and conn[0][2] == int(e[2])
          and ctr[0][0] == e[3] and area[0] == e[5] and grp[0] == int(e[6]))
    # 1-based connectivity must gather correctly
    x = nodes[:, 0]
    g = np.take(x, conn[:, 0] - 1)
    check("np.take with conn-1 gathers node x", g[0] == nodes[int(conn[0][0]) - 1][0])
    return tup


print()
geom("mo", "PMTest.fem", "mi_loadsolution", "mo_getnode",
     "mo_getelement", "mo_numnodes")
b = mo_getallb()
a = mo_getalla()
check("mo_getallb DC -> f8", dt(b) == "f8", dt(b))
check("mo_getalla DC -> f8", dt(a) == "f8", dt(a))
# mo_getallb is the element's CONSTANT B (meshelem[k].B1). getpointvalues
# returns the SMOOTHED value, so they agree only with smoothing off -- which is
# the check worth making, since it pins that getallb reads the element directly.
e1 = mo_getelement(1)
mo_smooth("off")
pv = mo_getpointvalues(e1[3], e1[4])
check("B1 matches getpointvalues, smoothing off", abs(b[0][0] - pv[1]) < 1e-9,
      "%r vs %r" % (b[0][0], pv[1]))
mo_smooth("on")
pvs = mo_getpointvalues(e1[3], e1[4])
check("...and differs with smoothing on (they are not the same quantity)",
      abs(b[0][0] - pvs[1]) > 1e-12)

print()
geom("eo", "electrostatic1.fee", "ei_loadsolution", "eo_getnode", "eo_getelement",
     "eo_numnodes")
d = eo_getalld()
v = eo_getallv()
check("eo_getalld -> (ne,2) f8 (a VECTOR, not a phasor)",
      dt(d) == "f8" and len(d.shape) == 2 and d.shape[1] == 2, dt(d))
check("eo_getallv -> f8", dt(v) == "f8", dt(v))

print()
geom("ho", "heatFlow1.feh", "hi_loadsolution", "ho_getnode", "ho_getelement",
     "ho_numnodes")
f = ho_getallf()
t = ho_getallt()
check("ho_getallf -> (ne,2) f8", dt(f) == "f8" and f.shape[1] == 2, dt(f))
check("ho_getallt -> f8", dt(t) == "f8", dt(t))

print()
geom("co", "currentFlow3.fec", "ci_loadsolution", "co_getnode", "co_getelement",
     "co_numnodes")
j = co_getallj()
cv = co_getallv()
# currentFlow3 runs at 60 Hz, so this is the AC half of the DC/AC rule the
# mo_ section above pins at DC. It used to be printed rather than checked, which
# meant the header's claim to cover both halves was only half true.
check("co_getallj/getallv at 60 Hz -> c16",
      dt(j) == "c16" and dt(cv) == "c16", "%s / %s" % (dt(j), dt(cv)))

print()
if bad:
    print("FAIL (%d): %s" % (len(bad), ", ".join(bad)))
else:
    print("all four physics: shapes, dtypes and scalar agreement ok")
