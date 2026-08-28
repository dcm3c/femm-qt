"""Diff what every binding emits for the same logical call.

    python3 compare_emissions.py

Reads contract.tsv and every emitted_<binding>.tsv present, and reports where
the bindings disagree. octavefemm is the reference, because it is the wire-token
authority the other four were derived from.

The comparison is SEMANTIC, not textual, and it has to be: for the same
argument the five bindings legitimately emit `1`, `1.0`, `1-I*0.25`,
`(1-0.25*I)`, `complex(1,-0.25)` and `Complex(1.,-0.25)`. All six are the same
number to the interpreter. What matters is the command name, the argument
count, and the argument VALUES -- which is precisely the level at which the
num.m bug (`1+2i`, which the interpreter rejects outright) shows up.

Exit status is 1 if anything unexplained differs, so this can gate a build.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REFERENCE = "octavefemm"
PASSES = ("int", "real", "cplx")
TOL = 1e-9

CMD = re.compile(r"^\s*(\w+)\s*\((.*)\)\s*$", re.S)


def split_args(s):
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


def value(tok):
    """One argument as a Python value: str for a quoted string, complex for a
    number in any of the six spellings the bindings produce."""
    tok = tok.strip()
    if len(tok) >= 2 and tok[0] == '"' and tok[-1] == '"':
        return tok[1:-1]
    expr = tok
    # Mathematica's CForm spells a complex Complex(re,im); Scilab uses
    # complex(re,im); everyone else writes it out with an I.
    expr = re.sub(r"\bComplex\(", "complex(", expr)
    # A bare I is the imaginary unit on the wire. Guard against touching the
    # 'I' inside an identifier, though arguments here are always numeric.
    expr = re.sub(r"(?<![A-Za-z0-9_])I(?![A-Za-z0-9_])", "1j", expr)
    try:
        v = eval(expr, {"__builtins__": {}}, {"complex": complex})  # noqa: S307
    except Exception:                                    # noqa: BLE001
        return ("UNPARSED", tok)
    return complex(v)


def parse(emission):
    """One emitted command -> (name, [values]), or a marker tuple."""
    if emission.startswith("MISSING"):
        return ("<missing>", [])
    if emission.startswith("ERROR:"):
        return ("<error>", [emission[6:]])
    m = CMD.match(emission)
    if not m:
        return ("<unparsed>", [emission])
    args = [a for a in split_args(m.group(2)) if a != ""]
    return (m.group(1), [value(a) for a in args])


def same(a, b):
    if isinstance(a, str) or isinstance(b, str):
        return a == b
    if isinstance(a, tuple) or isinstance(b, tuple):
        return a == b
    scale = max(1.0, abs(a), abs(b))
    return abs(a - b) <= TOL * scale


def load(binding):
    path = os.path.join(HERE, "emitted_%s.tsv" % binding)
    if not os.path.exists(path):
        return None
    seq = {}
    for line in io.open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3:
            continue
        seq.setdefault((parts[0], parts[1]), []).append(parts[2])
    return seq


def load_allowed():
    path = os.path.join(HERE, "known_divergence.tsv")
    allowed = {}
    if os.path.exists(path):
        for line in io.open(path, encoding="utf-8"):
            if line.startswith("#") or not line.strip():
                continue
            p = line.rstrip("\n").split("\t")
            if len(p) >= 3:
                allowed[(p[0], p[1])] = p[2]
    return allowed


def main():
    bindings = ["octavefemm", "pyfemm", "scifemm", "juliafemm", "mathfemm"]
    data = {b: load(b) for b in bindings}
    present = [b for b in bindings if data[b] is not None]
    absent = [b for b in bindings if data[b] is None]
    if REFERENCE not in present:
        print("no emitted_%s.tsv -- the reference is required" % REFERENCE)
        return 2
    allowed = load_allowed()

    print("bindings compared : %s" % ", ".join(present))
    if absent:
        print("not recorded      : %s  (host not installed, or not run)"
              % ", ".join(absent))
    print("")

    ref = data[REFERENCE]
    findings = []
    counts = {b: dict(same=0, missing=0, error=0, differ=0) for b in present}

    for (wrapper, which) in sorted(ref):
        if which not in PASSES:
            continue
        rseq = [parse(e) for e in ref[(wrapper, which)]]
        for b in present:
            if b == REFERENCE:
                continue
            got = data[b].get((wrapper, which))
            if got is None:
                got = data[b].get((wrapper, "int"))
                if got == ["MISSING"]:
                    if which == "int":
                        counts[b]["missing"] += 1
                    continue
                if got is None:
                    if which == "int":
                        counts[b]["missing"] += 1
                        findings.append((wrapper, which, b,
                                         "not recorded by this binding"))
                    continue
            gseq = [parse(e) for e in got]
            # The allowlist is consulted BEFORE the error branch. A wrapper
            # documented as divergent for a reason that also makes it refuse the
            # contract -- *_getview needs a bitmap the stub never writes, the
            # array wrappers cannot be called with scalar sentinels -- would
            # otherwise be reported anyway, and its entry would look ineffective.
            key = (wrapper, b)
            if key in allowed or (wrapper, "*") in allowed:
                counts[b]["same"] += 1
                continue
            if gseq and gseq[0][0] == "<error>":
                counts[b]["error"] += 1
                findings.append((wrapper, which, b, "ERROR " + gseq[0][1][0]))
                continue
            if len(gseq) != len(rseq):
                counts[b]["differ"] += 1
                findings.append((wrapper, which, b,
                                 "emits %d command(s), %s emits %d"
                                 % (len(gseq), REFERENCE, len(rseq))))
                continue
            bad = None
            for (rn, ra), (gn, ga) in zip(rseq, gseq):
                if rn != gn:
                    bad = "command %s vs %s" % (gn, rn)
                    break
                if len(ra) != len(ga):
                    bad = "%d args vs %d" % (len(ga), len(ra))
                    break
                for i, (x, y) in enumerate(zip(ra, ga), 1):
                    if not same(x, y):
                        bad = "arg %d = %r, expected %r" % (i, y, x)
                        break
                if bad:
                    break
            if bad:
                counts[b]["differ"] += 1
                findings.append((wrapper, which, b, bad))
            else:
                counts[b]["same"] += 1

    print("%-12s %8s %8s %8s %8s" % ("binding", "agree", "differ", "missing",
                                     "error"))
    for b in present:
        if b == REFERENCE:
            print("%-12s %8s   (reference)" % (b, len(ref) // 3))
            continue
        c = counts[b]
        print("%-12s %8d %8d %8d %8d"
              % (b, c["same"], c["differ"], c["missing"], c["error"]))

    if findings:
        # Category counts over ALL findings, not just the ones printed below.
        # The printed list is capped, and aggregating from the capped output is
        # how a fault in all four physics was first reported as one physics.
        def bucket(why):
            if why.startswith("ERROR"):
                return "wrapper refused the contract (guarded before emitting)"
            if why.startswith("not recorded"):
                return "wrapper absent from this binding"
            if "<missing>" in why:
                # The wrapper does not exist in this binding. Reporting that as
                # a "different command name" put 37 coverage gaps in the same
                # bucket as one genuinely wrong command, which is exactly the
                # kind of miscategorisation that hides a real finding.
                return "wrapper absent from this binding"
            if why.startswith("command "):
                return "different command name"
            if why.startswith("arg ") and "expected" in why:
                return "different argument value"
            if "args vs" in why:
                return "different argument count"
            if "command(s)" in why:
                return "different number of commands emitted"
            return "other"

        cats = {}
        for _, _, b, why in findings:
            cats[(b, bucket(why))] = cats.get((b, bucket(why)), 0) + 1
        print("")
        print("--- all %d disagreement(s) by category ---" % len(findings))
        for (b, cat), n in sorted(cats.items(), key=lambda kv: -kv[1]):
            print("  %5d  %-11s %s" % (n, b, cat))
        # A substring filter for triage: `compare_emissions.py expected` lists
        # every argument-value difference, `... command` every wrong command.
        # Without it the detail print is capped and a fault spanning four
        # physics can read as one, which happened.
        filt = sys.argv[1] if len(sys.argv) > 1 else None
        shown = [f for f in findings if filt is None or filt in f[3]]
        cap = len(shown) if filt else 60
        print("")
        print("--- %s%d in detail ---"
              % ("" if filt else "first ", min(cap, len(shown))))
        for wrapper, which, b, why in shown[:cap]:
            print("  %-26s %-5s %-11s %s" % (wrapper, which, b, why))
        if len(shown) > cap:
            print("  ... and %d more (pass a substring to filter)"
                  % (len(shown) - cap))
    else:
        print("")
        print("every binding agrees on every wrapper.")

    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
