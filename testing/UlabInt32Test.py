# int32 in ulab: what femm-qt adds on top of upstream, and why.
#
#     build/bin/femm_headless -run testing/UlabInt32Test.py
#
# Upstream ulab's integer dtypes stop at 16 bits -- int8/uint8/int16/uint16 --
# because its arithmetic is macro-expanded over every pair of dtypes and each
# new type costs flash on the microcontrollers it targets.  On a desktop that
# tradeoff is upside down: 32767 is not enough to index a mesh, and exceeding
# it wraps silently rather than complaining.
#
# So this build adds int32, with one deliberate difference from numpy:
# ARITHMETIC ON int32 PRODUCES float64.  That is exact -- every int32 value is
# exactly representable in a double, and so is every sum and difference of
# them -- and it means the change did not have to fork ulab's arithmetic core,
# which is the part that would have to be re-merged on every upstream update.
# It also follows a rule ulab already had: uint16 + int16 => float.
#
# int64 is deliberately NOT added: it cannot round-trip through float64 above
# 2**53, so the same design would silently lose precision instead of being
# exactly right.
import numpy as np

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + (": " + detail if detail else ""))


# --- the type exists and is 32 bits -----------------------------------------

check("np.int32 exists", hasattr(np, "int32"))
a = np.array([1, 2, 3], dtype=np.int32)
check("dtype is int32", int(a.dtype) == int(np.int32), str(int(a.dtype)))
check("itemsize is 4", a.itemsize == 4, str(a.itemsize))
check("length", len(a) == 3)

# --- it holds what int16 could not ------------------------------------------

big = np.array([40000, 1000000, 2147483647, -2147483648], dtype=np.int32)
check("holds 40000", big[0] == 40000, str(big[0]))
check("holds a million", big[1] == 1000000, str(big[1]))
check("holds INT32_MAX", big[2] == 2147483647, str(big[2]))
check("holds INT32_MIN", big[3] == -2147483648, str(big[3]))

# A mesh index array is the motivating case.
idx = np.array([i * 7 for i in range(1000)], dtype=np.int32)
check("index array exact at the top", idx[999] == 6993, str(idx[999]))

# --- element access and assignment ------------------------------------------

a[1] = 123456
check("assignment", a[1] == 123456, str(a[1]))
a[2] = -99
check("negative assignment", a[2] == -99, str(a[2]))

# --- slicing, shape, views --------------------------------------------------

m = np.array([[1, 2, 3], [4, 5, 6]], dtype=np.int32)
check("2-D shape", m.shape == (2, 3), str(m.shape))
check("2-D element", m[1][2] == 6, str(m[1][2]))
check("slice", list(idx[:4]) == [0, 7, 14, 21], str(list(idx[:4])))
check("strided slice", list(idx[0:8:2]) == [0, 14, 28, 42],
      str(list(idx[0:8:2])))
check("reshape", np.array([1, 2, 3, 4], dtype=np.int32).reshape((2, 2)).shape
      == (2, 2))

# --- bytes in and out, which is what the matplotlib bridge needs ------------

raw = np.array([1, 70000, -3], dtype=np.int32).tobytes()
check("tobytes is 4 bytes per element", len(raw) == 12, str(len(raw)))
rt = np.frombuffer(raw, dtype=np.int32)
check("frombuffer round trip", list(rt) == [1, 70000, -3], str(list(rt)))

# --- arithmetic: exact, and float64 by design -------------------------------

s = np.array([100000, 200000], dtype=np.int32) + np.array([1, 2],
                                                          dtype=np.int32)
check("sum is exact", list(s) == [100001, 200002], str(list(s)))
check("arithmetic yields float", int(s.dtype) == int(np.float),
      str(int(s.dtype)))
check("scalar arithmetic", (np.array([5], dtype=np.int32) * 3)[0] == 15)
check("mixed with float", (np.array([2], dtype=np.int32) + np.array([0.5]))[0]
      == 2.5)
check("mixed with int16", (np.array([100000], dtype=np.int32)
                           + np.array([1], dtype=np.int16))[0] == 100001)
check("comparison", bool((np.array([5], dtype=np.int32)
                          > np.array([4], dtype=np.int32))[0]))
check("negation", (-np.array([2147483647], dtype=np.int32))[0] == -2147483647)
check("abs", abs(np.array([-1000000], dtype=np.int32))[0] == 1000000)

# --- the library functions that take any dtype ------------------------------

check("sum()", np.sum(np.array([1000000, 2000000], dtype=np.int32)) == 3000000,
      str(np.sum(np.array([1000000, 2000000], dtype=np.int32))))
check("min()", np.min(big) == -2147483648, str(np.min(big)))
check("max()", np.max(big) == 2147483647, str(np.max(big)))
check("sqrt of an int32", abs(np.sqrt(np.array([1000000], dtype=np.int32))[0]
                              - 1000.0) < 1e-9)

# --- printing ---------------------------------------------------------------

check("repr says int32", "int32" in repr(np.array([1], dtype=np.int32)),
      repr(np.array([1], dtype=np.int32)))

# --- the narrow types still behave exactly as before ------------------------
#
# Everything above must be additive: the existing dtypes are unchanged, down to
# int16 still wrapping, because that is upstream behaviour that other code (and
# the regression suite) may rely on.

check("int16 unchanged", int(np.array([1], dtype=np.int16).dtype) == 104)
check("int16 still 2 bytes", np.array([1], dtype=np.int16).itemsize == 2)
check("uint8 unchanged", np.array([1], dtype=np.uint8).itemsize == 1)
check("float still the default", int(np.array([1, 2]).dtype) == int(np.float))
check("float arithmetic untouched", (np.array([1.5]) * 2)[0] == 3.0)
check("complex untouched", np.array([1 + 2j], dtype=np.complex)[0] == 1 + 2j)

if fails:
    print("FAIL (%d)" % len(fails))
    for f in fails:
        print("  " + f)
else:
    print("ulab int32 check: 32-bit integers, exact arithmetic via float64, "
          "narrow dtypes unchanged")
