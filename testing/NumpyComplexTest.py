# Complex coverage of the numpy PROXY.
#
#     build/bin/femm_headless -run testing/NumpyComplexTest.py
#
# FEMM is complex-valued throughout -- AC magnetics, eddy currents, AC E/J
# fields -- so "does numpy work on a complex array" is not a corner case here.
#
# Two layers, and only the lower one is thin:
#
#   ulab    has a complex dtype (c16) and does storage, indexing and arithmetic,
#           but rejects complex from linalg, the transcendentals, the linear
#           reductions, and even array() on a complex Python list.
#   femm-qt fills those in -- complex linalg in py_clinalg.cpp over
#           common/fullmatrix.cpp, everything else in the numpy proxy in
#           libfemm/py_init_script.cpp.
#
# SCRIPTS ONLY EVER SEE THE PROXY, so ulab's raw gaps do not matter; what matters
# is where the PROXY still raises. This file sweeps for exactly that, and pins
# the answers of what it fills in -- "does not raise" is not enough, since a
# wrong branch cut or a wrong ordering raises nothing.
import numpy as np

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + ((": " + detail) if detail else ""))


def close(label, got, want, tol=1e-12):
    d = abs(complex(got) - complex(want))
    if d > tol:
        fails.append("%s is %r, want %r (off by %g)" % (label, got, want, d))


# --------------------------------------------------------------------------
# 1. the two layers are both present
# --------------------------------------------------------------------------

z = np.array([1 + 2j, 3 - 4j])
check("complex dtype exists", int(z.dtype) == int(np.complex))
check("complex itemsize is 16", z.itemsize == 16)
check("complex arithmetic", (z * z)[0] == (1 + 2j) ** 2)
check("complex .real/.imag",
      list(z.real) == [1.0, 3.0] and list(z.imag) == [2.0, -4.0])

A = np.array([[4 + 1j, 1 - 2j], [2 + 0j, 3 - 1j]])
b = np.array([1 + 0j, 2 + 1j])
w = np.linalg.eigvals(A)
close("eigenvalue product == det", w[0] * w[1], np.linalg.det(A), 1e-9)
x = np.linalg.solve(A, b)
r = np.dot(A, x)
close("solve residual [0]", r[0], b[0], 1e-9)
close("solve residual [1]", r[1], b[1], 1e-9)
for nm in ("inv", "det", "qr", "svd", "pinv", "cond", "norm", "matrix_rank"):
    try:
        getattr(np.linalg, nm)(A)
    except Exception as e:
        check("linalg.%s on complex" % nm, False, type(e).__name__)

# --------------------------------------------------------------------------
# 2. the values, not just the absence of an exception
# --------------------------------------------------------------------------

zz = 1.5 + 0.75j

# inverse hyperbolics are composed from log and sqrt; the identities are what
# catch a wrong branch. acosh in particular uses the factored
# sqrt(z+1)*sqrt(z-1), NOT sqrt(z*z-1), which is what puts the cut where numpy
# puts it -- sqrt(z*z-1) passes a naive round-trip test and is still wrong.
close("sinh(asinh(z)) == z", np.sinh(np.asinh(np.array([zz])))[0], zz, 1e-12)
close("cosh(acosh(z)) == z", np.cosh(np.acosh(np.array([zz])))[0], zz, 1e-12)
close("tanh(atanh(z)) == z", np.tanh(np.atanh(np.array([zz])))[0], zz, 1e-12)
close("acosh on the cut (z=-2)", np.acosh(np.array([-2 + 0j]))[0],
      1.3169578969248166 + 3.141592653589793j, 1e-12)

close("log10", np.log10(np.array([100 + 0j]))[0], 2.0, 1e-12)
close("log2", np.log2(np.array([8 + 0j]))[0], 3.0, 1e-12)
close("expm1 small", np.expm1(np.array([0 + 0j]))[0], 0.0, 1e-15)
close("expm1", np.expm1(np.array([1 + 0j]))[0], 1.718281828459045, 1e-12)
close("sinc(0) is 1", np.sinc(np.array([0 + 0j]))[0], 1.0, 1e-15)
close("sinc(1) is 0", np.sinc(np.array([1 + 0j]))[0], 0.0, 1e-15)

# the C-style spellings must agree with the arc* ones, which is the confusion
# this closed: np.arccos(z) worked while np.acos(z) raised
close("acos == arccos on complex",
      np.acos(np.array([zz]))[0], np.arccos(np.array([zz]))[0], 1e-14)
close("asin == arcsin on complex",
      np.asin(np.array([zz]))[0], np.arcsin(np.array([zz]))[0], 1e-14)
close("atan == arctan on complex",
      np.atan(np.array([zz]))[0], np.arctan(np.array([zz]))[0], 1e-14)

# ordering: numpy ranks complex lexicographically, real part then imaginary.
# The tie on the real part is the case that separates a correct implementation
# from one that only compares magnitudes or real parts.
tie = np.array([1 + 5j, 3 + 0j, 1 + 2j, 1 - 1j])
check("max is lexicographic", np.max(tie) == 3 + 0j, str(np.max(tie)))
check("min is lexicographic", np.min(tie) == 1 - 1j, str(np.min(tie)))
check("argmax", np.argmax(tie) == 1, str(np.argmax(tie)))
check("argmin", np.argmin(tie) == 3, str(np.argmin(tie)))
check("sort breaks ties on the imaginary part",
      list(np.sort(tie)) == [1 - 1j, 1 + 2j, 1 + 5j, 3 + 0j],
      str(list(np.sort(tie))))
check("median (even n averages the middle two)",
      np.median(tie) == (1 + 2j + 1 + 5j) * 0.5, str(np.median(tie)))
check("median (odd n)", np.median(np.array([3 + 0j, 1 + 0j, 2 + 0j])) == 2 + 0j)

# std of a complex array is REAL in numpy
s = np.std(np.array([1 + 1j, 3 + 3j]))
check("std is real", not isinstance(s, complex) or Im(s) == 0.0, repr(s))
close("std value", s, 1.4142135623730951, 1e-12)

check("diff", list(np.diff(np.array([1 + 1j, 3 + 2j, 6 + 4j])))
      == [2 + 1j, 3 + 2j], str(list(np.diff(np.array([1 + 1j, 3 + 2j, 6 + 4j])))))

check("equal", list(np.equal(np.array([1 + 1j, 2 + 0j]),
                             np.array([1 + 1j, 9 + 0j]))) == [True, False])
check("not_equal", list(np.not_equal(np.array([1 + 1j, 2 + 0j]),
                                     np.array([1 + 1j, 9 + 0j]))) == [False, True])
check("maximum is lexicographic",
      list(np.maximum(np.array([1 + 5j, 2 + 0j]),
                      np.array([1 + 2j, 3 + 0j]))) == [1 + 5j, 3 + 0j])
check("minimum is lexicographic",
      list(np.minimum(np.array([1 + 5j, 2 + 0j]),
                      np.array([1 + 2j, 3 + 0j]))) == [1 + 2j, 2 + 0j])

close("polyval", np.polyval([1 + 0j, 0 + 0j, 1 + 0j], 2 + 0j), 5 + 0j, 1e-12)
close("polyval with a complex argument",
      np.polyval([1, 0, 1], np.array([1j]))[0], 0 + 0j, 1e-12)

check("around", list(np.around(np.array([1.26 + 2.34j]), 1)) == [1.3 + 2.3j],
      str(list(np.around(np.array([1.26 + 2.34j]), 1))))

INF = float('inf')
check("isfinite", list(np.isfinite(np.array([1 + 1j, INF + 0j]))) == [True, False])
check("isinf (infinite real)",
      list(np.isinf(np.array([1 + 1j, complex(INF, 0)]))) == [False, True])
check("isinf (infinite imaginary)",
      list(np.isinf(np.array([1 + 1j, complex(0, INF)]))) == [False, True])

# KNOWN LIMITATION, pinned so it is a decision and not a surprise.
# np.array() on a Python list of complex numbers cannot go through ulab (it
# refuses complex lists), so the proxy rebuilds it as real*(1+0j) + imag*1j.
# That reconstruction is exact for finite values and LOSSY for infinities:
# inf*1j is nan+infj, because the complex multiply computes inf*0 for the real
# part, and the nan then poisons the sum. Constructing the element directly with
# complex(re, im) avoids it, which is why the two checks above do that.
# Fixing it means abandoning the arithmetic reconstruction for element-wise
# assignment into a preallocated array -- worth doing only if non-finite complex
# data ever turns up in practice, which for field values it does not.
bad = np.array([1 + 1j, 1 + INF * 1j])
check("array() from a list loses an infinite imaginary part (known)",
      Im(bad[1]) != Im(bad[1]),          # i.e. it is nan
      "reconstruction now preserves it -- drop this note and tighten the test")

# real arrays must still take ulab's fast path unchanged
check("real max untouched", np.max(np.array([1.0, 5.0, 2.0])) == 5.0)
check("real sort untouched", list(np.sort(np.array([3.0, 1.0, 2.0]))) == [1.0, 2.0, 3.0])
check("real log10 untouched", abs(np.log10(np.array([100.0]))[0] - 2.0) < 1e-12)
check("real diff untouched", list(np.diff(np.array([1.0, 3.0, 6.0]))) == [2.0, 3.0])

# --------------------------------------------------------------------------
# 3. THE SWEEP -- what still raises on complex that works on real
# --------------------------------------------------------------------------
# numpy itself rejects complex for these, so the proxy matching it is CORRECT.
# Filling them in would be a divergence, not a fix. Verified against CPython
# numpy 2.4.4; trapz does not exist in numpy 2.x at all (renamed trapezoid).
NUMPY_ALSO_REJECTS = ("floor", "ceil", "degrees", "radians", "arctan2", "trapz")

SKIP = ("ndinfo", "set_printoptions", "get_printoptions", "save", "savetxt",
        "load", "loadtxt", "frombuffer", "array", "asarray", "ndarray",
        "vectorize", "zeros", "ones", "empty", "eye", "full", "arange",
        "linspace", "logspace", "meshgrid")

r1 = np.array([1.0, 2.0, 3.0, 4.0])
r2 = np.array([2.0, 1.0, 4.0, 3.0])
c1 = np.array([1 + 2j, 3 - 1j, 2 + 0j, 1 - 1j])
c2 = np.array([2 + 1j, 1 - 2j, 1 + 1j, 3 + 0j])
rM = np.array([[4.0, 1.0], [2.0, 3.0]])
cM = np.array([[4 + 1j, 1 - 2j], [2 + 0j, 3 - 1j]])

worked = 0
narity = 0
gaps = []


def sweep(ns, nsname, real_args, cplx_args):
    global worked, narity
    for name in sorted(dir(ns)):
        if name.startswith("_") or name in SKIP or name in NUMPY_ALSO_REJECTS:
            continue
        try:
            fn = getattr(ns, name)
        except Exception:
            continue
        if not callable(fn):
            continue
        try:
            fn(*real_args)
        except Exception:
            narity += 1              # not a function of this arity; not a gap
            continue
        try:
            fn(*cplx_args)
            worked += 1
        except Exception as e:
            g = "%s.%s -> %s" % (nsname, name, type(e).__name__)
            if g not in gaps:
                gaps.append(g)


sweep(np, "np", (r1,), (c1,))
sweep(np, "np", (r1, r2), (c1, c2))
sweep(np, "np", (rM,), (cM,))
sweep(np.linalg, "np.linalg", (rM,), (cM,))

for g in gaps:
    check("sweep: works on real, raises on complex -- " + g, False)
check("sweep actually ran", worked >= 80, "only %d calls" % worked)

# and the allowlist must stay honest: each name on it must still be one that
# ulab refuses, otherwise it is silently excusing a gap that has since appeared
for nm in NUMPY_ALSO_REJECTS:
    fn = getattr(np, nm, None)
    if fn is None:
        continue
    try:
        fn(c1)
        fails.append("allowlisted %s now WORKS on complex -- drop it from "
                     "NUMPY_ALSO_REJECTS" % nm)
    except Exception:
        pass

if fails:
    print("FAIL (%d)" % len(fails))
    for f in fails:
        print("  " + f)
else:
    print("numpy complex check: ulab's dtype + femm-qt's linalg and proxy "
          "layers; %d calls verified on complex arrays, values pinned for the "
          "26 the proxy fills in, and the %d numpy itself rejects stay rejected"
          % (worked, len(NUMPY_ALSO_REJECTS)))
