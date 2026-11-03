# OpenResultsTest.py -- open() of a solution file, and the error a
# postprocessor command gives before any solution exists (CPython; drives
# femm_headless).
#
# A femm group report (27 Oct 2026): a user porting a pyfemm script opened its
# .ans files with opendocument(), as FEMM 4.2 allowed, then called
# mo_getcircuitproperties() and got "name 'mo_getcircuitproperties' isn't
# defined". Two defects behind that:
#
#   1. The engine's open() only knew the four input types; a solution file
#      threw "unsupported file type" from opendocument(), and a bare
#      open("x.ans") did not even get that far -- the init script's shim
#      routed only input extensions to FEMM and handed everything else to
#      Python's file open(), which returned a file handle without a word.
#   2. The postprocessor (and editor) commands were not defined until the
#      first document of that kind existed, so with nothing open the name
#      itself was missing instead of the command failing with "No magnetics
#      output in focus".
#
# Checks:
#   a. In a fresh engine, one command of each physics and side fails with
#      an "... in focus" error, never a NameError.
#   b. After analysing a model and closing its editor, open("x.ans") and
#      opendocument("x.ans") both make the magnetics postprocessor commands
#      work (the second is the same-file reload path), the solution values
#      are right, and open("x.ans", "r") is still Python's file open.
#   c. The same for a heat-flow solution through open("x.anh"), an
#      electrostatic one through open("x.res") and a current-flow one
#      through open("x.anc"): all four solution types.
#
# Usage:  python3 OpenResultsTest.py [path-to-femm_headless]
# Runs from testing/; writes and removes ort_tmp* scratch files.
# Exits nonzero on any violation.

import os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
def _default_engine(name):
    # Windows and Linux leave the engine in build/bin. macos_build.sh keeps
    # only the copy inside the app bundle, where the solvers sit beside it.
    b = os.path.join(HERE, "..", "build", "bin")
    loose = os.path.join(b, name + (".exe" if os.name == "nt" else ""))
    bundled = os.path.join(b, "femm_gui.app", "Contents", "MacOS", name)
    return bundled if not os.path.exists(loose) and os.path.exists(bundled) else loose


BIN = sys.argv[1] if len(sys.argv) > 1 else _default_engine("femm_headless")
FIXTURE = os.path.join(HERE, "myTestProblem.fem")
BASE = os.path.join(HERE, "ort_tmp").replace("\\", "/")

FRESH = r'''
for cmd in ("mo_getcircuitproperties('icoil')", "eo_getpointvalues(0, 0)",
            "ho_getpointvalues(0, 0)", "co_getpointvalues(0, 0)",
            "mi_getmaterial('Air')", "hi_getmaterial('Air')"):
    try:
        eval(cmd)
        print("NOERR", cmd)
    except Exception as e:
        print("ERR", cmd.split("(")[0], str(e))
'''

OPEN = r'''
D = "%s"
open(D + ".fem")
mi_analyze()
mi_close()
open(D + ".ans")
i, v, f = mo_getcircuitproperties("icoil")
print("R1", i, f)
opendocument(D + ".ans")
i, v, f = mo_getcircuitproperties("icoil")
print("R2", i, f)
fh = open(D + ".ans", "r")
print("R3", fh.readline().strip())
fh.close()
mo_close()

# A relative name must be recorded absolute (the GUI puts it in Recent Files,
# and mo_reload re-reads it after the script may have changed directory).
setcurrentdirectory(D.rsplit("/", 1)[0])
open(D.rsplit("/", 1)[1] + ".ans")
print("R7", mo_gettitle())
mo_close()

newdocument(2)
hi_probdef("centimeters", "planar", 1e-8, 100)
hi_addmaterial("NoGen", 200, 200, 0, 3)
hi_addboundprop("Hot", 0, 400, 0, 0, 0, 0, 0)
hi_addboundprop("Cold", 0, 300, 0, 0, 0, 0, 0)
hi_drawrectangle(-10, -1, 10, 1)
hi_addblocklabel(0, 0); hi_selectlabel(0, 0); hi_setblockprop("NoGen"); hi_clearselected()
hi_selectsegment(-10, 0); hi_setsegmentprop("Hot", 0, 1, 0, 0); hi_clearselected()
hi_selectsegment(10, 0); hi_setsegmentprop("Cold", 0, 1, 0, 0); hi_clearselected()
hi_saveas(D + ".feh")
hi_analyze()
hi_close()
open(D + ".anh")
print("R4", ho_getpointvalues(3, 0)[0])
ho_close()

newdocument(1)
ei_probdef("centimeters", "planar", 1e-8, 100, 30)
ei_addmaterial("Diel", 1, 1, 0)
ei_addboundprop("V10", 10, 0, 0, 0, 0)
ei_addboundprop("V0", 0, 0, 0, 0, 0)
ei_drawrectangle(-10, -1, 10, 1)
ei_addblocklabel(0, 0); ei_selectlabel(0, 0); ei_setblockprop("Diel", 1, 0, 0); ei_clearselected()
ei_selectsegment(-10, 0); ei_setsegmentprop("V10", 0, 1, 0, 0); ei_clearselected()
ei_selectsegment(10, 0); ei_setsegmentprop("V0", 0, 1, 0, 0); ei_clearselected()
ei_saveas(D + ".fee")
ei_analyze()
ei_close()
open(D + ".res")
print("R5", eo_getpointvalues(3, 0)[0])
eo_close()

newdocument(3)
ci_probdef("centimeters", "planar", 0, 1e-8, 100, 30)
ci_addmaterial("Cond", 1, 1, 1, 1, 0, 0)
ci_addboundprop("V10", 10, 0, 0, 0, 0)
ci_addboundprop("V0", 0, 0, 0, 0, 0)
ci_drawrectangle(-10, -1, 10, 1)
ci_addblocklabel(0, 0); ci_selectlabel(0, 0); ci_setblockprop("Cond", 1, 0, 0); ci_clearselected()
ci_selectsegment(-10, 0); ci_setsegmentprop("V10", 0, 1, 0, 0); ci_clearselected()
ci_selectsegment(10, 0); ci_setsegmentprop("V0", 0, 1, 0, 0); ci_clearselected()
ci_saveas(D + ".fec")
ci_analyze()
ci_close()
open(D + ".anc")
v = co_getpointvalues(3, 0)[0]
print("R6", v.real if isinstance(v, complex) else v)
co_close()
''' % BASE


def run(script):
    with open(BASE + ".py", "w") as f:
        f.write(script)
    r = subprocess.run([BIN, "-run", BASE + ".py"], capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def main():
    fails = []
    try:
        shutil.copyfile(FIXTURE, BASE + ".fem")

        rc, out, err = run(FRESH)
        errs = [ln for ln in out.splitlines() if ln.startswith("ERR") or ln.startswith("NOERR")]
        if rc != 0 or len(errs) != 6:
            fails.append("fresh engine: exit %d, %d of 6 results\n%s%s" % (rc, len(errs), out, err))
        for ln in errs:
            name = ln.split()[1]
            if ln.startswith("NOERR") or "defined" in ln or "in focus" not in ln:
                fails.append("fresh engine: %s" % ln)
            else:
                print("  no document: %s -> %s" % (name, ln.split(None, 2)[2]))

        rc, out, err = run(OPEN)
        got = {}
        for ln in out.splitlines():
            v = ln.split()
            if v and v[0] in ("R1", "R2", "R3", "R4", "R5", "R6", "R7"):
                got[v[0]] = v[1:]
        if rc != 0 or set(got) != {"R1", "R2", "R3", "R4", "R5", "R6", "R7"}:
            fails.append("open: exit %d, results %s\n%s%s" % (rc, sorted(got), out, err))
        else:
            for tag, what in (("R1", "open(.ans)"), ("R2", "opendocument(.ans), same file again")):
                i, f = float(got[tag][0]), float(got[tag][1])
                print("  %s: current %g A, flux linkage %g Wb" % (what, i, f))
                if abs(i - 20.0) > 1e-9 or not (0.1 < f < 0.3):
                    fails.append("%s: circuit properties %g, %g" % (what, i, f))
            title = " ".join(got["R7"])
            print("  open('ort_tmp.ans') by bare name records: %s" % title)
            if not (os.path.isabs(title) and title.lower().endswith("ort_tmp.ans")):
                fails.append("a relative open() recorded a relative path: %r" % title)
            first = " ".join(got["R3"])
            print("  open(.ans, 'r'): Python file, first line %r" % first)
            if not first.startswith("[Format]"):
                fails.append("open(.ans, 'r') did not read the file: %r" % first)
            t = float(got["R4"][0])
            print("  open(.anh): T(3,0) = %g K (335 expected)" % t)
            if abs(t - 335) > 0.05:
                fails.append("open(.anh): T(3,0) = %g, want 335" % t)
            for tag, what in (("R5", "open(.res)"), ("R6", "open(.anc)")):
                v = float(got[tag][0])
                print("  %s: V(3,0) = %g V (3.5 expected)" % (what, v))
                if abs(v - 3.5) > 0.005:
                    fails.append("%s: V(3,0) = %g, want 3.5" % (what, v))
    finally:
        for f in os.listdir(HERE):
            if f.startswith("ort_tmp"):
                try:
                    os.remove(os.path.join(HERE, f))
                except OSError:
                    pass
    if fails:
        for f in fails:
            print("  VIOLATION: " + f)
        print("OPEN RESULTS CHECK FAILED: %d violation(s)" % len(fails))
        sys.exit(1)
    print("open results check: commands exist before any document and say so; open()/opendocument() of .ans, .res, .anh and .anc load the postprocessor; open(.ans, 'r') is still a file")


if __name__ == "__main__":
    main()
