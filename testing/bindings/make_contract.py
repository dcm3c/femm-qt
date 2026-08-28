"""Turn the octavefemm probe into a contract every binding can be driven from.

    python3 make_contract.py

Reads probe_octavefemm.tsv (written by probe_octavefemm.m) and writes
contract.tsv:

    wrapper <TAB> arity <TAB> kinds

`kinds` is one character per argument position: `s` if that argument reached the
wire quoted, `n` if it reached it as a number. Positions are recovered BY VALUE
rather than by position -- the probe passes i as argument i, so finding `"3"` in
the emitted text means argument 3 is a string no matter where the wrapper put
it. A wrapper that drops an argument entirely leaves it unclassified; those
default to `n`, which is the safe choice (every binding's number formatter
accepts an integer, while its string quoter may not).
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CMD = re.compile(r"^\s*(\w+)\s*\((.*)\)\s*$", re.S)


def split_args(s):
    """Split a command's argument list, respecting double-quoted strings."""
    out, cur, q, depth = [], [], False, 0
    for ch in s:
        if q:
            cur.append(ch)
            if ch == '"':
                q = False
        elif ch == '"':
            q = True
            cur.append(ch)
        elif ch in "([":
            depth += 1
            cur.append(ch)
        elif ch in ")]":
            depth -= 1
            cur.append(ch)
        elif ch == "," and depth == 0:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    tail = "".join(cur).strip()
    if tail or out:
        out.append(tail)
    return out


def main():
    src = os.path.join(HERE, "probe_octavefemm.tsv")
    if not os.path.exists(src):
        print("no probe_octavefemm.tsv -- run probe_octavefemm.m first")
        return 2

    rows = {}          # wrapper -> (arity, [emitted commands])
    for line in io.open(src, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        name, arity, emitted = parts[0], int(parts[1]), parts[2]
        rows.setdefault(name, (arity, []))[1].append(emitted)

    out = io.open(os.path.join(HERE, "contract.tsv"), "w",
                  encoding="utf-8", newline="\n")
    out.write("# wrapper\tarity\tkinds   (s = quoted string, n = number)\n")
    nwrap = nsilent = 0
    unclassified = []
    for name in sorted(rows):
        arity, emissions = rows[name]
        if emissions == ["NOEMIT"]:
            nsilent += 1
            continue
        # Every argument the wrapper actually placed on the wire, across all the
        # commands it emitted (a bulk getter emits several).
        args = []
        for e in emissions:
            m = CMD.match(e)
            if m:
                args.extend(split_args(m.group(2)))
        kinds = []
        missing = []
        for i in range(1, arity + 1):
            if '"%d"' % i in args:
                kinds.append("s")
            elif str(i) in args:
                kinds.append("n")
            else:
                kinds.append("n")          # unused/optional: safe default
                missing.append(i)
        if missing:
            unclassified.append((name, missing))
        out.write("%s\t%d\t%s\n" % (name, arity, "".join(kinds)))
        nwrap += 1
    out.close()

    print("contract.tsv: %d wrappers (%d emitted nothing, excluded)"
          % (nwrap, nsilent))
    if unclassified:
        print("%d wrappers have arguments that never reached the wire; "
              "defaulted to numeric:" % len(unclassified))
        for name, miss in unclassified[:10]:
            print("    %-28s positions %s" % (name, miss))
        if len(unclassified) > 10:
            print("    ... and %d more" % (len(unclassified) - 10))
    return 0


if __name__ == "__main__":
    sys.exit(main())
