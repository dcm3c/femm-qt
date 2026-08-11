"""Diff the manual's Chapter 3 against the command names the interpreter
actually exposes (C:/temp/cmds.txt, dumped from a running femm_headless with a
document and a solution open for each physics)."""
import os

# Locate the tree from this script's own position: audit/ lives inside
# manual/, which lives inside the femm-qt checkout.
HERE = os.path.dirname(os.path.abspath(__file__))
MANUAL = os.path.dirname(HERE)
FEMM = os.path.dirname(MANUAL)

import collections
import io
import os
import re

real = set(l.strip() for l in io.open(os.path.join(HERE, "commands.txt")) if l.strip())

os.chdir(MANUAL)
doc = collections.defaultdict(set)
for f in ("magnpy.tex", "elecpy.tex", "heatpy.tex", "cflowpy.tex"):
    t = io.open(f, encoding="utf-8", errors="replace").read()
    # the chapters spell the underscore three ways: \_ , {\_} and plain
    t = t.replace("{" + chr(92) + "_}", "_").replace(chr(92) + "_", "_")
    for m in re.finditer(r"\b(m[io]|e[io]|h[io]|c[io])_([a-z0-9_]+)", t):
        doc[m.group(1) + "_" + m.group(2)].add(f)

print("documented %d   exposed %d" % (len(doc), len(real)))

missing = sorted(set(doc) - real)
print()
print("DOCUMENTED BUT NOT EXPOSED (%d) -- a script following the manual fails:" % len(missing))
by_suffix = collections.defaultdict(list)
for n in missing:
    by_suffix[n.split("_", 1)[1]].append(n)
for sfx in sorted(by_suffix):
    print("   %-22s %s" % (sfx, " ".join(by_suffix[sfx])))

undoc = sorted(real - set(doc))
print()
print("EXPOSED BUT NOT DOCUMENTED (%d):" % len(undoc))
by_suffix = collections.defaultdict(list)
for n in undoc:
    by_suffix[n.split("_", 1)[1]].append(n)
for sfx in sorted(by_suffix):
    print("   %-22s %s" % (sfx, " ".join(by_suffix[sfx])))
