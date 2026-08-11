# dump_commands.py -- write the scripting command names FEMM actually exposes.
#
# Run INSIDE femm-qt, from this directory:
#     ../../build/bin/femm_headless -run dump_commands.py
#
# This is MicroPython, not CPython: no __file__, no os.path, no re. Paths are
# relative to the working directory, which is why the command above cds here
# first. Output is commands.txt, which cmd_diff.py reads.
#
# The commands only exist once a document does -- they are registered on
# open/newdocument/loadsolution (py_bind.cpp) -- so this opens one model per
# physics and loads its solution, recording what appears each time.

PREFIXES = ("mi_", "mo_", "ei_", "eo_", "hi_", "ho_", "ci_", "co_")
EX = "../../examples/"

CASES = [
    ("magnetics",      "m", EX + "coil.fem"),
    ("electrostatics", "e", EX + "bdemo2.fee"),
    ("heatflow",       "h", EX + "auto-htutor.feh"),
    ("currentflow",    "c", EX + "cdemo2.fec"),
]


def snap():
    return set(n for n in globals() if n.startswith(PREFIXES))


base = snap()
seen = {}

for label, pfx, doc in CASES:
    before = snap()
    try:
        opendocument(doc)
    except Exception as e:
        print("%s: could not open %s (%s)" % (label, doc, e))
        continue
    editor = snap() - before
    try:
        globals()[pfx + "i_loadsolution"]()
    except Exception as e:
        print("%s: no solution loaded (%s)" % (label, e))
    post = snap() - before - editor
    seen[label] = (editor, post)

names = set()
for e, p in seen.values():
    names |= e | p

f = open("commands.txt", "w")
for n in sorted(names):
    f.write(n + "\n")
f.close()

for label in sorted(seen):
    e, p = seen[label]
    print("%-16s editor %3d   postproc %3d" % (label, len(e), len(p)))
print("wrote commands.txt: %d names" % len(names))
