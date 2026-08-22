# print() must reach the output sink, not a file descriptor.
#
#     python3 ConsolelessPrintTest.py <path to femm_gui>
#
# Every other row in _verify_all.sh drives a binary from a shell, so the child
# always inherits a valid descriptor 1 and any output at all looks like success.
# A GUI launched from Explorer, the Start menu or the script toolbar has no such
# thing: femm_gui is a GUI-subsystem program, so there is no console and no
# standard handle behind it.  Script output has exactly one legitimate route out
# of the interpreter -- mp_hal_stdout_tx_strn in py_state.cpp, which hands the
# bytes to whatever sink is installed (the console widget in the GUI) -- and
# anything that instead reaches for a descriptor fails with EBADF.
#
# That is not hypothetical.  MICROPY_PY_SYS_STDFILES was turned on to give
# scripts the NAMES sys.stdout/sys.stderr, and the flag quietly also made
# mp_sys_stdout_obj the default `file` for print() and the target of
# MP_PYTHON_PRINTER.  Two vendored files define that symbol -- one on mp_hal,
# one on a raw fd -- and the raw-fd one won the link, so from e331d58 until this
# test existed, printing ANYTHING from a GUI-launched script raised
# "[Errno 9] EBADF" and no suite row could see it.
#
# So the child is started with its standard handles closed, which is the one
# condition that tells the two routes apart.
import os
import subprocess
import sys
import tempfile
import time

GUI = sys.argv[1] if len(sys.argv) > 1 else "build/bin/femm_gui.exe"

# Each entry writes through a different path into the interpreter's output.
# _builtin_print is MicroPython's own; print is femm-qt's override; the sys.*
# ones are what the stdfiles flag was added for; logging binds sys.stderr at
# import time, which is why it could not be vendored before the flag.
SCRIPT = '''
import sys
res = []
def t(name, fn):
    try:
        fn()
        res.append(name + ": ok")
    except BaseException as e:
        res.append(name + ": FAILED " + repr(e))

t("_builtin_print", lambda: _builtin_print("a"))
t("print", lambda: print("b", 1, 2.5))
t("print(end=)", lambda: print("c", end="!"))
t("sys.stdout.write", lambda: sys.stdout.write("d"))
t("sys.stderr.write", lambda: sys.stderr.write("e"))
t("print(file=stderr)", lambda: print("f", file=sys.stderr))

def _logging():
    import logging
    logging.basicConfig(level=logging.INFO)
    logging.getLogger("probe").info("g")
t("logging", _logging)

# Not a print path, but it pins WHICH definition linked: the raw-fd object is a
# seekable file, the mp_hal one is not.  Without this a future build could go
# back to fd 1 and still pass every check above wherever fd 1 happens to work.
t("stdout is not a file",
  lambda: 1 / (0 if hasattr(sys.stdout, "seek") else 1))

f = _builtin_open(r"{d}/result.txt", "w")
f.write(chr(10).join(res) + chr(10))
f.close()
'''


def spawn_without_stdio(argv):
    """Start argv with no usable standard handles -- what Explorer gives a
    GUI-subsystem program, and what no shell can give it."""
    if os.name == "nt":
        # Ask CreateProcess to USE the std handles in STARTUPINFO, and leave
        # them NULL.  subprocess only overwrites these when stdout=/stderr= are
        # passed, so with none passed our settings survive into the child.
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESTDHANDLES
        si.hStdInput = si.hStdOutput = si.hStdError = None
        return subprocess.Popen(argv, startupinfo=si, close_fds=True)
    return subprocess.Popen(argv, close_fds=True,
                            preexec_fn=lambda: [os.close(fd) for fd in (0, 1, 2)])


def main():
    d = tempfile.mkdtemp(prefix="consoleless_print_")
    path = os.path.join(d, "probe.py")
    with open(path, "w") as f:
        f.write(SCRIPT.format(d=d.replace("\\", "/")))

    proc = spawn_without_stdio([GUI, "-run", path])
    try:
        result = os.path.join(d, "result.txt")
        deadline = time.time() + 90
        while time.time() < deadline and not os.path.exists(result):
            time.sleep(0.1)
        if not os.path.exists(result):
            print("ConsolelessPrint FAIL: the script never reported "
                  "(the GUI may have died before it could write)")
            return 1
        time.sleep(0.05)
        with open(result) as f:
            lines = [l for l in f.read().splitlines() if l]
    finally:
        if proc.poll() is None:
            proc.kill()
        try:
            proc.wait(10)
        except Exception:
            pass

    bad = [l for l in lines if not l.endswith(": ok")]
    if bad or len(lines) != 8:
        print("ConsolelessPrint FAIL")
        for l in (bad or lines):
            print("  " + l)
        return 1
    print("ConsolelessPrint OK: all %d output paths work with no console "
          "(print, sys.stdout/stderr, logging), and sys.stdout is the "
          "sink-backed object" % len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
