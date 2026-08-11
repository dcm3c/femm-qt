"""Audit the manual's figures before deleting any PostScript.

The preamble's \DeclareGraphicsExtensions still lists .eps and .ps as
fallbacks, so a figure with no .png is silently being satisfied by its
PostScript file today. Those are the ones deletion would break.
"""
import os
import sys

# Locate the tree from this script's own position: audit/ lives inside
# manual/, which lives inside the femm-qt checkout.
HERE = os.path.dirname(os.path.abspath(__file__))
MANUAL = os.path.dirname(HERE)
FEMM = os.path.dirname(MANUAL)
import glob
import re

os.chdir(MANUAL)

names = set()
for f in sorted(glob.glob("*.tex")):
    t = open(f, encoding="utf-8", errors="replace").read()
    # a literal backslash in the pattern must itself be escaped for the regex
    for m in re.finditer(chr(92) * 2 + r"includegraphics(?:\[[^]]*\])?\{([^}]+)\}", t):
        names.add(m.group(1))

png = {os.path.splitext(f)[0] for f in glob.glob("*.png")}
ps = {os.path.splitext(f)[0] for f in glob.glob("*.ps")}
eps = {os.path.splitext(f)[0] for f in glob.glob("*.eps")}

print("referenced figure names: %d" % len(names))
print(".png on disk: %d   .ps: %d   .eps: %d" % (len(png), len(ps), len(eps)))

missing = sorted(n for n in names if n not in png)
print()
print("REFERENCED BUT NO .png (%d):" % len(missing))
for n in missing:
    where = []
    if n in ps: where.append(".ps")
    if n in eps: where.append(".eps")
    print("   %-16s satisfied today by %s" % (n, "/".join(where) or "NOTHING"))

orphan_ps = sorted((ps | eps) - names)
print()
print("PostScript with no reference at all (%d): %s" % (len(orphan_ps), ", ".join(orphan_ps)))

both = sorted(n for n in names if n in png and (n in ps or n in eps))
print()
print("referenced, has .png, PostScript is now dead weight: %d" % len(both))
