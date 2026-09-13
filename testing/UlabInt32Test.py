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
# So this build adds int32, and since 2026-08-28 it is a FULL dtype: arithmetic
# on it stays integral, the way NumPy's does.  Until then arithmetic produced
# float64, which was exact but broke the use the type was added for -- `conn-1`
# came back float, and np.take refuses a float index.
#
# int64 is deliberately NOT added: it cannot round-trip through float64 above
# 2**53, and unlike int32 it could not simply be widened when mixed with a
# float.
#
# THE SWEEP AT THE END IS THE POINT OF THIS FILE.  Every dtype chain in ulab
# ends in an unconditional "assume float" branch, so a function that was never
# taught about int32 does not raise -- it reads four-byte integers as eight-byte
# doubles and returns plausible garbage.  CPython NumPy cannot do this: it
# dispatches to typed ufunc loops and raises when none matches.  Here the only
# way to find such a function is to call every one of them and compare against
# the float64 answer.  That is how sort/argsort/median/nonzero were caught when
# int32 first landed, and it is why this file sweeps dir(numpy) rather than
# listing a fixed set of expected values.
import numpy as np

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + (": " + detail if detail else ""))


I32 = int(np.int32)
F64 = int(np.float)
BOOL = int(np.uint8)

# --- the type exists and is 32 bits -----------------------------------------

check("np.int32 exists", hasattr(np, "int32"))
a = np.array([1, 2, 3], dtype=np.int32)
check("dtype is int32", int(a.dtype) == I32, str(int(a.dtype)))
check("itemsize is 4", a.itemsize == 4, str(a.itemsize))
check("length", len(a) == 3)

# --- it holds what int16 could not ------------------------------------------

big = np.array([40000, 1000000, 2147483647, -2147483648], dtype=np.int32)
check("holds 40000", big[0] == 40000, str(big[0]))
check("holds a million", big[1] == 1000000, str(big[1]))
check("holds INT32_MAX", big[2] == 2147483647, str(big[2]))
check("holds INT32_MIN", big[3] == -2147483648, str(big[3]))

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

# --- arithmetic keeps the dtype, as NumPy does ------------------------------
#
# Result dtypes below were taken from CPython NumPy 2.4.4, not guessed.

s = np.array([100000, 200000], dtype=np.int32) + np.array([1, 2],
                                                          dtype=np.int32)
check("sum is exact", list(s) == [100001, 200002], str(list(s)))
check("int32 + int32 -> int32", int(s.dtype) == I32, str(int(s.dtype)))

x32 = np.array([12, -7], dtype=np.int32)


def dt(v):
    return int(v.dtype)


check("int32 - scalar -> int32", dt(x32 - 1) == I32, str(dt(x32 - 1)))
check("int32 + scalar -> int32", dt(x32 + 1) == I32, str(dt(x32 + 1)))
check("int32 * scalar -> int32", dt(x32 * 2) == I32, str(dt(x32 * 2)))
check("int32 // scalar -> int32", dt(x32 // 2) == I32, str(dt(x32 // 2)))
check("int32 % scalar -> int32", dt(x32 % 5) == I32, str(dt(x32 % 5)))
check("int32 ** scalar -> int32", dt(x32 ** 2) == I32, str(dt(x32 ** 2)))
check("int32 & scalar -> int32", dt(x32 & 3) == I32, str(dt(x32 & 3)))
check("int32 | scalar -> int32", dt(x32 | 3) == I32, str(dt(x32 | 3)))
check("int32 ^ scalar -> int32", dt(x32 ^ 3) == I32, str(dt(x32 ^ 3)))
# true division is the one arithmetic operator that leaves the integers
check("int32 / scalar -> float", dt(x32 / 2) == F64, str(dt(x32 / 2)))
check("int32 + float -> float", dt(x32 + 0.5) == F64, str(dt(x32 + 0.5)))
check("float + int32 -> float", dt(0.5 + x32) == F64, str(dt(0.5 + x32)))

# a scalar too big for the 16-bit types stays integral now that int32 exists
check("int32 - big scalar -> int32", dt(x32 - 100000) == I32,
      str(dt(x32 - 100000)))
check("int32 - big scalar value", (np.array([100001], dtype=np.int32)
                                   - 100000)[0] == 1)

# mixed with each narrower integer type -- every one fits in an int32
for name, narrow in (("uint8", np.uint8), ("int8", np.int8),
                     ("uint16", np.uint16), ("int16", np.int16)):
    r = np.array([1000], dtype=np.int32) + np.array([2], dtype=narrow)
    check("int32 + %s -> int32" % name, int(r.dtype) == I32, str(int(r.dtype)))
    check("int32 + %s value" % name, r[0] == 1002, str(r[0]))
    r = np.array([2], dtype=narrow) + np.array([1000], dtype=np.int32)
    check("%s + int32 -> int32" % name, int(r.dtype) == I32, str(int(r.dtype)))

# comparisons give a boolean array, not an int32 one
c = np.array([1, 3, 3], dtype=np.int32)
for label, v in (("==", c == 3), ("!=", c != 3), ("<", c < 3), ("<=", c <= 3),
                 (">", c > 1), (">=", c >= 3)):
    check("int32 %s -> boolean" % label, v.dtype == np.uint8 and bool(v[1]) in
          (True, False), str(int(v.dtype)))
check("int32 == value", list(c == 3) == [False, True, True], str(list(c == 3)))
check("int32 < value", list(c < 3) == [True, False, False], str(list(c < 3)))
check("boolean mask on int32", list(np.array([10, 20, 30],
                                             dtype=np.int32)[c == 3])
      == [20, 30])

check("negation", (-np.array([2147483647], dtype=np.int32))[0] == -2147483647)
check("abs", abs(np.array([-1000000], dtype=np.int32))[0] == 1000000)

# --- integer semantics, matching NumPy --------------------------------------
#
# // and % floor toward negative infinity (Python's rule), NOT C's truncation.

check("floor div negative", list(np.array([-7], dtype=np.int32) // 2) == [-4],
      str(list(np.array([-7], dtype=np.int32) // 2)))
check("floor div negative divisor",
      list(np.array([7], dtype=np.int32) // -2) == [-4],
      str(list(np.array([7], dtype=np.int32) // -2)))
check("modulo sign follows divisor",
      list(np.array([-7], dtype=np.int32) % 3) == [2],
      str(list(np.array([-7], dtype=np.int32) % 3)))
check("modulo negative divisor",
      list(np.array([7], dtype=np.int32) % -3) == [-2],
      str(list(np.array([7], dtype=np.int32) % -3)))
check("division by zero yields 0",
      list(np.array([5], dtype=np.int32) // np.array([0], dtype=np.int32))
      == [0])
check("modulo by zero yields 0",
      list(np.array([5], dtype=np.int32) % np.array([0], dtype=np.int32))
      == [0])

try:
    np.array([2], dtype=np.int32) ** np.array([-1], dtype=np.int32)
    check("negative power raises", False, "no exception")
except ValueError:
    check("negative power raises", True)
except Exception as e:
    check("negative power raises", False, type(e).__name__)

# multiplication wraps at 32 bits, as NumPy's int32 does
w = np.array([100000], dtype=np.int32) * np.array([100000], dtype=np.int32)
check("int32 multiply wraps", int(w.dtype) == I32 and w[0] == 1410065408,
      "%s %s" % (int(w.dtype), w[0]))

# --- in-place operators -----------------------------------------------------
#
# Matching what ulab already does for its own integer dtypes, measured on int16
# and uint8: /= and **= are refused, the rest preserve the dtype.

p = np.array([4, 8], dtype=np.int32)
p += 1
check("int32 += preserves dtype", int(p.dtype) == I32 and list(p) == [5, 9],
      "%s %s" % (int(p.dtype), list(p)))
p = np.array([4, 8], dtype=np.int32)
p -= 1
check("int32 -= preserves dtype", int(p.dtype) == I32 and list(p) == [3, 7],
      "%s %s" % (int(p.dtype), list(p)))
p = np.array([4, 8], dtype=np.int32)
p *= 2
check("int32 *= preserves dtype", int(p.dtype) == I32 and list(p) == [8, 16],
      "%s %s" % (int(p.dtype), list(p)))
p = np.array([5, 9], dtype=np.int32)
p %= 3
check("int32 %= preserves dtype", int(p.dtype) == I32 and list(p) == [2, 0],
      "%s %s" % (int(p.dtype), list(p)))
p = np.array([4, 8], dtype=np.int32)
p //= 2
check("int32 //= preserves dtype", int(p.dtype) == I32 and list(p) == [2, 4],
      "%s %s" % (int(p.dtype), list(p)))
for label, fn in (("/=", "div"), ("**=", "pow")):
    q = np.array([4, 8], dtype=np.int32)
    try:
        if fn == "div":
            q /= 2
        else:
            q **= 2
        check("int32 %s refused" % label, False, "no exception")
    except TypeError:
        check("int32 %s refused" % label, True)
    except Exception as e:
        check("int32 %s refused" % label, False, type(e).__name__)

# --- the motivating case ----------------------------------------------------
#
# 1-based mesh connectivity, converted to a 0-based gather index. Before int32
# arithmetic was implemented, `conn - 1` came back float64 and np.take raised
# "can't convert float to int".

nodes = np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]])
conn = np.array([[1, 2, 3], [2, 4, 3]], dtype=np.int32)   # 1-based, as the API
try:
    got = np.take(nodes, conn[:, 0] - 1, axis=0)
    check("take with a computed int32 index",
          [list(r) for r in got] == [[0.0, 0.0], [1.0, 0.0]],
          str([list(r) for r in got]))
except Exception as e:
    check("take with a computed int32 index", False,
          "%s: %s" % (type(e).__name__, e))
check("column slice keeps int32", int((conn[:, 0]).dtype) == I32,
      str(int((conn[:, 0]).dtype)))

# --- 2-D and broadcast ------------------------------------------------------

m2 = np.array([[1, 2, 3], [4, 5, 6]], dtype=np.int32)
r2 = m2 - 1
check("2-D arithmetic dtype", int(r2.dtype) == I32, str(int(r2.dtype)))
check("2-D arithmetic values", [list(r) for r in r2] == [[0, 1, 2], [3, 4, 5]],
      str([list(r) for r in r2]))
r3 = m2 + np.array([10, 20, 30], dtype=np.int32)      # broadcast over rows
check("broadcast dtype", int(r3.dtype) == I32, str(int(r3.dtype)))
check("broadcast values",
      [list(r) for r in r3] == [[11, 22, 33], [14, 25, 36]],
      str([list(r) for r in r3]))
check("empty int32 result dtype",
      int((np.array([], dtype=np.int32) + np.array([], dtype=np.uint8)).dtype)
      == I32)

# --- printing ---------------------------------------------------------------

check("repr says int32", "int32" in repr(np.array([1], dtype=np.int32)),
      repr(np.array([1], dtype=np.int32)))

# --- the narrow types still behave exactly as before ------------------------
#
# Everything above must be additive: the existing dtypes are unchanged, down to
# int16 still wrapping, because that is upstream behaviour that other code (and
# the regression suite) may rely on.  ulab is already NumPy-correct here, which
# is why int32 was given its own path rather than every integer being widened.

check("int16 unchanged", int(np.array([1], dtype=np.int16).dtype) != I32)
check("int16 still 2 bytes", np.array([1], dtype=np.int16).itemsize == 2)
check("uint8 unchanged", np.array([1], dtype=np.uint8).itemsize == 1)
check("int16 arithmetic still int16",
      int((np.array([5], dtype=np.int16) - 1).dtype)
      == int(np.array([1], dtype=np.int16).dtype))
check("uint8 arithmetic still uint8",
      int((np.array([5], dtype=np.uint8) + 1).dtype)
      == int(np.array([1], dtype=np.uint8).dtype))
check("uint8 + uint16 unchanged",
      int((np.array([5], dtype=np.uint8) + np.array([5], dtype=np.uint16)).dtype)
      == int(np.array([1], dtype=np.uint16).dtype))
check("float still the default", int(np.array([1, 2]).dtype) == F64)
check("float arithmetic untouched", (np.array([1.5]) * 2)[0] == 3.0)
check("complex untouched", np.array([1 + 2j], dtype=np.complex)[0] == 1 + 2j)

# ---------------------------------------------------------------------------
# THE SWEEP
# ---------------------------------------------------------------------------
# Call every callable in dir(numpy) with an int32 array and with the float64
# array holding the same values, and compare.  A function that was never taught
# about int32 shows up here as either a different answer or a one-sided
# exception -- it cannot show up as an error on its own, which is the whole
# reason this is a sweep and not a list.
#
# Self-selecting: anything that raises for BOTH dtypes is not a one-argument
# array function and is skipped.  Only a DIFFERENCE between the two is a
# failure.

VALUES = [1, 2, 3, 7, 12, 40000, 1000000, 5]
sweep_i = np.array(VALUES, dtype=np.int32)
sweep_f = np.array([float(v) for v in VALUES])

# Skipped because they print, mutate global state, or are constructors that
# read their arguments as a SHAPE rather than as data. The constructors matter:
# handed an array of values they will happily try to allocate that many
# elements, and np.zeros/np.full/np.eye with a 1000000 in the shape takes the
# process down before the sweep can report anything. They also test nothing
# about int32 data handling, since none of them reads the input as values.
SKIP = ("ndinfo", "set_printoptions", "get_printoptions", "save", "savetxt",
        "load", "loadtxt", "frombuffer", "array", "asarray", "ndarray",
        "vectorize",
        "zeros", "ones", "empty", "eye", "full", "arange", "linspace",
        "logspace", "meshgrid")


def flatten(v):
    """Return v as a flat list of floats, or None if it cannot be compared."""
    try:
        if isinstance(v, bool) or isinstance(v, int) or isinstance(v, float):
            return [float(v)]
        if isinstance(v, complex):
            return [v.real, v.imag]
        if isinstance(v, (tuple, list)):
            out = []
            for e in v:
                sub = flatten(e)
                if sub is None:
                    return None
                out.extend(sub)
            return out
        if hasattr(v, "flatten"):
            return flatten(v.flatten().tolist())
        if hasattr(v, "tolist"):
            return flatten(v.tolist())
    except Exception:
        return None
    return None


def describe(v):
    """What a result IS -- dtype and shape for an array, else its type."""
    try:
        return "dtype=%d shape=%s" % (int(v.dtype), v.shape)
    except Exception:
        pass
    try:
        return "%s len=%d" % (type(v).__name__, len(v))
    except Exception:
        return type(v).__name__


def compare(label, res_i, err_i, res_f, err_f, report):
    """Compare one int32 result against its float64 twin.

    Returns True if the pair was actually compared, False if it was skipped.
    `report` collects failures, so the canary below can drive this exact code
    path with a pair that is KNOWN to differ.
    """
    if err_i is not None and err_f is not None:
        return False                       # wrong arity / unsupported for both
    if (err_i is None) != (err_f is None):
        report.append("%s: raises for one dtype only (int32=%s float64=%s)"
                      % (label, err_i or "ok", err_f or "ok"))
        return True
    fi, ff = flatten(res_i), flatten(res_f)
    if fi is None or ff is None:
        return True                        # ran, but returns nothing comparable
    if len(fi) != len(ff):
        report.append("%s: different result length, %d vs %d"
                      % (label, len(fi), len(ff)))
        return True
    for k in range(len(fi)):
        if abs(fi[k] - ff[k]) > 1e-9 * max(1.0, abs(ff[k])):
            # Two bare numbers are not enough to act on. A mismatch here was
            # once reported as "differs at 63, int32 1e-09 vs float64 4.0" --
            # a value no int32 array can hold, which said the result was not
            # the dtype it should have been, but not what it WAS. So say what
            # both results actually are, and print them whole when they are
            # small enough to read: the finding usually arrives from a machine
            # that is not to hand, and a second round trip costs a day.
            # ONE report entry, not three: the canary below asserts the
            # comparator produces exactly one finding for a known difference.
            msg = ("%s: differs at %d, int32 %r vs oracle %r  [%s | %s]"
                   % (label, k, fi[k], ff[k],
                      describe(res_i), describe(res_f)))
            if 1 < len(fi) <= 64:
                msg += "\n      int32  %r\n      oracle %r" % (fi, ff)
            report.append(msg)
            return True
    return True


def call(fn, *args):
    try:
        return fn(*args), None
    except Exception as e:
        return None, type(e).__name__


def sweep(tag, argsets_i, argsets_f, report):
    """argsets_* are the argument tuples to try, int32 and float64 versions."""
    ran = 0
    skip = 0
    for name in sorted(dir(np)):
        if name.startswith("_") or name in SKIP:
            continue
        try:
            fn = getattr(np, name)
        except Exception:
            continue
        if not callable(fn):
            continue
        ri, ei = call(fn, *argsets_i)
        rf, ef = call(fn, *argsets_f)
        if compare("%s %s" % (tag, name), ri, ei, rf, ef, report):
            ran += 1
        else:
            skip += 1
    return ran, skip


report = []

# Two oracles, because neither alone is right.
#
#   float64 -- the natural comparison for elementwise maths, but WRONG for the
#      integer-only functions: np.bitwise_and and np.take are supposed to reject
#      a float array, so "int32 works, float64 raises" is correct behaviour
#      there, not a finding.
#
#   int16 -- the better oracle for everything else, and the sharper question:
#      is int32 as well supported as the narrow dtype it exists to replace?
#      Any difference is a genuine int32 gap. Needs values that fit in int16,
#      so it cannot also cover the large-magnitude cases.
#
# So: float64 for the one-argument maths at full int32 magnitude, int16 for
# everything else at small magnitude.

# 1-D, one argument, large values -- elementwise maths and reductions.
n1, s1 = sweep("sweep1", (sweep_i,), (sweep_f,), report)

SMALL = [1, 2, 3, 7, 12, 5, 9, 4]
INDEX = [0, 1, 2, 3, 4, 5, 6, 7]
sa_i = np.array(SMALL, dtype=np.int32)
sb_i = np.array(INDEX, dtype=np.int32)
sa_h = np.array(SMALL, dtype=np.int16)
sb_h = np.array(INDEX, dtype=np.int16)

# 1-D, one argument, against int16.
n1b, s1b = sweep("sweep1b", (sa_i,), (sa_h,), report)

# 1-D, two arguments. The index-like functions (take, compress, roll, delete)
# get legal arguments here instead of raising for both dtypes and being
# skipped -- that is what hid bitwise_and, take and the shifts from sweep1.
n2, s2 = sweep("sweep2", (sa_i, sb_i), (sa_h, sb_h), report)

# 2-D, one argument -- diag, trace and anything with an axis default.
grid = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
g_i = np.array(grid, dtype=np.int32)
g_h = np.array(grid, dtype=np.int16)
n3, s3 = sweep("sweep3", (g_i,), (g_h,), report)

for msg in report:
    check(msg, False)

swept = n1 + n1b + n2 + n3
check("sweep actually ran", swept >= 60, "only %d calls" % swept)

# --- the three places int32 had not reached (2026-09-01) ---------------------
#
# All three predate the dtype and were found by asking why arange widens int16
# to FLOAT when int32 exists.  They are items 7-9 in
# micropython/ulab/FEMM-MODIFICATIONS.md, and none of them fails loudly if a
# future re-vendor drops the edit, which is why they are pinned here.

# 1. nonzero(): the counter and the indices were both uint16, so an input
#    longer than 65535 came back the wrong LENGTH holding indices reduced mod
#    65536 -- silently.  np.take then gathered the wrong elements.  A mesh past
#    65535 elements is ordinary; this tree ships a 156k-node example.
for N in (70000, 200000):
    a = np.zeros(N)
    a[N - 1] = 1.0
    idx = np.nonzero(a)[0]
    check("nonzero(%d) finds one hit" % N, len(idx) == 1,
          "got %d" % len(idx))
    check("nonzero(%d) index is not wrapped" % N, idx[0] == N - 1,
          "got %s, want %d" % (idx[0], N - 1))
    check("nonzero(%d) is int32" % N, int(idx.dtype) == I32,
          "dtype %d" % idx.dtype)

# The count is a size_t now, so more hits than a uint16 holds is fine too.
big = np.zeros(70000) + 1.0
check("nonzero counts past 65535 hits", len(np.nonzero(big)[0]) == 70000,
      "got %d" % len(np.nonzero(big)[0]))

# SIGNED, as numpy's is: an index array is routinely used as `idx - 1`, and on
# an unsigned type that turns element 0 into a huge positive number.
z = np.zeros(4)
z[0] = 1.0
check("nonzero()[0] - 1 gives -1", (np.nonzero(z)[0] - 1)[0] == -1,
      "got %s" % (np.nonzero(z)[0] - 1)[0])

# 2. arange(): escaped int16 -> float, so np.take(x, arange(n)) worked for
#    n <= 32768 and failed above it -- the same line, deciding on length.
check("arange stays int16 while it fits", int(np.arange(1000).dtype) == int(np.int16))
check("arange escapes to int32, not float", int(np.arange(40000).dtype) == I32,
      "dtype %d" % np.arange(40000).dtype)
check("arange(32769) is int32", int(np.arange(32769).dtype) == I32)
check("arange(40000) counts correctly", np.arange(40000)[-1] == 39999)
_x = np.zeros(40000)
check("np.take accepts arange(40000)", len(np.take(_x, np.arange(40000))) == 40000)
# The guard on that escape: it also fires for FLOAT arguments, and retargeting
# it without checking the current dtype makes arange(100000.5) return integers.
check("float arguments still give float", int(np.arange(100000.5).dtype) == F64,
      "dtype %d" % np.arange(100000.5).dtype)

# 3. Promotion: two pairs need a wider SIGNED type than either operand holds.
#    numpy gives int32 for both.  Upstream said float for one and uint16 for
#    the other -- and the uint16 answer is silently WRONG for a negative int8.
_u16 = np.array([1, 2], dtype=np.uint16)
_i16 = np.array([1, 2], dtype=np.int16)
_i8 = np.array([1, 2], dtype=np.int8)
check("uint16 + int16 -> int32", int((_u16 + _i16).dtype) == I32,
      "dtype %d" % (_u16 + _i16).dtype)
check("int8 + uint16 -> int32", int((_i8 + _u16).dtype) == I32,
      "dtype %d" % (_i8 + _u16).dtype)
check("int8(-5) + uint16(10) == 5",
      (np.array([-5], dtype=np.int8) + np.array([10], dtype=np.uint16))[0] == 5,
      "got %s" % (np.array([-5], dtype=np.int8) + np.array([10], dtype=np.uint16))[0])
# The narrow pairs that were already right must stay right.
check("uint8 + uint16 -> uint16 still",
      int((np.array([1], dtype=np.uint8) + _u16).dtype) == int(np.uint16))
check("uint8 + int8 -> int16 still",
      int((np.array([1], dtype=np.uint8) + _i8).dtype) == int(np.int16))
# An empty result must be typed the same as a non-empty one -- these used to be
# two separate hand-kept tables.
check("empty uint16+int16 agrees with non-empty",
      int((np.array([], dtype=np.uint16) + _i16).dtype) == int((_u16 + _i16).dtype))

# --- the canary -------------------------------------------------------------
#
# A sweep that reports nothing is worthless unless it is known to be capable of
# reporting something.  Drive the SAME comparator with a pair that must differ:
# int16 wraps 40000 to -25536, float64 does not.  If this does not produce a
# finding, the machinery above is broken and its silence means nothing.

canary = []
wrap_i = np.array([40000], dtype=np.int16)
wrap_f = np.array([40000.0])
compare("canary sum", np.sum(wrap_i), None, np.sum(wrap_f), None, canary)
check("canary: comparator detects a known difference", len(canary) == 1,
      "comparator reported %d findings for int16's wrap" % len(canary))

# ...and that a one-sided exception is caught too.
canary2 = []
compare("canary raise", None, "TypeError", np.sum(wrap_f), None, canary2)
check("canary: comparator detects a one-sided exception", len(canary2) == 1)

if fails:
    print("FAIL (%d)" % len(fails))
    for f in fails:
        print("  " + f)
else:
    print("ulab int32 check: full arithmetic dtype, NumPy semantics, narrow "
          "dtypes unchanged; swept %d numpy calls (%d+%d 1-arg, %d 2-arg, "
          "%d 2-D; %d skipped as wrong-arity), comparator canary armed"
          % (swept, n1, n1b, n2, n3, s1 + s1b + s2 + s3))
