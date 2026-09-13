# luafemm: is it still compatible with FEMM 4.2 scripts, and does the launcher
# actually work?
#
#     python3 LuaFemmTest.py <path-to-femm_headless>
#
# This is the only binding with NO automated coverage before 2026-09-11, and
# the cost of that showed the moment anyone looked: femm-lua.sh was missing
# from the macOS distribution entirely, all three shipped examples died on
# their first line under the lua4 we ship, and femm.lua was missing 56 of the
# commands FEMM 4.2 documents. None of it was exotic. Nothing was checking.
#
# Two things are checked here, because luafemm fails in two different ways.
#
# 1. SURFACE. luafemm exists for one reason -- to run FEMM 4.2 scripts -- so
#    the oracle is FEMM 4.2's own script surface, held in
#    femm42_lua_surface.txt beside this file, not the newer bindings' much
#    larger API. Measured against octavefemm it looked like 207 commands were
#    missing; against 4.2 it was 56. The wrong oracle turns a finished binding
#    into a permanent backlog. Reachability is settled by ASKING the engine,
#    not by consulting another binding's file list -- doing the latter hid six
#    more gaps behind a proxy that happened to lack them too.
#
#    The 11 names FEMM 4.2 has that this engine does not implement, and why
#    none of them is a gap:
#
#      MATLAB link      flput lua2matlab mlclose mlopen mlput
#      console / GUI    actxprint pause prompt
#      other            makeplot mo_gradient setcompatibilitymode
#
#    A further 4 are client-side rather than engine-side and femm.lua supplies
#    them itself: mi_setbhpointsfromfile and smartmesh (FEMM 4.2 wrote both in
#    init.lua, not in C++), and showpointprops / hidepointprops, which toggle a
#    floating window femm-qt does not have.
#
#    Three categories had to be added after a mutation showed the check was
#    blind to each. Renaming messagebox was NOT caught, because the engine probe
#    borrowed from DocDriftTest lists only DOCUMENT commands and every global
#    was therefore scored "outside the engine"; global_commands() fixes that.
#    Deleting "quit = exit" was not caught either, because only "function name("
#    counted as a wrapper. And print and exit must NOT be wrapped -- they are
#    Lua 4's own, and overriding print would silence every script in the corpus
#    -- which is settled by asking a bare interpreter, not by a list kept here.
#
#    setcompatibilitymode is a deliberate non-goal, decided 2026-09-11. It
#    switches FEMM 4.2's engine to its old_lua_* handlers, which are FEMM 4.0
#    and 4.1's argument lists -- old_lua_addmatprop takes Jsrc as re and im in
#    positions 5 and 6, where 4.2 takes one complex value in position 5. That
#    is a second, incompatible API for a twenty-year-old release nobody runs.
#    Not implementing it is the safe failure: a script calling it gets an
#    error, rather than the same calls being interpreted differently.
#
# 2. THE LAUNCHER AND THE CORPUS. femm-lua.sh / femm-lua.bat is shipped code
#    with real logic -- interpreter discovery, port selection, headless vs GUI,
#    argument passthrough -- and no other binding has one. A wrapper-level test
#    cannot see it: loading femm.lua directly works fine while the launcher is
#    absent from the installer, which is exactly what happened on macOS. So the
#    examples are run THROUGH the launcher, end to end, against a real engine.
import io
import os
import re
import subprocess
import tempfile
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
LUAFEMM = os.path.join(ROOT, "luafemm")
SURFACE = os.path.join(HERE, "femm42_lua_surface.txt")

fails = []


def check(label, ok, detail=""):
    if not ok:
        fails.append("%s%s" % (label, (": " + detail) if detail else ""))


# --- 1. Surface ----------------------------------------------------------
def read_surface():
    """Every name FEMM 4.2 makes callable from a script."""
    return [l.strip() for l in io.open(SURFACE, encoding="utf-8")
            if l.strip() and not l.startswith("#")]


def global_commands(exe):
    """The engine's names that are NOT document commands.

    Session-level things -- open, create, newdocument, smartmesh, messagebox,
    setcurrentdirectory, the main_* window commands -- exist before any
    document does, so this probe needs none of the model loading
    DocDriftTest's does and costs a second rather than a minute.
    """
    fd, path = tempfile.mkstemp(suffix=".py", dir=HERE)
    os.close(fd)
    io.open(path, "w", encoding="utf-8", newline="\n").write(
        "import femm\n"
        "for n in sorted(dir(femm)):\n"
        "    print('CMD ' + n)\n")
    try:
        r = subprocess.run([exe, "-run", path.replace(os.sep, "/")],
                           capture_output=True, text=True, timeout=300)
    finally:
        os.remove(path)
    return set(l[4:] for l in r.stdout.splitlines() if l.startswith("CMD "))


def lua_builtins(bindir, names):
    """Which of `names` a bare Lua 4 already provides as functions.

    No femm.lua is loaded, so what comes back is the interpreter's own surface
    and nothing else.
    """
    lua = os.path.join(bindir, "lua4.exe" if os.name == "nt" else "lua4")
    if not os.path.exists(lua):
        return set()
    fd, path = tempfile.mkstemp(suffix=".lua", dir=HERE)
    os.close(fd)
    body = "N = {%s}\n" % ",".join('"%s"' % n for n in sorted(names))
    body += ("for i = 1, getn(N) do\n"
             "  if type(getglobal(N[i])) == \"function\" then print(N[i]) end\n"
             "end\n")
    io.open(path, "w", encoding="utf-8", newline="\n").write(body)
    try:
        r = subprocess.run([lua, "-f", path], capture_output=True,
                           text=True, timeout=120)
    finally:
        os.remove(path)
    return set(l.strip() for l in r.stdout.splitlines() if l.strip())


def surface_check(exe):
    """A gap is a command FEMM 4.2 offers, the engine implements, and femm.lua
    does not wrap.

    All three terms are measured, none assumed. The engine is ASKED rather than
    guessed at -- DocDriftTest already does that for the manuals and its probe
    is reused here, because you cannot tell from a command's name whether this
    engine implements it.

    An earlier version of this check compared against a file that had itself
    been derived from femm.lua, which made it circular: it could catch a future
    regression but not a gap present on the day it was written. The oracle is
    now FEMM 4.2 plus the live engine, and femm.lua is only ever the thing
    being measured.
    """
    surface = read_surface()
    src = io.open(os.path.join(LUAFEMM, "femm.lua"),
                  encoding="utf-8", errors="replace").read()
    have = set(re.findall(r"^\s*function ([A-Za-z_]\w*)", src, re.M))
    # ... plus bare aliases, written "quit = exit" rather than as a function.
    # Kept to that exact shape -- name, equals, one name, end of line -- so this
    # cannot quietly absorb an assignment that is not a wrapper and call a real
    # gap covered.
    have |= set(re.findall(r"^([A-Za-z_]\w*) = [A-Za-z_]\w*\s*$", src, re.M))

    sys.path.insert(0, HERE)
    from DocDriftTest import engine_commands
    engine = engine_commands(exe)
    check("the engine could be enumerated", engine is not None)
    if engine is None:
        return len(surface), 0, 0
    # DocDriftTest's probe enumerates DOCUMENT commands -- its filter keeps
    # only names shaped xx_ -- so open, create, messagebox, smartmesh and the
    # main_* window commands never appear in it. That is right for the manuals
    # and wrong here: a global missing from femm.lua would have been counted
    # "outside the engine" and passed silently. Verified by deleting
    # messagebox, which this check did not notice until the globals were added.
    engine = engine | global_commands(exe)

    # Two of FEMM 4.2's documented names -- print and exit -- are Lua 4's own,
    # and femm.lua must NOT define them: overriding print would silence every
    # script in the corpus. Which names those are is a question for the
    # interpreter, not for a list kept here, so ask a bare lua4 that has not
    # loaded femm.lua.
    have = have | lua_builtins(os.path.dirname(exe), surface)

    # init.lua's helpers are client-side by nature: FEMM 4.2 defined them in
    # Lua, not in C++, so the engine will not report them and they must be
    # wrapped here regardless.
    inengine = [c for c in surface if c in engine]
    gaps = sorted(c for c in inengine if c not in have)
    check("FEMM 4.2 commands the engine has but femm.lua does not wrap",
          not gaps, "%d: %s" % (len(gaps), " ".join(gaps[:12])))

    outside = sorted(c for c in surface if c not in engine and c not in have)
    return len(surface), len(inengine), len(outside)


# --- 2. Launcher and corpus ----------------------------------------------
def launcher_path(bindir):
    """femm-lua.bat on Windows, femm-lua.sh elsewhere. Named rather than
    globbed: which one exists IS the thing being checked."""
    for name in ("femm-lua.bat", "femm-lua.sh"):
        p = os.path.join(bindir, name)
        if os.path.exists(p):
            return p
    return None


def run_launcher(launcher, args, cwd, timeout=600):
    argv = ([launcher] if launcher.endswith(".sh") else [launcher])
    if launcher.endswith(".sh"):
        argv = ["bash", launcher]
    return subprocess.run(argv + args, cwd=cwd, capture_output=True,
                          text=True, timeout=timeout)


def launcher_check(bindir):
    launcher = launcher_path(bindir)
    check("a launcher ships beside the binaries", launcher is not None,
          "no femm-lua.bat or femm-lua.sh in " + bindir)
    if not launcher:
        return 0

    # femm.lua must be beside it: the launcher resolves it as $here/femm.lua,
    # so a launcher without its library is a launcher that cannot run.
    check("femm.lua sits beside the launcher",
          os.path.exists(os.path.join(os.path.dirname(launcher), "femm.lua")))

    r = run_launcher(launcher, ["--help"], bindir, timeout=60)
    check("--help exits 0", r.returncode == 0, "rc=%d" % r.returncode)
    check("--help describes the switches", "-p" in r.stdout or "-p" in r.stderr)

    # Every shipped example, headless, through the launcher. Each prints its
    # own "completed successfully" line; a script that dies prints a Lua
    # traceback instead, and exits non-zero.
    exdir = os.path.join(LUAFEMM, "examples")
    ran = 0
    for name in sorted(os.listdir(exdir)):
        if not name.endswith(".lua"):
            continue
        r = run_launcher(launcher, ["-h", name], exdir)
        out = (r.stdout or "") + (r.stderr or "")
        ok = r.returncode == 0 and "completed successfully" in out
        tail = [l for l in out.strip().splitlines() if l.strip()][-1:] or [""]
        check("example runs: " + name, ok, "rc=%d, last line: %s"
              % (r.returncode, tail[0][:120]))
        ran += 1
    check("there are examples to run", ran > 0)

    # A script that DIES must be reported as a failure by the launcher itself,
    # not merely by leaving a traceback on the terminal. femm-lua.bat ended on
    # "goto :eof", which returns whatever errorlevel happens to be current
    # rather than the interpreter's, so a broken script exited 0 and anything
    # scripted around femm-lua saw success.
    #
    # Checked in BOTH directions, because the first attempt at that fix made a
    # passing script report failure instead -- a one-sided check would have
    # shipped it. The passing direction is covered by the example rows above.
    boom = os.path.join(exdir, "_boom.lua")
    io.open(boom, "w", encoding="utf-8", newline="\n").write(
        'error("deliberate failure")\n')
    try:
        r = run_launcher(launcher, ["-h", "_boom.lua"], exdir, timeout=180)
        check("a failing script makes the launcher exit non-zero",
              r.returncode != 0, "rc=%d" % r.returncode)
    finally:
        if os.path.exists(boom):
            os.remove(boom)

    # The WIDEST command FEMM 4.2 has. mi_addmaterial reads positions 1..14
    # (femmeLua.cpp, lua_addmatprop) and callfemm carried only ten, so a 4.2
    # script lost the tail with no error at all -- the material simply came out
    # with default NStrands and WireD. That is the failure mode worth pinning:
    # not a crash, a quietly wrong model.
    #
    # Checked through the saved document rather than through mi_getmaterial,
    # which reads the materials LIBRARY and not what the script just defined.
    wide = os.path.join(exdir, "_wide.lua")
    fem = os.path.join(exdir, "_wide.fem")
    io.open(wide, "w", encoding="utf-8", newline="\n").write(
        'newdocument(0)\n'
        'mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)\n'
        'mi_addmaterial("W14", 2, 3, 4, 5, 6, 7, 8, 9, 1, 11, 12, 13, 14)\n'
        'mi_saveas("_wide.fem")\n'
        'mi_close()\n')
    try:
        run_launcher(launcher, ["-h", "_wide.lua"], exdir, timeout=180)
        text = ""
        if os.path.exists(fem):
            text = io.open(fem, encoding="utf-8", errors="replace").read()
        got = dict(re.findall(
            r"<(NStrands|WireD|Phi_hx|Phi_hy)>\s*=\s*(\S+)", text))
        want = {"Phi_hx": "11", "Phi_hy": "12",
                "NStrands": "13", "WireD": "14"}
        check("all 14 arguments of mi_addmaterial reach the engine",
              got == want, "got %r" % (got,))
    finally:
        for f in (wide, fem):
            if os.path.exists(f):
                os.remove(f)

    # Arguments after the script belong to the script, exactly as with -run on
    # femm_headless. Lua 4 delivers them in the global `arg` table -- the same
    # global that names the complex phase function, which is why both meanings
    # are pinned below.
    probe = os.path.join(exdir, "_argprobe.lua")
    io.open(probe, "w", encoding="utf-8", newline="\n").write(
        'print("ARGN=" .. getn(arg))\n'
        'print("ARG1=" .. tostring(arg[1]))\n'
        'print("ARGFN=" .. arg(3 + 4*I))\n')
    try:
        r = run_launcher(launcher, ["-h", "_argprobe.lua", "alpha", "beta"],
                         exdir, timeout=120)
        out = (r.stdout or "") + (r.stderr or "")
        check("script arguments reach the script", "ARG1=alpha" in out,
              out.strip().replace("\n", " | ")[:160])
        # ...and arg() the complex phase function still works, even though
        # Lua 4 binds the same global name to that table. lua/lua.c gives the
        # table a "function" tag method so both meanings survive; without it a
        # FEMM 4.2 script asking for a phase dies on a line that has been
        # correct for twenty years.
        check("arg() still returns a complex phase", "ARGFN=0.92729" in out,
              out.strip().replace("\n", " | ")[:160])
    finally:
        if os.path.exists(probe):
            os.remove(probe)
    return ran


def main():
    if len(sys.argv) < 2:
        print("usage: LuaFemmTest.py <path-to-femm_headless>")
        return 2
    bindir = os.path.dirname(os.path.abspath(sys.argv[1]))

    nsurface, ninengine, noutside = surface_check(os.path.abspath(sys.argv[1]))
    nex = launcher_check(bindir)

    if fails:
        print("FAIL (%d):" % len(fails))
        for f in fails:
            print("  " + f)
        return 1
    print("luafemm check: of FEMM 4.2's %d documented script names, the engine "
          "implements %d and femm.lua wraps every one of them (%d are outside "
          "this engine -- the MATLAB link, the interactive console, and FEMM "
          "4.0/4.1 compatibility mode); the "
          "launcher runs, and %d shipped examples complete through it against "
          "a real headless engine"
          % (nsurface, ninengine, noutside, nex))
    return 0


if __name__ == "__main__":
    sys.exit(main())
