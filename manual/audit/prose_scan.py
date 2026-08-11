"""Cheap mechanical scan of Chapter 6 and the Appendix before reading them:
doubled words, stale program names, and the sentence-level damage that has
turned up repeatedly elsewhere in the manual."""
import os
import sys

# Locate the tree from this script's own position: audit/ lives inside
# manual/, which lives inside the femm-qt checkout.
HERE = os.path.dirname(os.path.abspath(__file__))
MANUAL = os.path.dirname(HERE)
FEMM = os.path.dirname(MANUAL)
import io
import re

os.chdir(MANUAL)

import glob

targets = sorted(glob.glob("*.tex"))
body = []
for t in targets:
    for i, l in enumerate(io.open(t, encoding="utf-8",
                                  errors="replace").read().split("\n"), 1):
        body.append(("%s:%d" % (t, i), l))

print("scanning %d lines across %s" % (len(body), ", ".join(targets)))

# doubled words
print()
print("DOUBLED WORDS:")
for where, l in body:
    for m in re.finditer(r"\b([A-Za-z]{2,})\s+\1\b", l):
        print("   %-18s %s" % (where, l.strip()[:72]))

# stale program names and known damage patterns
PATTERNS = ["Triangle", "femm.exe", "ActiveX", "MathLink", "Lua ", "lua",
            " is be ", "the the ", " a a ", "Windows 95", "metafile"]
print()
print("PATTERN HITS:")
for where, l in body:
    for p in PATTERNS:
        if p in l:
            print("   %-18s [%s]  %s" % (where, p.strip(), l.strip()[:60]))

# sentences ending mid-clause, as "displayed that." did
print()
print("SUSPICIOUS ENDINGS:")
for where, l in body:
    if re.search(r"\b(that|which|the|of|to|for|with|and|is)\.\s*$", l.strip()):
        print("   %-18s %s" % (where, l.strip()[:72]))
