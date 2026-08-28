"""Compare a phasecurrent driver against the in-process reference.

    python3 PhaseCompare.py reference.txt candidate.txt

This scenario has no Original FEMM golden -- it does not exist upstream -- so
voltage and flux linkage are compared against an in-process run of the same
scenario, on the same engine the binding drives.

The currents are checked differently, and more strongly. mo_getcircuitproperties
echoes the PRESCRIBED current for a current-driven circuit, so the value read
back must equal the complex number the driver sent. That is a round trip through
the binding's argument marshalling AND its reply parser, checked against a
constant rather than against another run -- which is the point, since the bug
this scenario exists for (octavefemm's num.m sending MATLAB's `1+2i`) corrupts
the argument on the way out.

Both files are checked against the prescribed currents, so a reference generated
by a broken engine cannot quietly license a broken binding.
"""
import io
import re
import sys

# What the drivers are required to send. Chosen to sum to exactly zero.
PRESCRIBED = {
    "A": complex(1000, 0),
    "B": complex(-500, -866.0254038),
    "C": complex(-500, 866.0254038),
}
TOL = 2e-2          # same 2% the engine comparers use
CUR_TOL = 1e-9      # the current is an echo, so it should be near-exact
LINE = re.compile(r"^\s*(\S+)\s*=\s*(.+)$")


def parse(path):
    out = {}
    for line in io.open(path, encoding="utf-8", errors="replace"):
        if "|" not in line:
            continue
        m = LINE.match(line.strip())
        if not m:
            continue
        vals = []
        for piece in m.group(2).split("|"):
            n = piece.split()
            if len(n) >= 2:
                try:
                    vals.append(complex(float(n[0]), float(n[1])))
                except ValueError:
                    vals = []
                    break
        if len(vals) == 3:
            out[m.group(1)] = vals
    return out


def close(a, b, tol, scale):
    return abs(a - b) <= tol * max(scale, 1e-30)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    ref, test = parse(sys.argv[1]), parse(sys.argv[2])
    if not test:
        print("no records parsed from %s" % sys.argv[2])
        return 1

    checked, flags = 0, []

    # 1. Every driver, and the reference, must have sent the prescribed current.
    for label, src in (("reference", ref), ("candidate", test)):
        for name, want in PRESCRIBED.items():
            if name not in src:
                flags.append("%s: phase %s missing" % (label, name))
                continue
            got = src[name][0]
            checked += 1
            if not close(got, want, CUR_TOL, abs(want)):
                flags.append("%s: phase %s current came back %s, sent %s"
                             % (label, name, got, want))

    # 2. The three currents must sum to zero -- the physical constraint for a
    #    three-wire line, and a second way to catch a mangled imaginary part.
    for label, src in (("reference", ref), ("candidate", test)):
        if len(src) == 3:
            checked += 1
            tot = sum(v[0] for v in src.values())
            if abs(tot) > 1e-6 * 1000:
                flags.append("%s: currents sum to %s, not zero" % (label, tot))

    # 3. Voltage and flux linkage against the reference.
    for name in sorted(ref):
        if name not in test:
            flags.append("candidate: phase %s missing" % name)
            continue
        for j, what in ((1, "V"), (2, "flux")):
            checked += 1
            a, b = ref[name][j], test[name][j]
            if not close(a, b, TOL, abs(a)):
                flags.append("phase %s %s: ref=%s test=%s" % (name, what, a, b))

    print("compared %d phase-current components: %d agree (err <= %.1f%%), "
          "%d flagged" % (checked, checked - len(flags), TOL * 100, len(flags)))
    if flags:
        print("")
        print("FLAGGED:")
        for f in flags:
            print("  " + f)
    return 1 if flags else 0


if __name__ == "__main__":
    sys.exit(main())
