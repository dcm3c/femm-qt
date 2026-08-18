# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""The M1 acceptance run: the plan's opening example, through a real window.

    python3 demo_interactive.py [--backend qtagg] [--hold 8]

Same script as the top of pybridge-implementation-plan.md, driven by the fake
client against a live worker on a real interactive backend.  Watch for three
things, because they are what the headless suite cannot show:

  * a window appears, with a working zoom/pan/save toolbar;
  * it stays responsive while the client is idle -- the worker pumps its own
    event loop between round trips, which is why a femm-qt script can end with
    figures still alive;
  * closing the window does not disturb the bridge; the client keeps working.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fake_client import Bridge  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", default="", help="blank = let the worker probe")
    ap.add_argument("--hold", type=float, default=8.0,
                    help="seconds to leave the window up (0 = until closed)")
    args = ap.parse_args()

    br = Bridge(backend=args.backend or None).start()
    try:
        print("backend: %s   (matplotlib %s, python %s)"
              % (br.hello["backend"], br.hello["matplotlib"],
                 br.hello["python"].split()[0]))
        if br.hello["backend"] in (None, "agg", "Agg"):
            print("no interactive backend: %s" % br.hello["backend_error"])
            return 1

        plt = br.import_module("matplotlib.pyplot")
        fig, ax = plt.subplots()
        ax.plot([0, 1, 2], [0, 1, 4], "r-")
        ax.set_xlabel("x")
        ax.set_title("femm-qt PyBridge")
        plt.show()

        print("window up; the client is now idle -- try zoom/pan/save")
        deadline = time.time() + args.hold if args.hold else None
        while br.eval("len(__import__('matplotlib')._pylab_helpers"
                      ".Gcf.get_all_fig_managers())"):
            time.sleep(0.25)
            if deadline and time.time() > deadline:
                break
        print("bridge still healthy after the window: 2+2 =", br.eval("2 + 2"))
    finally:
        br.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
