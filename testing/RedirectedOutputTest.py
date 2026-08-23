# Script output redirected to a file must arrive with every line intact.
#
#     python3 RedirectedOutputTest.py <path-to-femm_headless>
#
# femm_headless writes two things to two places: script output (every print(),
# through mp_hal_stdout_tx_strn) goes to stdout, and status -- the banner,
# "Executing:", "Script complete, exiting." -- goes to stderr. Redirecting both
# into one file is ordinary use:
#
#     femm_headless -run myscript.py > results.txt 2>&1
#
# C leaves stderr unbuffered and block-buffers stdout when it is not a terminal,
# so the two are written on entirely different schedules. Before the setvbuf in
# ipc/headless_main.cpp, stdout's 4 kB buffer would fill mid-line and flush
# there, and the status line printed between that flush and the one at exit
# landed in the hole:
#
#     torque = 0.38579Script complete, exiting.
#     47595 0
#
# Found on Ubuntu in MOGapIntegralTest.py, where it read as a torque of 47595
# instead of 0.3858. Nothing had computed anything wrong -- all 18 suites were
# bit-identical to Windows -- the transport had cut a line in half. Silent: no
# error, no exit code, nothing but a truncated number in the middle of a
# results file. Anyone piping a run into a .csv would have got the same.
#
# Driven from outside because that is the only place the bug exists. In-process
# tests cannot see it (they never leave the interpreter), and subprocess's
# capture_output=True cannot either -- that gives stdout and stderr SEPARATE
# pipes, so there is nothing to interleave. It takes one descriptor, shared, as
# a shell's 2>&1 gives it. That is what stderr=STDOUT does below.
#
# PLATFORM NOTE: this passes trivially on Windows, which buffers stderr too and
# so flushes it as one block at exit where it cannot land mid-line. It is a
# POSIX guard. To see it fail, run it against a femm_headless built before the
# setvbuf -- which is how it was verified, on the Ubuntu VM that reported the
# original symptom.
#
# The cases are sized so that a 4 kB boundary necessarily falls mid-line (no
# line width divides 4096), and the last one is large enough to force a flush
# even where stdio picks a much bigger buffer than 4 kB from st_blksize.
import os
import subprocess
import sys
import tempfile

STATUS = ("FEMM headless", "IPC:", "Executing:", "Script complete",
          "Script error", "Interrupted.")

# (line width including the newline, number of lines)
CASES = ((37, 200), (51, 200), (97, 300), (53, 6000))


def line(i, w):
    """A line that carries its own index at BOTH ends.

    The observed corruption kept a line's tail and lost its head, so a marker
    only at the start would have been thrown away with the damage. With the
    index at both ends, any surviving fragment still says which line it came
    from and what is missing.
    """
    head, tail = "L%06d" % i, "%06d" % i
    return head + "-" * (w - 1 - len(head) - len(tail)) + tail


def run_case(exe, tmp, w, n, fails):
    script = os.path.join(tmp, "emit_%d_%d.py" % (w, n))
    with open(script, "w") as f:
        f.write("for i in range(%d):\n"
                "    head = 'L%%06d' %% i\n"
                "    tail = '%%06d' %% i\n"
                "    print(head + '-' * (%d - 1 - len(head) - len(tail)) + tail)\n"
                % (n, w))

    out = os.path.join(tmp, "merged_%d_%d.txt" % (w, n))
    with open(out, "wb") as f:
        # One descriptor for both streams, exactly as `> file 2>&1` does it.
        rc = subprocess.call([exe, "-run", script], stdout=f,
                             stderr=subprocess.STDOUT)
    if rc != 0:
        fails.append("w=%d n=%d: exit code %d" % (w, n, rc))
        return 0

    with open(out, "rb") as f:
        blob = f.read().decode("utf-8", "replace")
    # The Windows CRT translates \n to \r\n on the way out; that is the
    # transport doing its job, not damage.
    got = [ln.rstrip("\r") for ln in blob.split("\n")]

    body, spliced = [], []
    for ln in got:
        if not ln:
            continue
        at = [ln.find(m) for m in STATUS]
        at = [i for i in at if i >= 0]
        if at and min(at) == 0:
            continue                      # a status line, all by itself: fine
        if at:
            spliced.append(ln)            # a status line welded into output
            continue
        body.append(ln)

    want = [line(i, w) for i in range(n)]
    if spliced:
        fails.append("w=%d n=%d: %d line(s) spliced with a status message, "
                     "first: %r" % (w, n, len(spliced), spliced[0][:80]))
    elif body != want:
        if len(body) != len(want):
            fails.append("w=%d n=%d: %d lines of output, expected %d"
                         % (w, n, len(body), len(want)))
        for i, (a, b) in enumerate(zip(body, want)):
            if a != b:
                fails.append("w=%d n=%d: line %d is %r, expected %r"
                             % (w, n, i, a[:60], b[:60]))
                break
    return w * n


def main(argv):
    if len(argv) != 2:
        print("usage: RedirectedOutputTest.py <femm_headless>")
        return 2
    exe = argv[1]
    if not os.path.exists(exe):
        print("SKIP: no femm_headless at %s" % exe)
        return 0

    fails = []
    biggest = 0
    tmp = tempfile.mkdtemp()
    try:
        for w, n in CASES:
            biggest = max(biggest, run_case(exe, tmp, w, n, fails))
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
        print("RedirectedOutput FAIL (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    print("RedirectedOutput OK: %d cases merged stdout and stderr into one "
          "descriptor; every line survived whole, across %d kB and a 4 kB "
          "buffer boundary that falls mid-line"
          % (len(CASES), biggest // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
