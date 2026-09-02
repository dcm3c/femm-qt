# The two manuals, the shipped wrappers and the engine drift apart silently,
# and only some of that is visible to a user.
#
#     python3 DocDriftTest.py <path-to-femm_headless>
#
# A wrapper nobody documented is a discoverability problem. An ENTRY with no
# wrapper is worse: it fails at the user, in their script, after they did
# exactly what the manual said. The octavefemm manual carried eight of those
# for years -- three outerspace commands, three selectconductor commands and
# two moverotate2 -- and every one turned out to be a missing wrapper rather
# than a stale entry, so neither side alone could have found them.
#
# WHY THIS NEEDS THE ENGINE. Which manual should document a given wrapper is
# not a naming question. `mo_getb` looks exactly like an engine command and is
# toolbox sugar over mo_getpointvalues; `mo_setweightingscheme` looks exactly
# like toolbox sugar and is an engine command. Guessing from the name put
# ~30 commands in the wrong bucket, in both directions. So the engine is asked
# what it actually registers, and that answer decides:
#
#   engine command  -> the femm-qt manual must document it (the octavefemm
#                      manual may cross-reference, but is not the home)
#   toolbox only    -> only the octavefemm manual can document it
#
# Commands do not exist until a document of that type does, and the
# postprocessor half does not exist until a solution is loaded, so the probe
# opens and solves one document per physics. An audit that skips either step
# concludes the engine has no commands at all -- which is exactly what a first
# attempt reported.
#
# THREE WAYS TO GET THE LaTeX SCAN WRONG, all of which produced a confidently
# wrong number before this file existed:
#
#   1. LaTeX escapes the underscore. The manual writes `mi\_addnode`, so a
#      naive [a-z]{2}_ regex matches almost nothing -- it reported 5% coverage
#      where the truth was 79%, which was nearly taken as grounds to abandon
#      the manual outright.
#   2. `%\item{\tt mi\_getview} ...` is a COMMENT, not an entry. Counting
#      commented-out entries turned 8 phantom entries into 16.
#   3. `mi\_makeABC` matched as `mi_make` under a lowercase-only character
#      class, inventing four more commands that never existed.
#
# So: strip comments, unescape, and match mixed case. The `\item` anchor is
# what separates an ENTRY from a passing mention -- `co_numcontours` and
# `hi_hmax` appear in prose as argument names and are not commands.
import io
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OCT = os.path.join(ROOT, "octavefemm")
OCT_MAN = os.path.join(OCT, "manual")
MAIN_MAN = os.path.join(ROOT, "manual")

OCT_CHAPTERS = ("magn.tex", "elec.tex", "heat.tex", "cflow.tex")
MAIN_CHAPTERS = ("magnpy.tex", "elecpy.tex", "heatpy.tex", "cflowpy.tex")

CMD = re.compile(r"\b([cmeh][io]_[a-zA-Z0-9_]+)")

# Infrastructure the wrappers call, or toolbox lifecycle. Not commands a user
# issues, so not a documentation gap.
PLUMBING = {
    "callfemm", "callfemm_noeval", "quote", "quotec", "rawquote", "num", "numc",
    "femmselect", "create", "messagebox", "showconsole", "hideconsole",
    "main_maximize", "main_minimize", "main_resize", "main_restore",
    "AWG", "IEC", "openfemm", "closefemm", "opendocument", "newdocument",
    "smartmesh", "showpointprops", "hidepointprops", "complex2str",
}

PROBE = '''import femm
T = %r
names = set()
for kind, pre, doc in ((0, "mi", "PMTest.fem"), (1, "ei", "electrostatic1.fee"),
                       (2, "hi", "heatFlow1.feh"), (3, "ci", "currentFlow1.fec")):
    femm.newdocument(kind)
    names.update(dir(femm))
    try:
        femm.opendocument(T + doc)
        getattr(femm, pre + "_loadsolution")()
        names.update(dir(femm))
    except Exception as e:
        print("PROBEFAIL " + doc + ": " + str(e))
for n in sorted(names):
    if len(n) > 3 and n[1] in "io" and n[2] == "_":
        print("CMD " + n)
'''


def engine_commands(exe):
    """Every command the engine registers, asked of the engine itself."""
    d = HERE + os.sep
    fd, path = tempfile.mkstemp(suffix=".py", dir=HERE)
    os.close(fd)
    io.open(path, "w", encoding="utf-8").write(PROBE % d.replace(os.sep, "/"))
    try:
        out = subprocess.run([exe, "-run", path.replace(os.sep, "/")],
                             capture_output=True, text=True, timeout=300).stdout
    finally:
        os.remove(path)
    bad = [l for l in out.splitlines() if l.startswith("PROBEFAIL")]
    if bad:
        print("could not enumerate the engine:")
        for l in bad:
            print("    " + l)
        return None
    return set(l[4:] for l in out.splitlines() if l.startswith("CMD "))


def strip(text):
    """LaTeX source -> plain text with comments removed and \\_ unescaped."""
    bs = chr(92)
    text = re.sub(r"(?m)^\s*%.*$", "", text)
    text = re.sub(r"(?<!" + re.escape(bs) + r")%.*", "", text)
    return text.replace(bs + "_", "_")


def item_blocks(path):
    bs = chr(92)
    text = strip(io.open(path, encoding="utf-8", errors="replace").read())
    return re.split(r"(?m)^" + re.escape(bs) + r"item\b", text)[1:]


def documented(chapter_dir, chapters):
    """(commands named inside any \\item block, commands that HEAD an entry)."""
    named, heads = set(), set()
    for c in chapters:
        p = os.path.join(chapter_dir, c)
        if not os.path.exists(p):
            continue
        for block in item_blocks(p):
            named.update(CMD.findall(block))
            # A command that appears only in the BODY of someone else's entry
            # is cross-referenced, not documented, and must not be read as an
            # entry that needs a wrapper.
            head = CMD.search(block[:200])
            if head:
                heads.add(head.group(1))
    return named, heads


def main(argv):
    if len(argv) != 2:
        print("usage: DocDriftTest.py <femm_headless>")
        return 2
    engine = engine_commands(argv[1])
    if engine is None:
        return 2

    wrappers = sorted(f[:-2] for f in os.listdir(OCT) if f.endswith(".m"))
    shipped = set(wrappers)
    user_facing = [w for w in wrappers
                   if w not in PLUMBING and not w.endswith("analyse")]
    engine_side = [w for w in user_facing if w in engine]

    oct_named, oct_heads = documented(OCT_MAN, OCT_CHAPTERS)
    main_named, _ = documented(MAIN_MAN, MAIN_CHAPTERS)

    problems = []
    for name in sorted(h for h in oct_heads if h not in shipped):
        problems.append(("octavefemm manual documents a command with no wrapper", name))
    for name in sorted(w for w in user_facing if w not in oct_named):
        problems.append(("shipped wrapper undocumented in the octavefemm manual", name))
    for name in sorted(w for w in engine_side if w not in main_named):
        problems.append(("engine command undocumented in the femm-qt manual", name))

    print("engine commands registered   %d" % len(engine))
    print("octavefemm wrappers shipped  %d  (%d user-facing, %d plumbing/aliases)"
          % (len(wrappers), len(user_facing), len(wrappers) - len(user_facing)))
    print("  of those, engine commands  %d  (the rest are toolbox-only)"
          % len(engine_side))
    print("  documented, octavefemm manual  %d of %d"
          % (len([w for w in user_facing if w in oct_named]), len(user_facing)))
    print("  documented, femm-qt manual     %d of %d applicable"
          % (len([w for w in engine_side if w in main_named]), len(engine_side)))

    if problems:
        by_kind = {}
        for kind, name in problems:
            by_kind.setdefault(kind, []).append(name)
        for kind in sorted(by_kind):
            names = by_kind[kind]
            print("")
            print("%d %s:" % (len(names), kind))
            for n in names:
                print("    " + n)
        print("")
        print("DocDrift: FAIL, %d problem(s)" % len(problems))
        return 1

    print("")
    print("DocDrift: ok, %d wrappers and %d engine commands, no drift in either "
          "direction" % (len(user_facing), len(engine)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
