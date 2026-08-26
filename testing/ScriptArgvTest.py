# Arguments after the script name must reach it as sys.argv.
#
#     python3 ScriptArgvTest.py <path-to-femm_headless>
#
# Before this existed, femm_headless and femm_gui parsed their own switches and
# SILENTLY DISCARDED everything else, while MicroPython left sys.argv defined
# but empty (runtime.c initialises the list and nothing ever appends to it). So
# a script doing sys.argv[1] got an IndexError -- a symptom that reads as a bug
# in the script rather than as a missing feature.
#
# The contract, matching CPython and the `arg` table Lua 4 builds for femm-lua:
#
#     argv[0]   the script itself, as an ABSOLUTE path
#     argv[1:]  everything after the script name, verbatim
#
# argv[0] is absolute rather than as-typed because both front ends chdir into
# the script's own directory before running it (so the script's relative paths
# resolve); a relative argv[0] would no longer point at anything from in there.
#
# "Everything after the script name" is literal: naming the script ends the
# engine's own argument list, so `-run s.py --port 5` hands --port to the
# script. That is what CPython does with `python s.py -v`, and what Lua 4's -f
# does. Engine switches go BEFORE the script. Case 4 pins it, because the
# tempting "keep parsing our switches" reading would break any script with a
# flag whose name we happen to share -- now or in some future release.
#
# Driven from outside because that is the only place the feature exists: an
# in-process test cannot set the command line it was itself started with.
import os
import subprocess
import sys
import tempfile

# (label, arguments after the script, expected sys.argv[1:])
CASES = (
    ("no arguments",        [],                       []),
    ("three arguments",     ["alpha", "beta", "42"],  ["alpha", "beta", "42"]),
    ("embedded spaces",     ["two words", " lead"],   ["two words", " lead"]),
    # A switch after the script belongs to the script, not to the engine. If
    # this ever fails with argv[1:] == [] the parser resumed scanning.
    ("switch after script", ["--port", "5"],          ["--port", "5"]),
    # Non-ASCII has to survive the Windows wide-argv -> UTF-8 conversion in
    # femm_utf8_args, the same path a script PATH takes; testers in Germany and
    # Belgium hit this routinely.
    ("non-ascii",           ["M\u00f6bius", "Gr\u00fc\u00dfe"],
                            ["M\u00f6bius", "Gr\u00fc\u00dfe"]),
    # An empty argument is a real thing a shell can pass and must not vanish.
    ("empty argument",      ["", "after"],            ["", "after"]),
)


def run_case(exe, tmp, i, args, fails, label):
    script = os.path.join(tmp, "argv_%d.py" % i)
    out = os.path.join(tmp, "argv_%d.txt" % i)
    with open(script, "w") as f:
        # Written as UTF-8 bytes and compared as UTF-8 bytes: printing is not
        # usable here, because the value under test may not be encodable in
        # the console code page the harness happens to be running under.
        # chr(0), not an escape: an escape here is interpreted when THIS
        # file is compiled, so the generated script would receive a real
        # NUL byte -- which exec_python_file rejects outright, failing
        # every case with a message about the harness, not about argv.
        f.write("import sys, io\n"
                "f = io.open(%r, 'wb')\n"
                "f.write(chr(0).join(sys.argv).encode('utf-8'))\n"
                "f.close()\n" % out.replace("\\", "/"))

    rc = subprocess.call([exe, "-run", script] + args,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if rc != 0:
        fails.append("%s: exit code %d" % (label, rc))
        return
    if not os.path.exists(out):
        fails.append("%s: the script did not run" % label)
        return

    with open(out, "rb") as f:
        got = f.read().decode("utf-8").split(chr(0))

    # argv[0]: absolute, and pointing at the script that ran.
    if not got:
        fails.append("%s: sys.argv is empty" % label)
        return
    zero = got[0].replace("\\", "/")
    if os.path.normcase(os.path.abspath(zero)) != os.path.normcase(script):
        fails.append("%s: argv[0] is %r, expected the script %r"
                     % (label, got[0], script))
    if not os.path.isabs(zero):
        fails.append("%s: argv[0] is relative (%r); it must survive the chdir "
                     "into the script's directory" % (label, got[0]))

    want = list(CASES[i][2])
    if got[1:] != want:
        fails.append("%s: argv[1:] is %r, expected %r" % (label, got[1:], want))


def main(argv):
    if len(argv) != 2:
        print("usage: ScriptArgvTest.py <femm_headless>")
        return 2
    exe = argv[1]
    if not os.path.exists(exe):
        print("SKIP: no femm_headless at %s" % exe)
        return 0

    fails = []
    tmp = tempfile.mkdtemp()
    try:
        for i, (label, args, _) in enumerate(CASES):
            run_case(exe, tmp, i, args, fails, label)
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
        print("ScriptArgv FAIL (%d)" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    print("ScriptArgv OK: %d cases -- argv[0] is the script's absolute path "
          "and argv[1:] arrives verbatim, including spaces, an empty string, "
          "non-ascii, and a switch that would otherwise look like ours"
          % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
