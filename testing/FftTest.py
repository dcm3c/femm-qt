# FftTest.py
# np.fft.fft / ifft accept any length, and use MATLAB's scaling.
#
#     build/bin/femm_headless -run testing/FftTest.py
#
# ulab's own FFT takes power-of-two lengths only.  That is an implementation
# limit rather than a mathematical one -- MATLAB and CPython numpy transform
# any length -- and a script translated from either should not have to resample
# its data to suit us.  The numpy proxy in libfemm/py_init_script.cpp therefore
# routes non-power-of-two lengths through Bluestein's algorithm, which turns the
# DFT into a convolution that the power-of-two transform can do.
#
# Powers of two must keep taking ulab's own path, so both are checked here: a
# regression that quietly sent everything through Bluestein would still be
# correct and would still pass a correctness-only test, so the fast path is
# asserted by timing rather than by result.
#
# The reference is a direct O(n^2) DFT computed here, so this test needs no
# CPython.  Cross-checked once against CPython numpy 2.4.4 over PyBridge, where
# the worst relative error across the same lengths was 8.3e-16 -- better than
# against this reference, whose own rounding is the larger term.

import math
import time

import numpy as np

fails = []


def dft(x, sign=-1):
    """Textbook DFT, unnormalized: sum x[j] exp(sign*2i pi jk/n). sign=+1 is
    the inverse kernel, without its 1/n -- the callers apply that, so the
    forward and inverse references come from the same six lines."""
    n = len(x)
    out = []
    for k in range(n):
        s = 0j
        for j in range(n):
            a = sign * 2.0 * math.pi * j * k / n
            s += x[j] * complex(math.cos(a), math.sin(a))
        out.append(s)
    return out


def sample(n):
    return [math.sin(0.7 * j) + 0.3 * math.cos(2.1 * j) + 0.1 * j / n
            for j in range(n)]


# --- matches a direct DFT at any length ------------------------------------
worst = 0.0
for n in (1, 2, 3, 5, 7, 12, 16, 17, 60, 100, 180, 256, 360):
    x = sample(n)
    ref = dft(x)
    got = np.fft.fft(np.array(x))
    scale = max(abs(v) for v in ref) or 1.0
    rel = max(abs(got[k] - ref[k]) for k in range(n)) / scale
    if rel > 1e-12:
        fails.append("fft(n=%d) differs from the DFT by %.2e of scale" % (n, rel))
    if rel > worst:
        worst = rel

# --- complex input, at both path lengths -----------------------------------
# The Bluestein path is complex by construction, but the power-of-two path hands
# the array to ulab, whose complex support is thin -- so a build could easily be
# right on one and drop the imaginary part on the other. Both are checked
# against the same direct DFT, which needs no change to accept complex input.
def csample(n):
    return [complex(math.sin(0.7 * j) + 0.3 * j / n,
                    math.cos(1.3 * j) - 0.2 * j / n) for j in range(n)]

for n in (1, 2, 4, 8, 64, 256, 3, 5, 7, 12, 60, 180, 250):
    x = csample(n)
    ref = dft(x)
    got = np.fft.fft(np.array(x))
    scale = max(abs(v) for v in ref) or 1.0
    rel = max(abs(got[k] - ref[k]) for k in range(n)) / scale
    if rel > 1e-12:
        fails.append("complex fft(n=%d) differs from the DFT by %.2e of scale"
                     % (n, rel))
    if rel > worst:
        worst = rel

# A complex round trip, one length per path.
for n in (64, 90):
    x = csample(n)
    back = np.fft.ifft(np.fft.fft(np.array(x)))
    err = max(abs(back[k] - x[k]) for k in range(n)) / max(abs(v) for v in x)
    if err > 1e-12:
        fails.append("complex ifft(fft(x)) at n=%d is off by %.2e" % (n, err))

# A purely imaginary signal is the case a real-only path gets visibly wrong
# rather than subtly wrong: it would return zeros.
z = np.fft.fft(np.array([1j, 2j, 3j]))
if abs(z[0] - 6j) > 1e-12:
    fails.append("fft([1j,2j,3j])[0] is %r, want 6j" % (complex(z[0]),))

# A Python list of complex must work as well as an array does -- scripts build
# data both ways, and only the array form goes through ulab's own dtype path.
z = np.fft.fft([1 + 2j, 3 - 1j, 0j, 2 + 0j])
if abs(z[0] - (6 + 1j)) > 1e-12:
    fails.append("fft of a complex LIST gave %r for X[0], want 6+1j"
                 % (complex(z[0]),))

# --- ifft inverts it, at any length ----------------------------------------
for n in (3, 7, 60, 128, 180):
    x = sample(n)
    back = np.fft.ifft(np.fft.fft(np.array(x)))
    err = max(abs(back[k].real - x[k]) for k in range(n))
    if err > 1e-12:
        fails.append("ifft(fft(x)) at n=%d is off by %.2e" % (n, err))

# --- 2-D input: shape preserved, one transform per line --------------------
# ulab's own fft rejects a 2-D array outright ("FFT is implemented for linear
# arrays only"), and the Bluestein wrapper used to FLATTEN one -- a (2,4) came
# back as a single length-8 transform, silently, which is the wrong answer in
# the shape a script reaches for most naturally (N elements x M time samples).
# numpy transforms along the LAST axis by default and keeps the shape.
#
# Worth stating because both are legal and only one is right: numpy's default
# is rows, MATLAB's fft() of a matrix is COLUMNS. A script ported from MATLAB
# wants axis=0.
def dft2_rows(m, sign=-1):
    return [dft(row, sign) for row in m]


def transpose(m):
    return [[m[i][j] for i in range(len(m))] for j in range(len(m[0]))]


def dft2_cols(m, sign=-1):
    return transpose(dft2_rows(transpose(m), sign))


def grid(r, c):
    return [[complex(math.sin(0.7 * (i * c + j)) + 0.1 * i,
                     math.cos(1.3 * (i * c + j)) - 0.05 * j)
             for j in range(c)] for i in range(r)]


def cmp2(label, get, ref):
    """`get` is a thunk, not a value, so a transform that RAISES is recorded as
    one failure instead of ending the whole row -- which is what the shape
    checks below would otherwise do to everything after them."""
    global worst
    try:
        got = get()
    except BaseException as e:
        fails.append("%s raised %r" % (label, e))
        return
    r, c = len(ref), len(ref[0])
    if tuple(got.shape) != (r, c):
        fails.append("%s returned shape %s, want (%d, %d)"
                     % (label, tuple(got.shape), r, c))
        return
    scale = max(abs(v) for row in ref for v in row) or 1.0
    rel = max(abs(got[i][j] - ref[i][j])
              for i in range(r) for j in range(c)) / scale
    if rel > 1e-12:
        fails.append("%s differs from the DFT by %.2e of scale" % (label, rel))
    if rel > worst:
        worst = rel


# (1,7) and (6,1) are in here because a degenerate 2-D array is exactly where a
# rows-vs-columns mix-up stops being visible in the shape.
for (r, c) in ((2, 4), (3, 5), (4, 8), (5, 12), (1, 7), (6, 1)):
    a = grid(r, c)
    A = np.array(a)
    n = "%dx%d" % (r, c)
    cmp2("fft(%s)" % n, lambda: np.fft.fft(A), dft2_rows(a))
    cmp2("fft(%s, axis=0)" % n, lambda: np.fft.fft(A, axis=0), dft2_cols(a))
    cmp2("ifft(%s)" % n, lambda: np.fft.ifft(A),
         [[v / c for v in row] for row in dft2_rows(a, +1)])
    cmp2("fft2(%s)" % n, lambda: np.fft.fft2(A), dft2_cols(dft2_rows(a)))
    cmp2("ifft2(%s)" % n, lambda: np.fft.ifft2(A),
         [[v / (r * c) for v in row] for row in dft2_cols(dft2_rows(a, +1), +1)])

# --- n= resizes, norm= rescales --------------------------------------------
# Both change the answer, so ignoring either would return something plausible
# and wrong. n= zero-pads or truncates to that transform length; norm= picks the
# convention, and 'ortho' -- 1/sqrt(N) each way -- is Mathematica's, which is
# what a script ported from there needs.
def resize(x, n):
    return [x[j] if j < len(x) else 0j for j in range(n)]


def scaled(ref, norm, n, inv):
    if norm == "ortho":
        f = 1.0 / math.sqrt(n)
    elif norm == "forward":
        f = 1.0 if inv else 1.0 / n
    else:                                  # 'backward', numpy's default
        f = 1.0 / n if inv else 1.0
    return [v * f for v in ref]


def cmp1(label, get, ref):
    global worst
    try:
        got = get()
    except BaseException as e:
        fails.append("%s raised %r" % (label, e))
        return
    if len(got) != len(ref):
        fails.append("%s returned %d values, want %d"
                     % (label, len(got), len(ref)))
        return
    scale = max(abs(v) for v in ref) or 1.0
    rel = max(abs(got[k] - ref[k]) for k in range(len(ref))) / scale
    if rel > 1e-12:
        fails.append("%s differs from the DFT by %.2e of scale" % (label, rel))
    if rel > worst:
        worst = rel


x9 = csample(9)
# 4 truncates, 9 is a no-op, 16 pads to a power of two (a different code path
# from 5, which pads to a Bluestein length).
for n in (4, 9, 16, 32, 5):
    cmp1("fft(x, %d)" % n, lambda n=n: np.fft.fft(np.array(x9), n),
         dft(resize(x9, n)))
    cmp1("ifft(x, %d)" % n, lambda n=n: np.fft.ifft(np.array(x9), n),
         scaled(dft(resize(x9, n), +1), "backward", n, True))

for nm in ("backward", "ortho", "forward"):
    cmp1("fft(x, norm=%r)" % nm, lambda nm=nm: np.fft.fft(np.array(x9), norm=nm),
         scaled(dft(x9), nm, 9, False))
    cmp1("ifft(x, norm=%r)" % nm, lambda nm=nm: np.fft.ifft(np.array(x9), norm=nm),
         scaled(dft(x9, +1), nm, 9, True))

# ortho must round-trip exactly, which backward does too but forward does not --
# so this catches a factor applied on the wrong side.
rt = np.fft.ifft(np.fft.fft(np.array(x9), norm="ortho"), norm="ortho")
if max(abs(rt[k] - x9[k]) for k in range(9)) > 1e-12:
    fails.append("ortho does not round-trip")

# On 2-D the normalisation is over BOTH axes for fft2 but only the transformed
# one for fft, which is the easy thing to get wrong.
g35 = grid(3, 5)
cmp2("fft(3x5, norm='ortho')",
     lambda: np.fft.fft(np.array(g35), norm="ortho"),
     [scaled(r, "ortho", 5, False) for r in dft2_rows(g35)])
cmp2("fft2(3x5, norm='ortho')",
     lambda: np.fft.fft2(np.array(g35), norm="ortho"),
     [[v / math.sqrt(15) for v in r] for r in dft2_cols(dft2_rows(g35))])
cmp2("fft2(3x5, s=(4,8))",
     lambda: np.fft.fft2(np.array(g35), (4, 8)),
     dft2_cols(dft2_rows([resize(r, 8) for r in g35] + [[0j] * 8])))

# What must still refuse.
def refuses(label, fn):
    try:
        fn()
        fails.append("%s was accepted; it should refuse" % label)
    except (NotImplementedError, ValueError, TypeError):
        pass


refuses("norm='nonsense'",
        lambda: np.fft.fft(np.array(sample(8)), norm="nonsense"))
refuses("fft(x, 0)", lambda: np.fft.fft(np.array(sample(8)), 0))
refuses("fft(x, -4)", lambda: np.fft.fft(np.array(sample(8)), -4))
refuses("fft(2-D, axis=2)", lambda: np.fft.fft(np.array(grid(2, 4)), axis=2))
refuses("fft2 of a 1-D array", lambda: np.fft.fft2(np.array(sample(8))))
refuses("fft2 with a 1-length s", lambda: np.fft.fft2(np.array(grid(2, 4)), (2,)))

# --- scaling is MATLAB's, not Mathematica's --------------------------------
# Forward carries no 1/N and the inverse carries all of it.  Mathematica's
# Fourier[] splits it as 1/sqrt(N) each way, so a script ported from there
# needs an adjustment that a MATLAB port does not -- worth pinning, because
# getting it wrong scales every result by N or sqrt(N) rather than failing.
for n in (8, 180):
    dc = abs(np.fft.fft(np.zeros(n) + 1.0)[0])
    if abs(dc - n) > 1e-9:
        fails.append("fft(ones(%d))[0] is %.6f, want %d (unnormalized forward)"
                     % (n, dc, n))

# A pure tone must come back at its own amplitude under the 2/N convention
# every FEMM loss script uses.
n, k0, amp = 180, 7, 3.0
tone = np.array([amp * math.cos(2 * math.pi * k0 * j / n) for j in range(n)])
got = abs(np.fft.fft(tone)[k0]) * 2.0 / n
if abs(got - amp) > 1e-9:
    fails.append("tone amplitude came back as %.6f, want %.1f" % (got, amp))

# --- powers of two still take ulab's native path ---------------------------
# Bluestein transforms a length-M >= 2N-1 buffer, so it cannot be as quick as
# the native transform of the same N.  If a change ever routed everything
# through it the results would stay right and only the speed would tell.
# BEST-of-N, not a mean, and ticks_us rather than time.time(). Both matter,
# and GhostBSD demonstrated why: there time.time() is coarse enough that 200
# power-of-two transforms measured as 0 s, so the `t_pow2 > 0` guard silently
# skipped this check entirely -- while under the full suite's load the same
# measurement came out non-zero, one scheduling hiccup inflated the MEAN, the
# ratio fell under 1.5, and the row failed. Passing alone and failing in the
# suite is the signature of a timing assertion, not of a bug.
#
# A minimum is the right statistic for "how fast can this go": noise only ever
# adds. Batches are also sized to clear the clock rather than assumed to.
def timeit(n, batches=5, floor_us=20000):
    x = np.array(sample(n))
    reps = 50
    while True:                       # grow until a batch clears the clock
        t0 = time.ticks_us()
        for _ in range(reps):
            np.fft.fft(x)
        if time.ticks_diff(time.ticks_us(), t0) >= floor_us or reps >= 12800:
            break
        reps *= 2
    best = None
    for _ in range(batches):
        t0 = time.ticks_us()
        for _ in range(reps):
            np.fft.fft(x)
        dt = time.ticks_diff(time.ticks_us(), t0)
        if best is None or dt < best:
            best = dt
    return float(best) / reps          # microseconds per transform

t_pow2 = timeit(256)
t_blue = timeit(250)
if t_pow2 <= 0:
    fails.append("cannot time the fft: 256-point transforms measured as zero "
                 "even after growing the batch")
elif t_blue / t_pow2 < 1.5:
    fails.append("n=256 (%.1f us) is not meaningfully faster than n=250 (%.1f us) "
                 "-- the power-of-two fast path may have been lost"
                 % (t_pow2, t_blue))

if fails:
    for m in fails:
        print("   " + m)
    print("fft: %d FAILED" % len(fails))
else:
    print("fft check: any length, real or complex, 1-D or 2-D, matches a direct "
          "DFT (worst %.1e of scale); ifft inverts it, fft2/axis=0 agree, "
          "scaling is MATLAB's, and powers of two still take ulab's fast path "
          "(%.0f us vs %.0f us at n=256/250)"
          % (worst, t_pow2, t_blue))
