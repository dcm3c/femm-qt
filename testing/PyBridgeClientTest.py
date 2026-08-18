# The client half of the matplotlib bridge, from inside femm-qt.
#
#     build/bin/femm_headless -run testing/PyBridgeClientTest.py
#
# The transport is already covered by PyBridgeHostTest; what is under test here
# is pybridge.py -- the proxy, the codec, and the matplotlib import shims -- so
# this reads the way a user's script would, deliberately.
#
# Skips cleanly when no usable CPython is installed: plotting is an optional
# capability and a machine without matplotlib must not fail the suite.
import pybridge

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append(label + (": " + detail if detail else ""))


def close(a, b, tol=1e-9):
    return abs(a - b) < tol


def main():
    try:
        pybridge.start()
    except pybridge.BridgeError as e:
        print("SKIP: no usable Python for the matplotlib bridge")
        print("  " + str(e).split("\n")[0])
        return 0

    import numpy as np          # ulab, in-process -- NOT proxied
    import matplotlib
    import matplotlib.pyplot as plt

    # --- the example from the top of the design document ------------------------

    fig, ax = plt.subplots()
    check("subplots unpacks a tuple of proxies", isinstance(fig, pybridge.Proxy)
          and isinstance(ax, pybridge.Proxy))
    lines = ax.plot([0, 1, 2], [0, 1, 4], "r-")
    check("plot returns a list of one Line2D", isinstance(lines, list)
          and len(lines) == 1, str(lines))
    ax.set_xlabel("x")
    check("set_xlabel round trips", ax.get_xlabel() == "x")
    plt.show()

    # --- nothing beside the binaries may shadow a module ------------------------
    #
    # The GUI's working directory is the one holding the binaries, and the
    # working directory sits on sys.path AHEAD of python/.  So a directory
    # there whose name is a valid identifier gets imported -- as an empty
    # namespace package -- in place of the real module.  That is exactly how a
    # directory called "pybridge" (staging the CPython worker) silently
    # replaced python/pybridge.py for every GUI user, while every test here
    # passed, because -run chdirs to the script's directory instead.
    #
    # Checked structurally rather than by repeating the failure, so it also
    # covers whatever gets staged beside the binaries next.
    import sys
    import os as _os
    exedir = None
    for entry in sys.path:
        if entry.endswith("/python"):
            exedir = entry[:-len("/python")]
    check("found the binary directory", exedir is not None, str(sys.path))
    if exedir:
        risky = ("pybridge", "matplotlib", "numpy", "scipy", "os", "sys",
                 "json", "struct", "time", "re", "random", "gc", "math")
        shadowing = [n for n in _os.listdir(exedir)
                     if n in risky and _os.path.isdir(exedir + "/" + n)]
        check("no directory beside the binaries shadows a module",
              not shadowing, str(shadowing))

    # --- attributes, in both directions ----------------------------------------

    # This is the case that is silently wrong without MICROPY_PY_DELATTR_SETATTR:
    # the assignment would set a dead local attribute and never leave the process.
    ax.xaxis.labelpad = 12
    check("attribute assignment reaches CPython", ax.xaxis.labelpad == 12,
          str(ax.xaxis.labelpad))
    check("module attribute via the shim", isinstance(matplotlib.rcParams,
                                                      pybridge.Proxy))
    check("__version__ through the shim", "." in matplotlib.__version__)

    # --- ulab arrays over the binary path ---------------------------------------

    x = np.linspace(0.0, 2.0 * 3.141592653589793, 1000)
    y = np.sin(x)
    check("ulab stays local", isinstance(x, np.ndarray))
    ax.plot(x, y)

    # A slice is not dense, and ulab refuses tobytes() on it -- the codec has to
    # densify rather than let this fail out of plot().
    ax.plot(x[::2], y[::2])

    # 2-D, which is what a triangulation index array is.
    grid = np.array([[0.0, 1.0, 2.0], [3.0, 4.0, 5.0]])
    pybridge.put("grid", grid)
    check("2-D shape survives", pybridge.eval("grid.shape") == (2, 3),
          str(pybridge.eval("grid.shape")))
    check("2-D order survives", close(pybridge.eval("float(grid[1][2])"), 5.0))

    # --- fetch, and the dtypes ulab cannot hold ---------------------------------

    back = pybridge.fetch(pybridge.eval("__import__('numpy').linspace(0, 1, 500)"))
    check("fetch returns a ulab array", isinstance(back, np.ndarray), str(type(back)))
    check("fetch length", len(back) == 500, str(len(back)))
    check("fetch contents", close(float(back[250]), 250.0 / 499.0))

    # numpy's arange is int64 here; ulab has no int64, so the worker must widen it
    # rather than send something that cannot be constructed.
    ints = pybridge.fetch(pybridge.eval("__import__('numpy').arange(10)"))
    check("int64 widened to something ulab has", isinstance(ints, np.ndarray),
          str(type(ints)))
    check("widened contents", close(float(ints[7]), 7.0))

    # Complex, since it is why femm-qt cares.
    z = pybridge.fetch(pybridge.eval("__import__('numpy').array([1+2j, 3-4j])"))
    check("complex array round trip", isinstance(z, np.ndarray)
          and int(z.dtype) == int(np.complex), str(type(z)))
    check("complex contents", z[1] == 3 - 4j, str(z[1]))
    check("complex scalar", pybridge.eval("3+4j") == 3 + 4j)

    # --- round trip of a femm-flavoured workload --------------------------------

    sweep = [complex(i, -0.5 * i) for i in range(400)]     # a frequency sweep
    pybridge.put("sweep", sweep)
    check("bulk complex list took the binary path",
          pybridge.eval("str(sweep.dtype)") == "complex128",
          pybridge.eval("str(sweep.dtype)"))
    mags = pybridge.fetch(pybridge.eval("__import__('numpy').abs(sweep)"))
    check("magnitudes came back", close(float(mags[10]), abs(complex(10, -5))))

    # --- containers, iteration, operators ---------------------------------------

    d = pybridge.eval("{'a': 1, 'b': 2}")
    check("dict is a handle", isinstance(d, pybridge.Proxy))
    check("subscript", d["a"] == 1)
    d["c"] = 3
    check("subscript assignment", len(d) == 3)
    check("contains", "b" in d)
    check("iteration", sorted(list(pybridge.eval("[3, 1, 2]"))) == [1, 2, 3])

    frac = pybridge.import_module("fractions")
    half = frac.Fraction(1, 2)
    check("forward operator", str(half + half) == "1")
    check("reverse operator", str(2 * half) == "1", str(2 * half))
    check("unary operator", str(-half) == "-1/2")
    check("comparison", half < 1)

    # --- errors read like Python ------------------------------------------------

    try:
        pybridge.eval("1/0")
        check("remote ZeroDivisionError", False)
    except ZeroDivisionError:
        pass
    try:
        plt.no_such_function()
        check("remote AttributeError", False)
    except AttributeError:
        pass
    check("stream survives errors", pybridge.eval("2 + 2") == 4)

    # --- a real figure, rendered ------------------------------------------------

    import os
    out = os.getcwd() + "/_pybridge_test.png"
    if os.path.exists(out):
        os.remove(out)
    fig.savefig(out)
    check("savefig produced a file", os.path.exists(out))
    if os.path.exists(out):
        check("png is plausible", os.stat(out)[6] > 5000, str(os.stat(out)[6]))
        os.remove(out)
    plt.close(fig)

    # --- the xlsxwriter shim, and the process boundary it does NOT hide ---------
    #
    # Shimmed for the same reason matplotlib is and numpy is not: there is no
    # in-process xlsxwriter to shadow, so forwarding costs nothing. What the
    # shim cannot make transparent is a file HANDLE, which belongs to the
    # process that opened it -- and the error a user meets there has to name
    # the remedy that fits, since that is the moment they learn the boundary
    # exists at all.
    try:
        import xlsxwriter
        from xlsxwriter import Workbook          # via module __getattr__
        check("xlsxwriter version through the shim", "." in xlsxwriter.__version__,
              str(xlsxwriter.__version__))
        xlsx = "_pybridge_shim_test.xlsx"
        wb = Workbook(xlsx)                      # a PATH: the worker opens it
        ws = wb.add_worksheet("t")
        ws.write_row(0, 0, ["a", "b"], wb.add_format({"bold": True}))
        ws.write_column(1, 0, [1, 2, 3])
        wb.close()
        check("workbook written by path", os.path.exists(xlsx)
              and os.stat(xlsx)[6] > 4000)

        # A local file object must be refused, and told what to do instead.
        lf = open("_pybridge_shim_local.xlsx", "wb")
        try:
            Workbook(lf)
            check("local file object should not cross", False)
        except pybridge.BridgeError as e:
            check("file error names pybridge.open", "pybridge.open" in str(e),
                  str(e))
        lf.close()

        # ...and pybridge.open() is that way through.
        rf = pybridge.open("_pybridge_shim_remote.xlsx", "wb")
        wb = Workbook(rf)
        wb.add_worksheet()
        wb.close()
        rf.close()
        check("workbook written through pybridge.open",
              os.stat("_pybridge_shim_remote.xlsx")[6] > 4000)

        for junk in (xlsx, "_pybridge_shim_local.xlsx",
                     "_pybridge_shim_remote.xlsx"):
            try:
                os.remove(junk)
            except Exception:
                pass
    except ImportError:
        check("xlsxwriter shim is importable", False)
    except pybridge.BridgeError as e:
        # No xlsxwriter in the worker's interpreter is not a femm-qt failure.
        if "xlsxwriter" not in str(e):
            raise
        print("  (skipped xlsxwriter: not installed in the worker)")

    # --- the zipfile shim ------------------------------------------------------
    #
    # Shimmed rather than written: a useful zip needs DEFLATE, which would mean
    # vendoring moddeflate.c and regenerating. The boundary barely bites here
    # because zipfile's API is mostly paths, and both processes see the same
    # filesystem -- which is the test below.
    try:
        import zipfile
        from zipfile import ZIP_DEFLATED          # via module __getattr__
        zpath = "_pybridge_shim.zip"
        src = "_pybridge_shim_src.txt"
        with open(src, "w") as f:
            f.write("compressible " * 500)
        with zipfile.ZipFile(zpath, "w", ZIP_DEFLATED) as z:
            z.write(src, "data.txt")              # a PATH: the worker opens it
            z.writestr("notes.txt", "swept 0..45")
        check("zip was written and is smaller than its input",
              0 < os.stat(zpath)[6] < os.stat(src)[6])
        with zipfile.ZipFile(zpath) as z:
            check("zip lists both members", sorted(z.namelist()),
                  ["data.txt", "notes.txt"])
            check("zip round trips bytes", z.read("notes.txt"), b"swept 0..45")
            check("zip is not corrupt", z.testzip() is None)
        for junk in (zpath, src):
            try:
                os.remove(junk)
            except Exception:
                pass
    except ImportError as e:
        # No zipfile in the worker's interpreter would be extraordinary (it is
        # stdlib), so this is a real failure rather than a skip.
        check("zipfile shim is importable", False, str(e))

    # A callable gets the OTHER remedy, which is what made the single shared
    # hint wrong: it advised pybridge.exec() for files too.
    try:
        pybridge.eval("sorted")([3, 1, 2], key=lambda v: -v)
        check("a local callable should not cross", False)
    except pybridge.BridgeError as e:
        check("callable error names pybridge.exec", "pybridge.exec" in str(e),
              str(e))

    # --- lifecycle --------------------------------------------------------------

    stale = pybridge.eval("list(range(100))")   # >64, so a handle not a value
    pybridge.restart()
    try:
        len(stale)
        check("stale proxy is refused", False)
    except pybridge.BridgeError as e:
        check("stale proxy message", "restarted" in str(e), str(e))
    check("bridge works again after restart", pybridge.eval("6 * 7") == 42)
    pybridge.shutdown()

    # --- report -----------------------------------------------------------------

    if fails:
        print("FAIL (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    print("PyBridgeClient OK")
    return 0


if main():
    raise SystemExit(1)   # a real failure: let the harness see a nonzero exit
