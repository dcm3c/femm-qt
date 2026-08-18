# The §8.4 budget, measured from inside femm-qt rather than between two
# CPythons.
#
#     build/bin/femm_headless -run testing/PyBridgeBenchTest.py
#
# pybridge/tests/bench_arrays.py measures the same transfers CPython-to-CPython
# and is the floor; this is the number that matters, because it includes the
# two copies the design document flagged as unmeasured -- the frame into a
# MicroPython `bytes`, and that again into the host's std::string.
#
# It also reports peak interpreter heap, which the plan asked for and the
# CPython benchmark cannot supply: an 8 MB frame plus the source array is
# several times the 2 MB the GC heap starts at, so the question of whether the
# copies matter is a memory question as much as a timing one.
#
# Budget: 1e6 float64 across and decoded in <= 150 ms.  Prints the numbers
# either way; only fails if the budget is blown, since timings on a loaded
# machine are noisy and a slow run is not a broken one.
import gc
import time

import pybridge

BUDGET_MS = 150
N = 1000000


def bench(label, fn, repeat=3):
    """Best of `repeat`, with the heap growth of a single run."""
    best = None
    gc.collect()
    before = gc.mem_alloc()
    peak = before
    for _ in range(repeat):
        t0 = time.ticks_us()
        fn()
        dt = time.ticks_diff(time.ticks_us(), t0) / 1000.0
        used = gc.mem_alloc()
        if used > peak:
            peak = used
        if best is None or dt < best:
            best = dt
        gc.collect()
    print("  %-38s %8.1f ms   heap +%.1f MB"
          % (label, best, (peak - before) / 1048576.0))
    return best


def main():
    try:
        pybridge.start()
    except pybridge.BridgeError as e:
        print("SKIP: no usable Python for the matplotlib bridge")
        print("  " + str(e).split("\n")[0])
        return 0

    import numpy as np

    print("PyBridge transfer benchmark (%d float64 = %.1f MB)"
          % (N, N * 8 / 1e6))
    x = np.linspace(0.0, 1.0, N)

    put_ms = bench("put: encode + transfer + decode", lambda: pybridge.put("x", x))
    pybridge.put("x", x)
    xp = pybridge.eval("x")
    fetch_ms = bench("fetch: back into a ulab array",
                     lambda: pybridge.fetch(xp))

    # int32 is femm-qt's addition to ulab, and the reason an index array now
    # crosses in four bytes per element rather than eight.
    idx = np.array([i for i in range(N // 4)], dtype=np.int32)
    bench("put: 250k int32 indices (i4, not f8)",
          lambda: pybridge.put("idx", idx))

    # The worst case a user will actually hit: a plain Python list.
    lst = [float(i) for i in range(100000)]
    bench("put: 1e5-element plain list", lambda: pybridge.put("l", lst))

    # The round-trip floor decides whether the two-RPC attribute path is worth
    # optimising away (plan §5.2).
    n = 200
    t0 = time.ticks_us()
    for _ in range(n):
        pybridge.eval("1")
    floor = time.ticks_diff(time.ticks_us(), t0) / 1000.0 / n
    print("  %-38s %8.3f ms" % ("empty round trip", floor))

    plt = pybridge.import_module("matplotlib.pyplot")
    fig, ax = plt.subplots()
    bench("ax.plot() of the same array",
          lambda: pybridge.call(ax.plot, xp) if hasattr(pybridge, "call")
          else ax.plot(xp), 2)
    plt.close(fig)
    pybridge.shutdown()

    over = [n for n, v in (("put", put_ms), ("fetch", fetch_ms))
            if v > BUDGET_MS]
    if over:
        print("OVER BUDGET (%d ms): %s" % (BUDGET_MS, ", ".join(over)))
        return 1
    print("PyBridgeBench OK: 8 MB across and decoded in %.0f/%.0f ms "
          "(budget %d), round trip %.2f ms"
          % (put_ms, fetch_ms, BUDGET_MS, floor))
    return 0


if main():
    raise SystemExit(1)
