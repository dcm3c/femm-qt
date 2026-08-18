# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""Bulk-transfer benchmark for the PyBridge binary path (plan §8.4).

    python3 bench_arrays.py [--points 1000000] [--repeat 5]

Budget: 1e6 float64 across the wire and decoded server-side in <= 150 ms, plus
the plain-list path at 1e5 elements, which is the worst case users will hit
anyway because a list is what you get from a Python loop.

Peak interpreter heap is the other half of §8.4 and cannot be measured here:
the copies that matter (MicroPython bytes -> QByteArray) exist only in the real
client.  This file measures the wall-clock half and the frame sizes; the heap
half belongs to the M3/M4 benchmark inside femm-qt.
"""

import argparse
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fake_client import Bridge  # noqa: E402

try:
    import numpy
except ImportError:
    numpy = None


def timed(fn, repeat):
    times = []
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        times.append((time.perf_counter() - t0) * 1e3)
    return min(times), statistics.median(times)


def report(label, best, med, budget=None):
    verdict = ""
    if budget is not None:
        verdict = "   [%s, budget %d ms]" % ("OK" if best <= budget else "OVER",
                                             budget)
    print("%-46s %8.1f ms best %8.1f ms median%s" % (label, best, med, verdict))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--points", type=int, default=1_000_000)
    ap.add_argument("--repeat", type=int, default=5)
    args = ap.parse_args()
    if numpy is None:
        print("numpy is required for this benchmark")
        return 1

    br = Bridge().start()
    try:
        print("worker: %s, numpy %s, matplotlib %s, backend %s"
              % (br.hello["python"].split()[0], br.hello["numpy"],
                 br.hello["matplotlib"], br.hello["backend"]))
        n = args.points
        arr = numpy.linspace(0.0, 1.0, n)
        print("array: %d float64 = %.1f MB" % (n, arr.nbytes / 1e6))

        report("encode only (client side)",
               *timed(lambda: br.enc(arr), args.repeat))
        report("put: transfer + server-side decode",
               *timed(lambda: br.put("x", arr), args.repeat), budget=150)

        br.put("x", arr)
        xp = br.eval("x")
        report("fetch: server -> client + decode",
               *timed(lambda: br.fetch(xp), args.repeat), budget=150)

        lst = [float(i) for i in range(100_000)]
        report("plain list, 1e5 elements (put)",
               *timed(lambda: br.put("l", lst), args.repeat))

        plt = br.import_module("matplotlib.pyplot")
        fig, ax = plt.subplots()
        y = numpy.sin(40.0 * arr)

        def plot_once():
            br.call(ax.plot, arr, y)

        report("plot() of the same array", *timed(plot_once, max(1, args.repeat // 2)))

        br.exec("import io")
        def render():
            br.exec("_buf = io.BytesIO()")
            br.call(fig.savefig, br.eval("_buf"), format="png")

        report("savefig to memory (render)", *timed(render, 2))
        print("\nround-trip floor:")
        report("empty round trip (eval '1')",
               *timed(lambda: br.eval("1"), 50))
    finally:
        br.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
