"""Layer 2: drive a real analysis through every binding and check the numbers.

    python3 run_layer2.py [--femm-dir DIR]

Layer 1 proves each wrapper emits a well-formed command. It cannot prove the
engine accepts it -- seven of the eight *_gridsnap commands rejected the
documented call while Layer 1 reported them perfect -- and it never sees a value
make the round trip at all.

Each scenario is a translation of one of the in-process accuracy suites, handed
to the SAME comparer and the SAME Original FEMM golden that suite uses. So a
binding is measured against Original FEMM, not against us, and there is no new
oracle to maintain. Adding a scenario is a row in SCENARIOS plus one driver per
binding.

`expect` is the number of components the in-process run compares. It is not
decoration: the comparers SKIP reference keys absent from the test file
(`if k not in tv: continue`), so a driver that died after two lines would report
"compared 2: 2 agree" and read as a pass. A short count is a failure.

Hosts that are not installed are skipped, loudly. Nothing here is fatal on a
machine with only Python.
"""
import argparse
import io
import re
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NEWLINE = chr(10)
TESTING = os.path.abspath(os.path.join(HERE, ".."))
# label, driver stem, comparer, golden, components the in-process run compares
SCENARIOS = [
    ("condprop", "l2_condprop", "CondPropCompare.py", "OF_eic_result.txt", 4),
    ("circuitprop", "l2_circuitprop", "MOCircuitPropCompare.py",
     "OF_circ_result.txt", 105),
    # A golden prefixed with @ is not a file but an in-process script: it is run
    # through femm_headless and its output becomes the reference. Needed where a
    # scenario has no Original FEMM counterpart, which is the case for anything
    # exercising complex ARGUMENTS -- no upstream suite sends one.
    ("phasecurrent", "l2_phase", "PhaseCompare.py",
     "@l2_phase_reference.py", 14),
]

# Each entry: label, driver, how to find the host, how to invoke it.
HOSTS = [
    ("pyfemm", "_pyfemm.py",
     lambda: sys.executable, lambda exe, d: [exe, "-u", d]),
    ("octavefemm", "_octave.m",
     lambda: shutil.which("matlab") or _first([
         r"C:\Program Files\MATLAB\R2025b\bin\matlab.exe"]),
     lambda exe, d: [exe, "-batch", os.path.splitext(os.path.basename(d))[0]]),
    # Same octavefemm binding, different HOST. The drivers are one file each:
    # MATLAB requires a script's local functions last, Octave requires them
    # before use, so the *_octave.m drivers avoid local functions entirely --
    # anonymous functions and inlined loops, which both hosts accept.
    ("octavefemm-oct", "_octave.m",
     lambda: shutil.which("octave-cli") or _glob(
         r"C:\Program Files\GNU Octave\Octave-*\mingw64\bin\octave-cli.exe"),
     lambda exe, d: [exe, "--no-gui", "-q", d]),
    ("scifemm", "_scifemm.sce",
     lambda: shutil.which("WScilex-cli") or _first([
         r"C:\Program Files\scilab-2026.0.1\bin\WScilex-cli.exe"]),
     lambda exe, d: [exe, "-quit", "-f", d]),
    ("juliafemm", "_juliafemm.jl",
     lambda: shutil.which("julia") or _first([
         os.path.expanduser(r"~\AppData\Local\Programs\Julia-1.12.6\bin\julia.exe")]),
     lambda exe, d: [exe, d]),
    ("mathfemm", "_mathfemm.m",
     lambda: shutil.which("math") or _first([
         r"C:\Program Files\Wolfram Research\Mathematica\9.0\math.exe"]),
     lambda exe, d: [exe, "-script", d]),
]


def _glob(pattern):
    """First match of a wildcard path -- Octave installs under a versioned
    directory, so hardcoding Octave-11.1.0 would rot."""
    import glob as _g
    hits = sorted(_g.glob(pattern))
    return hits[-1] if hits else None


def _first(paths):
    for p in paths:
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--femm-dir",
                    default=os.path.abspath(os.path.join(HERE, "..", "..",
                                                         "build", "bin")))
    args = ap.parse_args()
    if not os.path.isdir(args.femm_dir):
        print("no build at %s -- build first, or pass --femm-dir"
              % args.femm_dir)
        return 2
    env = dict(os.environ, FEMM_DIR=args.femm_dir)

    rc = 0
    for slabel, stem, comparer, golden, expect in SCENARIOS:
        # Comparers live beside the engine suites unless the scenario is
        # binding-specific, in which case they live here.
        comparer = (os.path.join(HERE, comparer)
                    if os.path.exists(os.path.join(HERE, comparer))
                    else os.path.join(TESTING, comparer))
        if golden.startswith("@"):
            ref_script = os.path.join(HERE, golden[1:])
            golden = os.path.join(HERE, "l2_%s_reference.txt" % slabel)
            engine = os.path.join(args.femm_dir, "femm_headless")
            if os.path.exists(engine + ".exe"):
                engine += ".exe"
            r = subprocess.run([engine, "-run", ref_script],
                               capture_output=True, text=True, timeout=1800)
            keep = [ln for ln in r.stdout.splitlines()
                    if not ln.startswith(("FEMM headless", "IPC:", "Executing:",
                                          "Script complete"))]
            io.open(golden, "w", encoding="utf-8",
                    newline=NEWLINE).write(NEWLINE.join(keep) + NEWLINE)
            print("(reference regenerated in-process: %d lines)" % len(keep))
        else:
            golden = os.path.join(TESTING, golden)
        print("--- %s (%s, expecting %d components) ---"
              % (slabel, os.path.basename(golden), expect))
        for label, suffix, find, argv in HOSTS:
            exe = find()
            driver = os.path.join(HERE, stem + suffix)
            if not exe:
                print("  %-11s SKIP  (host not installed)" % label)
                continue
            if not os.path.exists(driver):
                print("  %-11s SKIP  (no driver: %s)"
                      % (label, os.path.basename(driver)))
                continue
            out = os.path.join(HERE, "l2_%s_%s.txt" % (slabel, label))
            try:
                r = subprocess.run(argv(exe, driver), cwd=HERE, env=env,
                                   capture_output=True, text=True, timeout=1800)
            except subprocess.TimeoutExpired:
                print("  %-11s FAIL  (timed out)" % label)
                rc = 1
                continue
            io.open(out, "w", encoding="utf-8", newline="\n").write(r.stdout)
            cmp_ = subprocess.run([sys.executable, comparer, golden, out],
                                  capture_output=True, text=True)
            lines = (cmp_.stdout or cmp_.stderr).strip().splitlines()
            verdict = lines[0] if lines else "(no output from the comparer)"
            m = re.search(r"compared (\d+)", verdict)
            n = int(m.group(1)) if m else 0
            ok = ("0 flagged" in verdict) and n >= expect
            if m and n < expect:
                verdict += "   <-- SHORT: expected %d" % expect
            print("  %-11s %-5s %s" % (label, "ok" if ok else "FAIL", verdict))
            if not ok:
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
