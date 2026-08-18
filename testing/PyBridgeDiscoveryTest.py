# Discovery has to satisfy two goals that pull against each other, so both are
# pinned here.
#
#     python3 PyBridgeDiscoveryTest.py <path to femm_headless>
#
#   1. A Python that CAN plot must win.  That is the common case and the whole
#      reason discovery exists.
#   2. A Python that CANNOT plot must still be usable.  The bridge is a general
#      package proxy -- csv, xlsxwriter, scipy, sympy need no figure window --
#      and it originally refused such an interpreter outright, which denied
#      every non-plotting use to anyone without matplotlib installed.
#
# A throwaway venv with no matplotlib provides case 2; it is built here rather
# than assumed, and skipped if venv creation is unavailable.
import os
import subprocess
import sys
import tempfile

HEADLESS = sys.argv[1] if len(sys.argv) > 1 else "build/bin/femm_headless.exe"
IS_WIN = os.name == "nt"

PROBE = '''
import pybridge
try:
    pybridge.start()
except pybridge.BridgeError as e:
    print("RESULT refused")
    raise SystemExit(0)
info = pybridge.info()
print("RESULT ok")
print("INTERP %s" % pybridge.interpreter())
print("BACKEND %s" % info.get("backend"))
try:
    pybridge.import_module("csv")
    print("CSV ok")
except Exception as e:
    print("CSV %s" % e)
try:
    pybridge.import_module("matplotlib.pyplot")
    print("PLOT ok")
except Exception as e:
    print("PLOT %s: %s" % (type(e).__name__, e))
pybridge.shutdown()
'''


def run(script_dir, env_extra):
    path = os.path.join(script_dir, "probe.py")
    with open(path, "w") as f:
        f.write(PROBE)
    env = dict(os.environ)
    env.update(env_extra)
    out = subprocess.run([HEADLESS, "-run", path], capture_output=True,
                         text=True, env=env, timeout=180).stdout
    got = {}
    for line in out.splitlines():
        if " " in line:
            k, v = line.split(" ", 1)
            got[k] = v
    return got


def venv_without_matplotlib(d):
    """A Python that answers but cannot draw. None if it cannot be built."""
    root = os.path.join(d, "venv")
    try:
        subprocess.run([sys.executable, "-m", "venv", root], check=True,
                       capture_output=True, timeout=300)
    except Exception:
        return None
    exe = os.path.join(root, "Scripts" if IS_WIN else "bin",
                       "python.exe" if IS_WIN else "python")
    return exe if os.path.exists(exe) else None


def main():
    d = tempfile.mkdtemp(prefix="pybridge_disc_")

    # Is there a plotting-capable Python at all?  If not, the whole suite is
    # skipping anyway.
    base = run(d, {})
    if base.get("RESULT") != "ok" or not base.get("BACKEND") \
            or base.get("BACKEND") == "None":
        print("PyBridgeDiscovery SKIP (no plotting-capable Python)")
        return 0

    plotless = venv_without_matplotlib(d)
    if plotless is None:
        print("PyBridgeDiscovery SKIP (cannot build a venv)")
        return 0

    problems = []

    # 1. The plotless interpreter is offered FIRST and must be passed over.
    got = run(d, {"FEMMQT_PYTHON": plotless})
    if got.get("RESULT") != "ok":
        problems.append("discovery refused everything when offered a "
                        "plotless python first")
    elif got.get("BACKEND") in (None, "None"):
        problems.append("discovery settled for a python that cannot plot "
                        "while a capable one existed")
    elif got.get("PLOT") != "ok":
        problems.append("chosen python cannot plot: %s" % got.get("PLOT"))

    # 2. Forced to the plotless one, it must still work for everything else.
    forced = PROBE.replace("import pybridge\n",
                           "import pybridge\npybridge.set_interpreter(%r)\n"
                           % plotless.replace("\\", "/"))
    path = os.path.join(d, "probe.py")
    with open(path, "w") as f:
        f.write(forced)
    out = subprocess.run([HEADLESS, "-run", path], capture_output=True,
                         text=True, timeout=180).stdout
    got = dict(l.split(" ", 1) for l in out.splitlines() if " " in l)
    if got.get("RESULT") != "ok":
        problems.append("a python without matplotlib was refused outright; "
                        "the bridge is not only for plotting")
    if got.get("CSV") != "ok":
        problems.append("csv unusable on a plotless python: %s"
                        % got.get("CSV"))
    plot = got.get("PLOT", "")
    if plot == "ok":
        problems.append("plotting reported success without matplotlib")
    elif "matplotlib" not in plot:
        problems.append("plotting failure does not name matplotlib: %r" % plot)

    if problems:
        print("PyBridgeDiscovery FAIL")
        for p in problems:
            print("  " + p)
        return 1
    print("PyBridgeDiscovery OK: prefers a python that can plot, still uses "
          "one that cannot")
    return 0


if __name__ == "__main__":
    sys.exit(main())
