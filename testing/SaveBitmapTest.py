"""Does *_savebitmap tell you when it did not write the file?

    python3 SaveBitmapTest.py [path-to-femm_gui]

NOT part of _verify_all.sh, and cannot be: savebitmap is a VIEW command, and
set_view_command_callback is registered only in gui/main.cpp. Under
femm_headless the callback is null, the binding returns without doing
anything, and the script is told nothing -- which is one of the three silent
failures this checks for.

Reported from the macOS pass: mo_savebitmap("/nonexistent_dir/x.png") raised
no error, returned None, and wrote nothing. gui/main.cpp ignores the result of
pix.save(). A script saving into a folder that does not exist cannot find out.

Three places the answer can be swallowed, and all three are checked, because
fixing only the first would change nothing a script can see:

  1. gui/main.cpp discards pix.save()'s bool.
  2. the bindings discard the callback's return -- py_{mag,elec,heat,curr}_
     postproc.cpp and py_editor_common.h call cb(...) and drop the result, so
     even an error string from the GUI would not reach Python.
  3. headless has no callback at all.

Eight commands from five code sites: mo_/eo_/ho_/co_ are written out one per
physics, and mi_/ei_/hi_/ci_ come from one template in py_editor_common.h.
Both halves are exercised -- a fix applied to the postprocessors alone would
leave the preprocessors silent.
"""
import io
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "pyfemm"))

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

# physics, fixture, preprocessor prefix, postprocessor prefix
CASES = [
    ("magnetics",     "PMTest.fem",         "mi", "mo"),
    ("electrostatic", "electrostatic1.fee", "ei", "eo"),
    ("heat flow",     "heatFlow1.feh",      "hi", "ho"),
    ("current flow",  "currentFlow3.fec",   "ci", "co"),
]

fails = []


def check(label, ok, detail=""):
    # The detail says why it FAILED, so it prints only then. Showing it on a
    # passing row produced "ok ... -- not PNG", which is nonsense and
    # undermines every other line in the report.
    print("  %-5s %s%s" % ("ok" if ok else "FAIL", label,
                           ("  -- " + detail) if (detail and not ok) else ""))
    if not ok:
        fails.append(label)


def is_png(path):
    try:
        return io.open(path, "rb").read(8) == PNG_MAGIC
    except OSError:
        return False


def probe(femm, cmd, path):
    """Call one savebitmap. Returns (raised, detail)."""
    try:
        getattr(femm, cmd)(path)
        return False, ""
    except Exception as e:                      # noqa: BLE001 - any is fine
        return True, str(e).strip().splitlines()[0][:90]


def run_gui(exe, tmp):
    import femm
    os.environ["FEMM_DIR"] = os.path.dirname(exe)
    femm.openfemm(0)                            # 0 = GUI; savebitmap needs one
    try:
        for phys, fixture, pre, post in CASES:
            print("\n%s (%s)" % (phys, fixture))
            femm.opendocument(os.path.join(HERE, fixture).replace("\\", "/"))
            work = os.path.join(tmp, phys.replace(" ", "_"))
            os.makedirs(work, exist_ok=True)

            # --- preprocessor view, before solving ---------------------
            good = os.path.join(work, pre + "_good.png").replace("\\", "/")
            raised, why = probe(femm, pre + "_savebitmap", good)
            check("%s_savebitmap writes a readable PNG" % pre,
                  not raised and is_png(good),
                  why or ("no file" if not os.path.exists(good) else "not PNG"))

            bad = os.path.join(work, "no_such_dir", "x.png").replace("\\", "/")
            raised, why = probe(femm, pre + "_savebitmap", bad)
            check("%s_savebitmap REPORTS a failed write" % pre, raised,
                  "returned quietly and wrote nothing")

            # --- solve, then the postprocessor view --------------------
            getattr(femm, pre + "_saveas")(
                os.path.join(work, "temp" + os.path.splitext(fixture)[1])
                .replace("\\", "/"))
            getattr(femm, pre + "_analyze")()
            getattr(femm, pre + "_loadsolution")()

            good = os.path.join(work, post + "_good.png").replace("\\", "/")
            raised, why = probe(femm, post + "_savebitmap", good)
            check("%s_savebitmap writes a readable PNG" % post,
                  not raised and is_png(good),
                  why or ("no file" if not os.path.exists(good) else "not PNG"))

            bad = os.path.join(work, "no_such_dir", "x.png").replace("\\", "/")
            raised, why = probe(femm, post + "_savebitmap", bad)
            check("%s_savebitmap REPORTS a failed write" % post, raised,
                  "returned quietly and wrote nothing")

            # A .bmp NAME gets PNG bytes -- deliberate, and the manuals now
            # say .png because of it. Pinned so that if the format selection
            # ever changes, the documentation stops being wrong silently.
            bmp = os.path.join(work, post + "_named.bmp").replace("\\", "/")
            probe(femm, post + "_savebitmap", bmp)
            check("%s_savebitmap on a .bmp name still writes PNG bytes" % post,
                  is_png(bmp), "extension changed behaviour")

            getattr(femm, post + "_close")()
    finally:
        try:
            femm.closefemm()
        except Exception:
            pass


def run_headless(exe_headless, tmp):
    """Headless has no view callback, so savebitmap can do nothing. It should
    at least SAY so rather than returning as though it worked."""
    print("\nheadless")
    out = os.path.join(tmp, "headless.png").replace("\\", "/")
    script = os.path.join(tmp, "hb.py")
    io.open(script, "w", encoding="utf-8", newline="\n").write(
        "import femm\n"
        "femm.opendocument(%r)\n"
        "try:\n"
        "    femm.mi_savebitmap(%r)\n"
        "    print('RESULT no-error')\n"
        "except Exception as e:\n"
        "    print('RESULT error ' + str(e).splitlines()[0])\n"
        % (os.path.join(HERE, "PMTest.fem").replace("\\", "/"), out))
    r = subprocess.run([exe_headless, "-run", script.replace("\\", "/")],
                       capture_output=True, text=True, timeout=600)
    line = next((l for l in r.stdout.splitlines() if l.startswith("RESULT")),
                "RESULT (no output)")
    wrote = os.path.exists(out)
    # startswith, NOT ("error" in line): the success marker is
    # "RESULT no-error", which CONTAINS "error", so the substring test was
    # satisfied by the very output it was meant to catch and could never fail.
    raised = line.startswith("RESULT error")
    check("headless savebitmap does not claim success while doing nothing",
          raised or wrote,
          "reported %r and wrote no file" % line[len("RESULT "):])


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        ROOT, "build", "bin", "femm_gui" + (".exe" if os.name == "nt" else ""))
    if not os.path.exists(exe):
        print("no femm_gui at %s -- build first, or pass the path" % exe)
        return 2
    headless = exe.replace("femm_gui", "femm_headless")

    tmp = tempfile.mkdtemp(prefix="savebitmap_")
    print("savebitmap check   (scratch: %s)" % tmp)
    try:
        run_gui(exe, tmp)
        if os.path.exists(headless):
            run_headless(headless, tmp)
    finally:
        pass                                    # leave the scratch for reading

    print()
    if fails:
        print("savebitmap check FAILED: %d of them" % len(fails))
        for f in fails:
            print("    " + f)
        return 1
    print("savebitmap check: every save either wrote a readable PNG or said "
          "why not, in all four physics, pre and post")
    return 0


if __name__ == "__main__":
    sys.exit(main())
