# A script must run regardless of its length or its line endings.
#
#     python3 RunScriptSizeTest.py <path-to-femm_headless>
#
# Driven from outside because the bug it pins was in `-run` itself, not in
# anything reachable from inside the interpreter: exec_python_file() opened the
# file in TEXT mode and sized its buffer with ftell(), which reports the
# on-disk BYTE count while a text-mode fread returns CHARACTERS -- fewer by one
# per CRLF. The two disagreed, and a CRLF script in a particular size window
# reached the lexer damaged and died with a bare "invalid syntax" naming no
# line and no cause.
#
# The window is what made it nasty: a 380-400 statement file failed while the
# same file at 410 ran, and the identical file with LF endings ran at every
# size. So it looked like a syntax error in the user's script, and shrinking
# the script to bisect it made the symptom disappear.
#
# CRLF is the default for anything edited in Notepad, and 8 kB is a small
# script -- a generated geometry easily exceeds it.
import os
import subprocess
import sys
import tempfile


def main(argv):
    if len(argv) != 2:
        print("usage: RunScriptSizeTest.py <femm_headless>")
        return 2
    exe = argv[1]
    if not os.path.exists(exe):
        print("SKIP: no femm_headless at %s" % exe)
        return 0

    fails = []
    tmp = tempfile.mkdtemp()
    try:
        # Straddles the window that used to fail (8.0-8.5 kB after CRLF), plus
        # a small file and a much larger one so the test still means something
        # if the boundary ever moves.
        for n in (1, 50, 380, 390, 400, 405, 410, 500, 1500):
            body = "".join("x%d = ('a%d', %d)\n" % (i, i, i) for i in range(n))
            body += "print('SIZE-OK')\n"
            for label, data in (("lf", body.encode()),
                                ("crlf", body.replace("\n", "\r\n").encode())):
                path = os.path.join(tmp, "s_%s_%d.py" % (label, n))
                with open(path, "wb") as f:
                    f.write(data)
                out = subprocess.run([exe, "-run", path], capture_output=True,
                                     text=True, timeout=120)
                blob = (out.stdout or "") + (out.stderr or "")
                if "SIZE-OK" not in blob:
                    fails.append("%-4s %4d statements (%d bytes): %s"
                                 % (label, n, len(data),
                                    blob.strip().splitlines()[-1]
                                    if blob.strip() else "no output"))

        # A file with no trailing newline is the other end-of-input edge.
        path = os.path.join(tmp, "no_final_newline.py")
        with open(path, "wb") as f:
            f.write(b"print('SIZE-OK')")
        out = subprocess.run([exe, "-run", path], capture_output=True,
                             text=True, timeout=120)
        if "SIZE-OK" not in (out.stdout or "") + (out.stderr or ""):
            fails.append("a file with no trailing newline did not run")
    finally:
        for name in os.listdir(tmp):
            try:
                os.remove(os.path.join(tmp, name))
            except OSError:
                pass
        try:
            os.rmdir(tmp)
        except OSError:
            pass

    if fails:
        print("RunScriptSize FAIL (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    print("RunScriptSize OK: 9 sizes x LF/CRLF run, including the 8 kB window "
          "that used to fail, and a file with no trailing newline")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
