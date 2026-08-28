"""Find definitions that shadow an earlier one in the same binding.

    python3 find_shadowed.py

MIAddPointProp was defined three times in mathfemm, and the third repeated the
first's pattern while emitting one fewer argument -- so every three-argument
call silently lost its last argument. The emission harness could see that the
command was wrong, but not that an overload had gone missing, because it only
ever calls at the contract's arity. This looks for the same shape directly in
the source.

Every language here resolves a redefinition the same way -- last one wins --
so a repeated signature is always a bug or always dead code.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")


def norm_mma(args):
    """Mathematica pattern -> comparable shape: names dropped, structure kept."""
    a = re.sub(r"\b[A-Za-z][A-Za-z0-9]*_", "_", args)
    return re.sub(r"\s+", "", a)


def scan_mathfemm(path):
    seen, out = {}, []
    src = io.open(path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"^([A-Z][A-Za-z0-9]*)\[([^\]]*)\]\s*:=", src, re.M):
        name, sig = m.group(1), norm_mma(m.group(2))
        line = src[: m.start()].count("\n") + 1
        key = (name, sig)
        if key in seen:
            out.append((name, sig, seen[key], line))
        else:
            seen[key] = line
    return out, len(seen) + len(out)


def scan_julia(path):
    seen, out = {}, []
    src = io.open(path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"^(?:function\s+)?([a-z_][A-Za-z0-9_]*)\(([^)]*)\)\s*=",
                         src, re.M):
        name = m.group(1)
        # Signature identity in Julia is the argument TYPES; untyped args are
        # all ::Any, so arity plus the annotations is the discriminator.
        sig = re.sub(r"\s+", "", re.sub(r"\b[a-z_][A-Za-z0-9_]*(?=(,|$|::))",
                                        "x", m.group(2)))
        line = src[: m.start()].count("\n") + 1
        key = (name, sig)
        if key in seen:
            out.append((name, sig, seen[key], line))
        else:
            seen[key] = line
    return out, len(seen) + len(out)


def scan_scilab(path):
    seen, out = {}, []
    src = io.open(path, encoding="utf-8", errors="replace").read()
    # The return-value part must be matched ONLY when an = follows it.
    # Written as an independent optional group it backtracks: for
    # `function ci_hidegrid()` it happily consumes "ci_hidegri" and captures
    # the trailing "d" as the function name.
    for m in re.finditer(r"^\s*function\s+(?:(?:\[[^\]]*\]|\w+)\s*=\s*)?"
                         r"([A-Za-z_]\w*)\s*\(", src, re.M):
        name = m.group(1)
        line = src[: m.start()].count("\n") + 1
        # Scilab has no overloading at all: a second definition simply replaces
        # the first, whatever its arity.
        if name in seen:
            out.append((name, "(no overloading in Scilab)", seen[name], line))
        else:
            seen[name] = line
    return out, len(seen) + len(out)


def scan_python(path):
    seen, out = {}, []
    src = io.open(path, encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"^(\s*)def\s+([A-Za-z_]\w*)\s*\(", src, re.M):
        indent, name = len(m.group(1)), m.group(2)
        line = src[: m.start()].count("\n") + 1
        key = (indent, name)          # same scope = same indent here
        if key in seen:
            out.append((name, "same scope", seen[key], line))
        else:
            seen[key] = line
    return out, len(seen) + len(out)


TARGETS = [
    ("mathfemm", os.path.join(ROOT, "mathfemm", "mathfemm.m"), scan_mathfemm),
    ("juliafemm", os.path.join(ROOT, "juliafemm", "juliafemm.jl"), scan_julia),
    ("scifemm", os.path.join(ROOT, "scifemm", "scifemm.sci"), scan_scilab),
    ("pyfemm", os.path.join(ROOT, "pyfemm", "femm.py"), scan_python),
]


def main():
    total = 0
    for label, path, fn in TARGETS:
        if not os.path.exists(path):
            print("%-11s (not found)" % label)
            continue
        hits, nscanned = fn(path)
        total += len(hits)
        # nscanned matters: a regex that silently matches nothing would
        # otherwise report a clean result. 0 scanned is a broken scanner.
        print("%-11s %4d definitions scanned, %d shadowed"
              % (label, nscanned, len(hits)))
        for name, sig, first, second in hits:
            print("    %-26s line %d shadows line %d   %s"
                  % (name, second, first, sig))
    # octavefemm is one function per file, so a definition cannot be shadowed
    # by a later one; nothing to scan.
    print("")
    print("octavefemm  n/a -- one function per file, so no in-file shadowing")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
