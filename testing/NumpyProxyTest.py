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


def same(label, got, want):
    """Exact equality, for dtypes and value lists -- check() subtracts."""
    if list(got) != list(want) if isinstance(want, list) else got != want:
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

# --- np.array's dtype argument -----------------------------------------------
#
# The proxy wraps np.array to handle complex lists, which ulab rejects. That
# fallback used to run on ANY TypeError/ValueError, not just the complex one --
# so np.array(floats, dtype=np.int32) came back COMPLEX, silently, at 16 bytes
# per element. ulab refuses to truncate a float into an integer dtype; the
# fallback swallowed that and produced the wrong type. It also dropped the
# dtype= argument entirely, since the fallback never forwarded *args/**kw.
#
# numpy truncates float -> int, so the proxy now does too.

I32 = int(np.int32)
CPX = int(np.complex)

check("array(floats, dtype=int32) is int32",
      int(np.array([1.0, 2.9, -1.7], dtype=np.int32).dtype), I32)
same("array(floats, dtype=int32) truncates",
     list(np.array([1.0, 2.9, -1.7], dtype=np.int32)), [1, 2, -1])
check("2-D float list, dtype=int32",
      int(np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.int32).dtype), I32)
check("float ndarray -> int32",
      int(np.array(np.array([1.0, 2.9]), dtype=np.int32).dtype), I32)
check("dtype passed positionally",
      int(np.array([1.0, 2.0], np.int32).dtype), I32)
check("narrower integer dtypes too",
      int(np.array([1.0, 2.0], dtype=np.int16).dtype),
      int(np.array([1], dtype=np.int16).dtype))

# the complex cases the fallback exists for, which must still work
check("complex list is complex", int(np.array([1 + 2j, 3]).dtype), CPX)
same("complex list values", list(np.array([1 + 2j, 3])), [1 + 2j, 3 + 0j])
check("nested complex list", int(np.array([[1 + 2j], [3 + 0j]]).dtype), CPX)
check("complex ndarray dense copy",
      int(np.array(np.array([1 + 2j, 3 - 1j])).dtype), CPX)

# and the ordinary paths are untouched
check("int list defaults to float", int(np.array([1, 2, 3]).dtype), int(np.float))
check("int list, dtype=int32", int(np.array([1, 2, 3], dtype=np.int32).dtype), I32)
check("float list", int(np.array([1.0, 2.0]).dtype), int(np.float))

# A failure that is NOT about complex input must propagate rather than be
# masked by the complex fallback.
try:
    np.array([1.0], dtype="not a dtype")
    fails.append("array() accepted a nonsense dtype")
except (TypeError, ValueError):
    pass

# KNOWN DIVERGENCE, deliberately not guarded: ulab silently drops the imaginary
# part for np.array(complex, dtype=<int>) where numpy raises. ulab does not
# raise, so the proxy's fallback never sees it, and detecting it would cost a
# full pass over the input on every integer-dtype construction -- the hot path
# for the bulk mesh getters -- to defend a call with no sensible meaning.
same("complex -> int dtype takes the real part (ulab, not numpy)",
     list(np.array([1 + 2j, 3 - 4j], dtype=np.int32)), [1, 3])


# --- real input must give a real result (2026-09-01) -------------------------
#
# The proxy exists to add complex support to dot and linalg, and it used to buy
# that by coercing EVERY input to complex.  So np.dot on two float arrays came
# back complex, where numpy -- and ulab's own dot, which this proxy shadows --
# returns a float.  It was also less accurate: real work routed through the
# complex kernel lost the exactness of the real one.
#
# Two behaviours are pinned here, and they pull in opposite directions, which
# is why both are needed: real in / real out, AND complex still working.
F64 = int(np.float)
CPX = int(np.complex)
_v = np.array([1.0, 2.0])

same("dot(real, real) dtype", int(np.dot(M, M).dtype), F64)
same("dot(vec, vec) is a real scalar", isinstance(np.dot(_v, _v), complex), False)
same("linalg.inv(real) dtype", int(np.linalg.inv(M).dtype), F64)
same("linalg.solve(real) dtype", int(np.linalg.solve(M, _v).dtype), F64)
same("linalg.pinv(real) dtype", int(np.linalg.pinv(M).dtype), F64)
same("linalg.qr(real) dtype", int(np.linalg.qr(M)[0].dtype), F64)
same("linalg.svd(real) dtype", int(np.linalg.svd(M)[1].dtype), F64)
same("linalg.det(real) is not complex", isinstance(np.linalg.det(M), complex), False)
# Exactness, not just dtype: the real kernel gives -2 outright where the
# complex one returned -1.9999999999999996.
same("det(M) is exact", np.linalg.det(M), -2.0)
same("inv(M)[0][0] is exact", np.linalg.inv(M)[0][0], -2.0)

_C = np.array([[1 + 1j, 2.0], [3.0, 4.0]])
same("dot(complex) stays complex", int(np.dot(_C, _C).dtype), CPX)
same("inv(complex) stays complex", int(np.linalg.inv(_C).dtype), CPX)
same("solve(complex) stays complex",
     int(np.linalg.solve(_C, np.array([1 + 0j, 2 + 0j])).dtype), CPX)
same("det(complex) stays complex", isinstance(np.linalg.det(_C), complex), True)

# eig is deliberately NOT in the real-in-real-out set: a real matrix can have
# genuinely complex eigenvalues, so collapsing it would be wrong rather than
# tidy.  (It also cannot delegate to ulab, whose eig refuses a non-symmetric
# matrix outright.)
same("eig(real, non-symmetric) is complex", int(np.linalg.eig(M)[0].dtype), CPX)
check("eig value unchanged", np.linalg.eig(M)[0][0].real, 5.372281323269014)

# --- the @ operator (2026-09-01) ---------------------------------------------
#
# MicroPython's compiler always emitted MP_BINARY_OP_MAT_MULTIPLY for `@` and
# delivered it to the ndarray type; upstream ulab just had no case for it, so
# it fell through as "unsupported types for __matmul__".  Now routed to the
# same code np.dot uses -- which is exact rather than approximate here, because
# numpy distinguishes matmul from dot only above 2 dimensions and ULAB_MAX_DIMS
# is 2.
#
# The pairing with np.dot is the thing to protect.  A complex `@` that raised
# while np.dot worked would be worse than no `@` at all, and the complex path
# reaches femm-qt's kernel through a hook, since ulab cannot call it directly.
_P = np.array([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]])
_Q = np.array([[1.0, 2.0], [3.0, 4.0], [5.0, 6.0]])
_w = np.array([1.0, 2.0, 3.0])

same("M @ N agrees with dot, row 0", list((_P @ _Q)[0]), list(np.dot(_P, _Q)[0]))
same("M @ N agrees with dot, row 1", list((_P @ _Q)[1]), list(np.dot(_P, _Q)[1]))
# SHAPE first, then values. list() of a 2-D result yields ROWS, and comparing
# that against a list of floats does not report a difference here -- so a
# values-only check passes even when the result has the wrong rank, which a
# mutation test caught.
same("matrix @ vector shape", (_P @ _w).shape, (2,))
same("matrix @ vector", list(_P @ _w), [14.0, 32.0])
check("vector @ vector is a scalar", _w @ _w, 14.0)
same("@ keeps a real result real", int((_P @ _Q).dtype), F64)

# A vector on the LEFT is the case that reads differently from the one above,
# and numpy treats them differently: P @ u contracts the matrix's COLUMNS,
# u @ P its ROWS, so the two give different answers for the same operands.
_R = np.array([[1.0, 2.0], [3.0, 4.0]])
_u = np.array([1.0, 2.0])
same("matrix @ vector shape (columns)", (_R @ _u).shape, (2,))
same("matrix @ vector (columns)", list(_R @ _u), [5.0, 11.0])
same("vector @ matrix shape (rows)", (_u @ _R).shape, (2,))
same("vector @ matrix (rows)", list(_u @ _R), [7.0, 10.0])

# @ must be MATRIX multiplication, not an elementwise product and not a
# symmetric one.  Both of those would still return an array of the right shape
# for square operands, so shape agreement proves nothing on its own.
_S = np.array([[5.0, 6.0], [7.0, 8.0]])
same("@ is not commutative",
     [list(r) for r in _R @ _S] == [list(r) for r in _S @ _R], False)
same("@ is not elementwise",
     [list(r) for r in _R @ _S] == [list(r) for r in _R * _S], False)
same("R @ S", [list(r) for r in _R @ _S], [[19.0, 22.0], [43.0, 50.0]])
same("S @ R", [list(r) for r in _S @ _R], [[23.0, 34.0], [31.0, 46.0]])

_D = np.array([[1 + 1j, 2.0], [3.0, 4.0]])
same("complex @ complex agrees with dot, row 0",
     list((_D @ _D)[0]), list(np.dot(_D, _D)[0]))
same("complex @ complex is complex", int((_D @ _D).dtype), CPX)
same("complex @ real is complex", int((_D @ M).dtype), CPX)
same("real @ complex is complex", int((M @ _D).dtype), CPX)

# The operand on either side may be a plain list -- the reverse form.
same("list @ ndarray", list(([[1.0, 0.0], [0.0, 1.0]] @ M)[0]), list(M[0]))
same("ndarray @ list", list((M @ [[1.0, 0.0], [0.0, 1.0]])[0]), list(M[0]))

# Shape errors must still be errors, not silence.  This is also what pins the
# handler's POSITION: ndarray_can_broadcast() runs later in ndarray_binary_op,
# and matmul shapes are deliberately not broadcastable, so a handler placed
# after it would reject (2,3) @ (3,2) as un-broadcastable instead of
# multiplying it.
try:
    np.array([[1.0, 2.0, 3.0]]) @ M
    fails.append("@ with mismatched shapes did not raise")
except (ValueError, TypeError):
    pass


if fails:
    for m in fails:
        print("   " + m)
    print("numpy proxy: %d FAILED" % len(fails))
else:
    print("numpy proxy check: matrix_rank (and the rank alias), eigvals, lstsq "
          "with numpy's empty-residual rule, roots verified by substitution, "
          "where in both its 1- and 3-argument forms, array()'s dtype argument "
          "(float->int truncates; complex still round-trips), real input giving "
          "real output from dot/inv/solve/pinv/qr/svd/det while complex still "
          "works and eig stays complex, and the @ operator agreeing with dot on "
          "real, complex and mixed operands while being a genuine matrix "
          "product -- shapes contract, a vector on the left differs from one on "
          "the right, and it is neither commutative nor elementwise")
