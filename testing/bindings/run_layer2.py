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
     "OF_circ_result.txt", 72),
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
    # Scilab is the one host whose CONSOLE binary is named differently per
    # platform -- WScilex-cli on Windows, where the W and the x are Scilab's
    # Windows convention -- which is why this row alone reported "not
    # installed" on a Linux box that had Scilab on PATH. Every other host here
    # happens to use a name that is the same everywhere: matlab, octave-cli,
    # julia.
    #
    # On Unix the name to want is scilab-adv-cli, NOT the shorter scilab-cli,
    # and the difference is not cosmetic. scifemm talks to femm-qt over
    # java.net.Socket, with PrintWriter and BufferedReader on top of it
    # (scifemm.sci:68), because Scilab has no native sockets -- so the binding
    # cannot work at all without a JVM. scilab-cli IS -nwni mode: no window,
    # no Java. It launches, gets as far as openfemm, and dies with "Scilab
    # 'jimport' function disabled in -nogui or -nwni mode". scilab-adv-cli is
    # the same thing with the JVM, and Windows' WScilex-cli already has one,
    # which is why this only ever showed up off Windows.
    #
    # scilab-cli is kept as a last resort rather than dropped: if it is the
    # only Scilab present, failing with that message is more use than a SKIP
    # saying Scilab is not installed when it plainly is.
    # macOS keeps its console binaries inside the .app and puts none of them on
    # PATH, so the bundle is searched too -- globbed rather than named, since
    # the version is in the directory, exactly as the Octave lookup above does.
    # PATH still wins, so someone who has chosen a particular Scilab keeps it.
    ("scifemm", "_scifemm.sce",
     # EVERY JVM-capable candidate is tried before the JVM-less one, which is
     # why scilab-cli sits alone at the end rather than inside the first
     # _which. Grouped by name it read correctly and behaved wrongly whenever
     # PATH carried scilab-cli but not scilab-adv-cli: it took the binary that
     # cannot run this binding while a working one sat in the .app, unlooked
     # at. That is not a corner case on macOS -- Homebrew's scilab cask
     # symlinks scilab and scilab-cli onto PATH and NOT scilab-adv-cli, so it
     # is the default arrangement there, and every run died in jimport.
     lambda: _which("WScilex-cli", "scilab-adv-cli")
             or _glob("/Applications/scilab-*.app/Contents/bin/scilab-adv-cli")
             or _first([
                 r"C:\Program Files\scilab-2026.0.1\bin\WScilex-cli.exe"])
             or _which("scilab-cli"),
     lambda exe, d: [exe, "-quit", "-f", d]),
    ("juliafemm", "_juliafemm.jl",
     lambda: shutil.which("julia") or _first([
         os.path.expanduser(r"~\AppData\Local\Programs\Julia-1.12.6\bin\julia.exe")]),
     lambda exe, d: [exe, d]),
    # luafemm is the one host we SHIP: lua4 is built here and installed beside
    # the engine, so this row runs on every platform without anything being
    # installed first -- unlike Octave, Julia, Scilab and Mathematica. It is
    # found next to femm_headless rather than on PATH for the same reason.
    ("luafemm", "_luafemm.lua",
     lambda: _beside_engine("lua4"), lambda exe, d: [exe, d]),
    ("mathfemm", "_mathfemm.m",
     lambda: shutil.which("math") or _first([
         r"C:\Program Files\Wolfram Research\Mathematica\9.0\math.exe"]),
     lambda exe, d: [exe, "-script", d]),
]


# Set by main() before the host table is consulted: the host lookups take no
# arguments, and lua4 is the only host whose location depends on --femm-dir.
FEMM_DIR = None


def _in_femm_dir(femm_dir, name):
    """A program shipped beside the engine, wherever this platform puts it.

    The bundle form FIRST, then the bare one. On macOS there is no
    build/bin/femm_headless and no build/bin/lua4: macos_build.sh copies the
    engine, the solvers and the interpreter into femm_gui.app and then deletes
    the loose copies, because two exeDir()s -- only one of which has the
    solvers and material libraries beside it -- is worse than one.

    All six bindings already resolve the engine this way, which is why the
    scenarios pass on macOS while the harness did not: it is the only thing
    here that runs a program directly, so it was the only thing still assuming
    the Windows/Linux layout. Same shape as the TEMP/TMPDIR fix of 2026-09-08,
    in the one place that fix did not reach. One function rather than two
    copies of the search order, so the luafemm row -- added after that was
    diagnosed, and the one other caller -- cannot drift back to the bug.
    """
    if not femm_dir:
        return None
    for cand in (os.path.join(femm_dir, "femm_gui.app", "Contents", "MacOS",
                              name),
                 os.path.join(femm_dir, name + ".exe"),
                 os.path.join(femm_dir, name)):
        if os.path.exists(cand):
            return cand
    return None


def _beside_engine(name):
    return _in_femm_dir(FEMM_DIR, name)


def _which(*names):
    """First of `names` found on PATH. For a host whose executable is spelled
    differently per platform."""
    for n in names:
        hit = shutil.which(n)
        if hit:
            return hit
    return None


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


# Diagnostic shapes, per host language. Anchored and punctuated so they cannot
# match the data: every captured line is either "===  section  ===" or
# "  name = re im | re im", and none of these patterns can occur in those.
_DIAG = [
    ("Mathematica", re.compile(r"^\s*[A-Za-z$][A-Za-z0-9$]*::[a-zA-Z]+:")),
    ("Octave/MATLAB", re.compile(r"^\s*(?:error|warning):", re.I)),
    ("Julia", re.compile(r"^\s*(?:ERROR|WARNING):|^\s*[┌│└]\s*(?:Warning|Error)")),
    ("Python", re.compile(r"^Traceback \(most recent call last\)|^\s*\w*(?:Error|Exception):")),
    ("Scilab", re.compile(r"^\s*!--error|^\s*at line\s+\d+\s+of")),
]


def _diagnostics(text):
    """Lines in a host's captured output that look like a complaint.

    The suite compares what a binding COMPUTES and ignores everything else in
    the capture, so a host that loads with errors and then produces correct
    numbers passes silently. That is not hypothetical: mathfemm printed

        MessageName::messg: MOGetAllGapB::usage cannot be set to ...

    on every load for the life of a release, and those four lines sat in
    l2_*_mathfemm.txt -- read by the harness, discarded, row green -- until a
    user opened Mathematica and saw them. Nothing was ever going to catch it.

    Mathematica cannot help from its side: under `math -script` everything
    goes to stdout, $Messages is ignored, and even an explicit
    WriteString["stderr", ...] comes out on stdout. So the scan is here.
    """
    hits = []
    for line in (text or "").splitlines():
        for lang, rx in _DIAG:
            if rx.search(line):
                hits.append((lang, line.strip()))
                break
    return hits


def _why(cmd, r):
    """What the host said when it failed, as lines to print.

    Only r.stdout is kept as the comparison input, so a host that died with a
    message on stderr used to leave "compared 0 components" as the entire
    record -- true, and useless. That is how a Scilab invocation that the
    Linux binary did not accept came back indistinguishable from a binding
    that had simply computed nothing.

    The command line is included because the failure this was written for was
    in the command line: the host was found and launched, and what it was
    asked to do was wrong.
    """
    out = ["      ran: %s" % " ".join(cmd),
           "      exit status: %s" % r.returncode]
    for stream, text in (("stderr", r.stderr), ("stdout", r.stdout)):
        lines = [l.rstrip() for l in (text or "").splitlines() if l.strip()]
        if not lines:
            out.append("      %s: empty" % stream)
            continue
        # Head AND tail, not just the tail. A host that rejects an argument
        # says so on its last line; a host that CRASHES says where on its
        # first, and then pads with several lines of advice about reporting
        # the bug. Showing 8 from the end of an 11-line Scilab segfault threw
        # away the three lines naming the function it died in and kept the
        # URL of the bug tracker -- on FreeBSD, where nobody can just re-run
        # it with a wider window.
        if len(lines) <= 16:
            shown, how = lines, "all %d lines" % len(lines)
        else:
            shown = lines[:8] + ["... %d lines omitted ..." % (len(lines) - 16)] \
                    + lines[-8:]
            how = "first and last 8 of %d lines" % len(lines)
        out.append("      %s (%s):" % (stream, how))
        out += ["        " + l for l in shown]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--femm-dir",
                    default=os.path.abspath(os.path.join(HERE, "..", "..",
                                                         "build", "bin")))
    # A full run is seven hosts times three scenarios and takes the better part
    # of ten minutes, nearly all of it spent re-proving the bindings that
    # already pass. When one binding is being worked on, these cut that to the
    # rows that can still change: --hosts scifemm is the whole of the
    # scifemm-only harness. Comma-separated, matched against the labels in
    # HOSTS and SCENARIOS, and an unknown name is an error rather than a silent
    # empty run -- a typo that quietly "passes" is worse than no filter at all.
    ap.add_argument("--hosts", help="only these hosts (comma-separated): "
                    + ", ".join(h[0] for h in HOSTS))
    ap.add_argument("--scenarios", help="only these scenarios: "
                    + ", ".join(s[0] for s in SCENARIOS))
    args = ap.parse_args()

    hosts, scenarios = HOSTS, SCENARIOS
    for opt, table, keep in (("hosts", HOSTS, args.hosts),
                             ("scenarios", SCENARIOS, args.scenarios)):
        if not keep:
            continue
        want = [w.strip() for w in keep.split(",") if w.strip()]
        bad = [w for w in want if w not in [row[0] for row in table]]
        if bad:
            print("no such %s: %s (have: %s)"
                  % (opt, ", ".join(bad), ", ".join(r[0] for r in table)))
            return 2
        sel = [row for row in table if row[0] in want]
        if opt == "hosts":
            hosts = sel
        else:
            scenarios = sel

    if not os.path.isdir(args.femm_dir):
        print("no build at %s -- build first, or pass --femm-dir"
              % args.femm_dir)
        return 2
    env = dict(os.environ, FEMM_DIR=args.femm_dir)
    global FEMM_DIR
    FEMM_DIR = args.femm_dir

    rc = 0
    for slabel, stem, comparer, golden, expect in scenarios:
        # Comparers live beside the engine suites unless the scenario is
        # binding-specific, in which case they live here.
        comparer = (os.path.join(HERE, comparer)
                    if os.path.exists(os.path.join(HERE, comparer))
                    else os.path.join(TESTING, comparer))
        if golden.startswith("@"):
            ref_script = os.path.join(HERE, golden[1:])
            golden = os.path.join(HERE, "l2_%s_reference.txt" % slabel)
            engine = _in_femm_dir(args.femm_dir, "femm_headless")
            if engine is None:
                print("no femm_headless under %s -- build first"
                      % args.femm_dir)
                return 2
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
        for label, suffix, find, argv in hosts:
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
            # A comparer that raised reports "Traceback (most recent call
            # last):" as its first line and the reason as its last, so taking
            # line 0 named the one thing every crash has in common and hid the
            # one thing that differs. That is how a Scilab run whose numbers
            # were perfectly good read as an opaque FAIL.
            if verdict.startswith("Traceback"):
                verdict = "the comparer crashed: " + lines[-1]
            m = re.search(r"compared (\d+)", verdict)
            n = int(m.group(1)) if m else 0
            ok = ("0 flagged" in verdict) and n >= expect
            if m and n < expect:
                verdict += "   <-- SHORT: expected %d" % expect
            # Right numbers are not the same as a clean run. A host that
            # complains on the way in and then computes correctly used to pass.
            diag = _diagnostics(r.stdout)
            if diag:
                ok = False
                verdict += "   <-- %d diagnostic line(s) from the host" % len(diag)
            print("  %-11s %-5s %s" % (label, "ok" if ok else "FAIL", verdict))
            if not ok:
                if diag:
                    # These first: when the numbers agreed, they ARE the
                    # finding, and burying them under a stdout dump is how
                    # this went unnoticed the first time.
                    seen = set()
                    for lang, line in diag:
                        if line in seen:
                            continue
                        seen.add(line)
                        print("      %s: %s" % (lang, line[:100]))
                        if len(seen) >= 8:
                            print("      ... %d more" % (len(diag) - len(seen)))
                            break
                for line in _why(argv(exe, driver), r):
                    print(line)
                rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
