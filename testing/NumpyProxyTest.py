# The numpy proxy's own additions -- the things femm-qt supplies that ulab does
# not.
#
#     build/bin/femm_headless -run testing/NumpyProxyTest.py
#
# `import numpy` here is ulab, a subset. libfemm/py_init_script.cpp wraps it in
# a proxy that fills in what a pasted numpy snippet expects, and this file
# covers that proxy rather than ulab itself. Everything is checked against a
# closed form or an invariant computed in this file, so it needs no CPython --
# though each was also compared against CPython numpy 2.4.4 when written, worst
# error 8.0e-12 (the cubic in `roots`, whose conditioning numpy shares).
#
# Two of these existed and were simply unreachable: matrix_rank was shipped
# under the non-numpy name `rank`, and where() rejected its one-argument form.
import math

import numpy as np

fails = []


def check(label, got, want, tol=1e-9):
    if abs(got - want) > tol:
        fails.append("%s is %r, want %r" % (label, got, want))


M = np.array([[1.0, 2.0], [3.0, 4.0]])
A = np.array([[1.0, 1.0], [1.0, 2.0], [1.0, 3.0]])
b3 = np.array([1.0, 2.0, 2.0])
A2 = np.array([[1.0, 2.0], [2.0, 4.0], [3.0, 6.0]])      # rank 1, deficient

# --- matrix_rank: numpy's spelling of what shipped as `rank` ----------------
check("matrix_rank(M)", np.linalg.matrix_rank(M), 2)
check("matrix_rank(A2)", np.linalg.matrix_rank(A2), 1)
# The old name has to keep working -- scripts written against this proxy may
# already use it, even though numpy has no such function.
check("rank(M) alias", np.linalg.rank(M), 2)

# --- eigvals: eig without the vectors --------------------------------------
# [[1,2],[3,4]] has eigenvalues (5 +- sqrt(33))/2 in closed form.
w = np.linalg.eigvals(M)
want = sorted([(5 + math.sqrt(33)) / 2, (5 - math.sqrt(33)) / 2])
got = sorted([complex(v).real for v in w])
if len(w) != 2:
    fails.append("eigvals returned %d values, want 2" % len(w))
else:
    for i in range(2):
        check("eigvals[%d]" % i, got[i], want[i])
    for v in w:
        if abs(complex(v).imag) > 1e-12:
            fails.append("eigvals of a real symmetric-spectrum matrix is not "
                         "real: %r" % (complex(v),))
# It must agree with eig(), which is what it is built on.
if max(abs(complex(a) - complex(b))
       for a, b in zip(np.linalg.eigvals(M), np.linalg.eig(M)[0])) > 1e-12:
    fails.append("eigvals disagrees with eig()[0]")

# --- lstsq -----------------------------------------------------------------
# Overdetermined and full rank. The normal equations give the answer exactly:
# A'A = [[3,6],[6,14]], A'b = [5,11], so x = [2/3, 1/2] and the residual sum of
# squares is 1/6.
x, res, rk, sv = np.linalg.lstsq(A, b3)
check("lstsq x[0]", complex(x[0]).real, 2.0 / 3.0)
check("lstsq x[1]", complex(x[1]).real, 0.5)
check("lstsq rank", rk, 2)
if len(res) != 1:
    fails.append("lstsq gave %d residuals, want 1 for a full-rank "
                 "overdetermined system" % len(res))
else:
    check("lstsq residual", float(res[0]), 1.0 / 6.0)
if len(sv) != 2:
    fails.append("lstsq returned %d singular values, want 2" % len(sv))

# Rank-deficient: numpy reports an EMPTY residual array rather than a number,
# because there is no unique residual to report. Getting this wrong gives a
# plausible value instead of nothing.
x2, res2, rk2, sv2 = np.linalg.lstsq(A2, b3)
check("lstsq rank-deficient rank", rk2, 1)
if len(res2) != 0:
    fails.append("lstsq on a rank-deficient system returned %d residuals, "
                 "want 0" % len(res2))

# rcond changes the answer, so it must refuse rather than be ignored.
try:
    np.linalg.lstsq(A, b3, rcond=1e-10)
    fails.append("lstsq accepted rcond=; it should refuse")
except (NotImplementedError, ValueError, TypeError):
    pass

# --- roots -----------------------------------------------------------------
# Checked by substitution rather than against a table: every returned root must
# actually be a root. That catches a wrong companion matrix, which a
# hand-written expected list for one polynomial would not.
def polyval(p, z):
    s = 0j
    for c in p:
        s = s * z + c
    return s


for p, n in (([1.0, -3.0, 2.0], 2),
             ([1.0, 0.0, 1.0], 2),               # +-i, so complex is required
             ([1.0, -6.0, 11.0, -6.0], 3),
             ([2.0, -4.0], 1),
             ([1.0, 2.0, 3.0, 4.0, 5.0], 4),
             ([1.0, -3.0, 2.0, 0.0], 3),         # trailing zero -> root at 0
             ([0.0, 1.0, -3.0, 2.0], 2)):        # leading zero is dropped
    r = np.roots(p)
    if len(r) != n:
        fails.append("roots(%r) gave %d roots, want %d" % (p, len(r), n))
        continue
    scale = max(abs(c) for c in p)
    for z in r:
        if abs(polyval(p, complex(z))) > 1e-8 * scale:
            fails.append("roots(%r): %r is not a root (p(z) = %.2e)"
                         % (p, complex(z), abs(polyval(p, complex(z)))))

if len(np.roots([5.0])) != 0:
    fails.append("roots of a constant should be empty")
if len(np.roots([0.0])) != 0:
    fails.append("roots of the zero polynomial should be empty")
if len(np.roots(np.array([1.0, -3.0, 2.0]))) != 2:
    fails.append("roots does not accept an ndarray")

# --- where -----------------------------------------------------------------
a = np.array([3.0, 1.0, 2.0, 5.0])
idx = np.where(a > 2)
if not isinstance(idx, tuple) or len(idx) != 1:
    fails.append("where(cond) must return a tuple of one index array, got %r"
                 % (idx,))
elif [int(v) for v in idx[0]] != [0, 3]:
    fails.append("where(a > 2) gave %r, want [0, 3]"
                 % ([int(v) for v in idx[0]],))
sel = np.where(a > 2, a, 0.0)
if [float(v) for v in sel] != [3.0, 0.0, 0.0, 5.0]:
    fails.append("where(cond, x, y) gave %r" % ([float(v) for v in sel],))
try:
    np.where(a > 2, a)
    fails.append("where() accepted two arguments; numpy takes 1 or 3")
except (ValueError, TypeError):
    pass

if fails:
    for m in fails:
        print("   " + m)
    print("numpy proxy: %d FAILED" % len(fails))
else:
    print("numpy proxy check: matrix_rank (and the rank alias), eigvals, lstsq "
          "with numpy's empty-residual rule, roots verified by substitution, "
          "and where in both its 1- and 3-argument forms")
