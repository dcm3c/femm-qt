"""Turn a gc_soak.py run into a verdict.

    python3 gc_soak_report.py rss.csv [gc_soak_phases.txt]

Reads the phase log written by testing/gc_soak.py and a memory trace sampled
from OUTSIDE the process (see gc_soak.py's header for the PowerShell and
POSIX samplers), lines them up, and reports each allocate/free cycle as

    before-plateau -> peak -> after-plateau

The bar (docs/bdwgc-handoff-plan.md 6.3, as corrected in 11.4a) is that the
after-plateau is within 20% of the before-plateau.

Two things this knows that a naive reading does not:

  * THE FIRST CYCLE IS EXCLUDED. It always reads about x3.5, because the
    process is reaching its working set from a cold start and cannot return
    below a baseline it had not yet touched. Judging it as a failure is wrong.
  * A plateau is measured over the SECOND HALF of its hold window. The first
    second or so after an allocation or a collect is still settling.

CSV: "ms,mem[,mem2]" with a header line. Units are detected -- ps -o rss=
gives kB, the PowerShell sampler above gives MB.
"""
import io
import sys

HOLD_MS = 5000          # gc_soak.py's HOLD, in ms
SETTLE = 1200           # ignore this much of each plateau


def load(csv_path, phases_path):
    rows = []
    for i, ln in enumerate(io.open(csv_path)):
        parts = ln.strip().split(",")
        if i == 0 and not parts[0].lstrip("-").isdigit():
            continue                                  # header
        if len(parts) < 2:
            continue
        rows.append((int(float(parts[0])), float(parts[1])))
    phases = []
    for ln in io.open(phases_path):
        ms, _, rest = ln.strip().partition(" ")
        if ms.lstrip("-").isdigit():
            phases.append((int(ms), rest))
    return rows, phases


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    csv_path = argv[1]
    phases_path = argv[2] if len(argv) > 2 else "gc_soak_phases.txt"
    rows, phases = load(csv_path, phases_path)
    if not rows or not phases:
        print("nothing to read (%d samples, %d phases)" % (len(rows), len(phases)))
        return 2

    # kB or MB? ps gives kB, so a few hundred MB reads as a few hundred thousand.
    unit = "MB"
    scale = 1.0
    if max(v for _, v in rows) > 20000:
        scale = 1.0 / 1024.0
        unit = "MB (from kB)"

    off = rows[0][0] - phases[0][0]

    def plateau(t):
        v = [v for (m, v) in rows if t + off + SETTLE <= m <= t + off + HOLD_MS - 400]
        return sum(v) / len(v) * scale if v else None

    print("%-18s %9s %9s %9s   %s" % ("cycle", "before", "peak", "after", "ratio"))
    print("-" * 60)
    fails, first = [], True
    for t, name in phases:
        if not name.endswith("rest_before"):
            continue
        tag = name.split()[0]
        live = next((x for x, n in phases if n.startswith(tag + " live")), None)
        freed = next((x for x, n in phases if n.startswith(tag + " freed")), None)
        if live is None or freed is None:
            continue
        b, a = plateau(t), plateau(freed)
        pk = max((v for (m, v) in rows if live + off <= m <= freed + off), default=0) * scale
        if b is None or a is None or b == 0:
            continue
        note = ""
        if first:
            note = "   (cold start, excluded)"
            first = False
        elif a / b > 1.20:
            note = "   OVER"
            fails.append(tag)
        print("%-18s %9.1f %9.1f %9.1f   x%.2f%s" % (tag, b, pk, a, a / b, note))

    print("\nmemory in %s   first sample %.1f, peak %.1f, last %.1f"
          % (unit, rows[0][1] * scale,
             max(v for _, v in rows) * scale, rows[-1][1] * scale))
    if fails:
        print("VERDICT: FAIL -- did not return within 20%% on: %s" % ", ".join(fails))
        return 1
    print("VERDICT: PASS -- every steady-state cycle returned within 20%")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
