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


def dft(x):
    """Textbook DFT, unnormalized: sum x[j] exp(-2i pi jk/n)."""
    n = len(x)
    out = []
    for k in range(n):
        s = 0j
        for j in range(n):
            a = -2.0 * math.pi * j * k / n
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

# --- ifft inverts it, at any length ----------------------------------------
for n in (3, 7, 60, 128, 180):
    x = sample(n)
    back = np.fft.ifft(np.fft.fft(np.array(x)))
    err = max(abs(back[k].real - x[k]) for k in range(n))
    if err > 1e-12:
        fails.append("ifft(fft(x)) at n=%d is off by %.2e" % (n, err))

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
def timeit(n, reps):
    x = np.array(sample(n))
    t0 = time.time()
    for _ in range(reps):
        np.fft.fft(x)
    return (time.time() - t0) / reps

t_pow2 = timeit(256, 200)
t_blue = timeit(250, 200)
if t_pow2 > 0 and t_blue / t_pow2 < 1.5:
    fails.append("n=256 (%.1f us) is not meaningfully faster than n=250 (%.1f us) "
                 "-- the power-of-two fast path may have been lost"
                 % (t_pow2 * 1e6, t_blue * 1e6))

if fails:
    for m in fails:
        print("   " + m)
    print("fft: %d FAILED" % len(fails))
else:
    print("fft check: any length matches a direct DFT (worst %.1e of scale), "
          "ifft inverts it, scaling is MATLAB's, and powers of two still take "
          "ulab's fast path (%.0f us vs %.0f us at n=256/250)"
          % (worst, t_pow2 * 1e6, t_blue * 1e6))
