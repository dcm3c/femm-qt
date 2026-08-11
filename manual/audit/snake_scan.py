"""Scan every .tex for snake_case command spellings.

Two passes: the 265 word-separated names Original FEMM registered, and a
generic pass for any prefixed name carrying an extra underscore, which would
catch a spelling OF never had either.
"""
import os
import sys

# Locate the tree from this script's own position: audit/ lives inside
# manual/, which lives inside the femm-qt checkout.
HERE = os.path.dirname(os.path.abspath(__file__))
MANUAL = os.path.dirname(HERE)
FEMM = os.path.dirname(MANUAL)
import glob
import io
import re

of = set(l.strip() for l in io.open(os.path.join(HERE, "of_commands.txt")) if l.strip())
PRE = ("mi_", "mo_", "ei_", "eo_", "hi_", "ho_", "ci_", "co_")


def compact(n):
    for p in PRE:
        if n.startswith(p):
            return p + n[len(p):].replace("_", "")
    return n


snake = {n for n in of if n.startswith(PRE) and compact(n) != n}
print("OF snake_case names to look for: %d" % len(snake))

os.chdir(MANUAL)
hits = []
generic = []
for f in sorted(glob.glob("*.tex")):
    raw = io.open(f, encoding="utf-8", errors="replace").read()
    t = raw.replace("{" + chr(92) + "_}", "_").replace(chr(92) + "_", "_")
    for i, line in enumerate(t.split("\n"), 1):
        for n in snake:
            if re.search(r"\b" + n + r"\b", line):
                hits.append((f, i, n, line.strip()[:70]))
        # generic: prefix + word + _ + word
        for m in re.finditer(r"\b((?:m[io]|e[io]|h[io]|c[io])_[a-z0-9]+_[a-z0-9_]+)\b", line):
            generic.append((f, i, m.group(1), line.strip()[:70]))

print()
print("OF snake_case spellings found in the manual: %d" % len(hits))
for h in hits[:20]:
    print("   %s:%d  %s" % (h[0], h[1], h[2]))

print()
print("any prefixed name with an internal underscore: %d" % len(generic))
seen = set()
for f, i, n, line in generic:
    if n in seen:
        continue
    seen.add(n)
    print("   %-28s %s:%d" % (n, f, i))
