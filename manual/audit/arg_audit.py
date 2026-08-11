"""Chapter 3, argument level: does each documented parameter list match the
signature the binding actually has?

Truth side: the lambda passed to m.def. Its parameter list gives a required
count and a total, since a trailing run of optional<...> parameters may be
omitted. Templated families (register_common_editor(m, "ci", ...) and friends)
are resolved to the prefixes they are registered with, as in the name audit.

Manual side: the parenthesised list on the \item that introduces a command.

Both sides are noisy in predictable ways, so the script self-checks against
four hand-verified signatures before reporting anything.
"""
import os

# Locate the tree from this script's own position: audit/ lives inside
# manual/, which lives inside the femm-qt checkout.
HERE = os.path.dirname(os.path.abspath(__file__))
MANUAL = os.path.dirname(HERE)
FEMM = os.path.dirname(MANUAL)

import collections
import glob
import io
import os
import re


PREFIXES = ("mi", "mo", "ei", "eo", "hi", "ho", "ci", "co")


def split_params(s):
    """Split a C++ parameter list on top-level commas (types carry <>)."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<([":
            depth += 1
        elif ch in ">)]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def match_paren(t, i):
    """i indexes the opening paren; return index of its match."""
    depth = 0
    while i < len(t):
        if t[i] == "(":
            depth += 1
        elif t[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


# ------------------------------------------------------------------ truth
os.chdir(FEMM)

helper_prefixes = collections.defaultdict(set)
for f in glob.glob("libfemm/py_*.cpp"):
    t = io.open(f, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"\b(register[A-Za-z_]*)\s*\(\s*m\s*,\s*\"([a-z]{2})\"", t):
        helper_prefixes[m.group(1)].add(m.group(2))

sig = {}          # name -> (required, total)
templ = []        # (helper, suffix, required, total)

for f in glob.glob("libfemm/py_*.cpp") + glob.glob("libfemm/py_*.h"):
    t = io.open(f, encoding="utf-8", errors="replace").read()
    # which helper function body each offset falls in (for the P + "_x" forms)
    bounds = [(m.start(), m.group(1))
              for m in re.finditer(r"\b(register[A-Za-z_]*)\s*\(", t)]

    # named lambdas: auto foo_fn = [capture](params) { ... };
    named = {}
    for m in re.finditer(r"\bauto\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*\[", t):
        rb2 = t.find("]", m.end() - 1)
        op2 = t.find("(", rb2)
        if op2 < 0 or op2 - rb2 > 3:
            continue
        cp2 = match_paren(t, op2)
        ps = [x for x in split_params(t[op2 + 1:cp2]) if x]
        named[m.group(1)] = (sum(1 for x in ps if "optional" not in x), len(ps))

    for m in re.finditer(r'm\.def\(\s*(?:"([a-z0-9_]+)"|P\s*\+\s*"(_[a-z0-9_]+)")\s*,', t):
        plain, suffix = m.group(1), m.group(2)
        # The lambda must follow immediately. m.def("name", some_fn) passes a
        # named function instead, and searching forward for the next '[' finds
        # an unrelated lambda further down the file -- which is exactly how
        # co_smooth, eo_smooth and ho_selectpoint got mis-reported.
        lb = t.find("[", m.end())
        if lb < 0 or lb - m.end() > 2:
            # not a lambda literal -- try a named function passed by name
            nm = re.match(r"\s*([A-Za-z_][A-Za-z_0-9]*)\s*\)", t[m.end():])
            if nm and nm.group(1) in named:
                if plain:
                    sig[plain] = named[nm.group(1)]
                else:
                    helper = None
                    for pos, name in bounds:
                        if pos < m.start():
                            helper = name
                    templ.append((helper, suffix) + named[nm.group(1)])
            continue
        rb = t.find("]", lb)
        op = t.find("(", rb)
        if op < 0 or op - rb > 3:
            # not a lambda (e.g. m.def("x", some_fn)) -- skip, unknown arity
            continue
        cp = match_paren(t, op)
        params = split_params(t[op + 1:cp])
        params = [p for p in params if p]
        total = len(params)
        req = sum(1 for p in params if "optional" not in p)
        if plain:
            sig[plain] = (req, total)
        else:
            helper = None
            for pos, name in bounds:
                if pos < m.start():
                    helper = name
            templ.append((helper, suffix, req, total))

for helper, suffix, req, total in templ:
    for p in helper_prefixes.get(helper, ()):
        sig.setdefault(p + suffix, (req, total))

# ------------------------------------------------------------------ manual
os.chdir(MANUAL)
doc = {}
for f in ("magnpy.tex", "elecpy.tex", "heatpy.tex", "cflowpy.tex"):
    t = io.open(f, encoding="utf-8", errors="replace").read()
    t = t.replace("{" + chr(92) + "_}", "_").replace(chr(92) + "_", "_")
    for line in t.split("\n"):
        if chr(92) + "item" not in line:
            continue
        m = re.search(r"\b((?:m[io]|e[io]|h[io]|c[io])_[a-z0-9_]+)\s*\(([^)]*)\)", line)
        if not m:
            continue
        name, args = m.group(1), m.group(2).strip()
        if name in doc:
            continue
        args = args.replace("{", "").replace("}", "").replace(chr(92) + "tt", "")
        parts = [a.strip() for a in args.split(",") if a.strip()]
        doc[name] = (len(parts), args, f)

# ------------------------------------------------------------------ check
SELF = {"mi_addnode": (2, 2), "mo_setfocus": (0, 1),
        "mi_addsegment": (4, 4), "mo_getpointvalues": (2, 2)}
bad = [k for k, v in SELF.items() if sig.get(k) != v]
print("self-check: %s" % ("OK" if not bad else "FAILED for %s -> %s"
                          % (bad, [sig.get(k) for k in bad])))
print("signatures extracted: %d   documented arg lists: %d" % (len(sig), len(doc)))

rows = []
for name, (n, args, f) in sorted(doc.items()):
    if name not in sig:
        continue
    req, total = sig[name]
    if n < req or n > total:
        rows.append((name, n, req, total, args, f))

nosig = sorted(n for n in doc if n not in sig)
print("documented commands with no extractable signature: %d" % len(nosig))
if nosig:
    print("   " + " ".join(nosig[:14]) + (" ..." if len(nosig) > 14 else ""))

print()
print("ARITY MISMATCHES (%d):" % len(rows))
print("   %-26s %-5s %-9s %s" % ("command", "doc", "real", "documented args"))
for name, n, req, total, args, f in rows:
    real = "%d" % req if req == total else "%d-%d" % (req, total)
    print("   %-26s %-5d %-9s %s" % (name, n, real, args[:46]))
