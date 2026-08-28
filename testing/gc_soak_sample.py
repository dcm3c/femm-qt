"""Run gc_soak.py and sample the process's memory from outside it.

    python3 gc_soak_sample.py <femm binary> [gc_soak.py] [out.csv]

e.g.

    python3 gc_soak_sample.py ../build/bin/femm_headless
    python3 gc_soak_sample.py ../build/bin/femm_gui gc_soak.py rss_gui.csv
    python3 gc_soak_sample.py \\
        ../build/bin/femm_gui.app/Contents/MacOS/femm_headless      # macOS bundle

Nothing inside the process can read its own RSS, so this launches the binary
and samples it by PID, writing "ms,rss_kb" every 250 ms. Then:

    python3 gc_soak_report.py rss.csv gc_soak_phases.txt

This exists because the obvious shell loop is not portable. `date +%s%N` is a
GNU extension: BSD date -- macOS and FreeBSD -- has no %N and prints a literal
"N", so the elapsed-ms arithmetic silently produces garbage and every sample
lands at the same timestamp. Counting iterations instead drifts, because the
sampling itself costs more than the sleep, and over a three-minute run the
drift is enough to misalign the plateaus against the phase log. Python's clock
avoids both.
"""
import os
import subprocess
import sys
import time

INTERVAL = 0.25


def rss_kb(pid):
    """Resident set size in kB, or None if the process is gone."""
    if os.name == "nt":
        out = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             "(Get-Process -Id %d -ErrorAction SilentlyContinue).WorkingSet64" % pid],
            capture_output=True, text=True).stdout.strip()
        return int(out) // 1024 if out.isdigit() else None
    # POSIX: ps reports kB on Linux, macOS and FreeBSD alike.
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out.isdigit() else None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    exe = argv[1]
    script = argv[2] if len(argv) > 2 else "gc_soak.py"
    out_path = argv[3] if len(argv) > 3 else "rss.csv"
    if not os.path.exists(exe):
        print("no binary at %s" % exe)
        return 2

    print("running %s -run %s, sampling every %d ms" % (exe, script, INTERVAL * 1000))
    proc = subprocess.Popen([exe, "-run", script],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    peak, n = 0, 0
    with open(out_path, "w") as f:
        f.write("ms,rss_kb\n")
        while proc.poll() is None:
            kb = rss_kb(proc.pid)
            if kb is not None:
                ms = int((time.time() - t0) * 1000)
                f.write("%d,%d\n" % (ms, kb))
                f.flush()
                peak = max(peak, kb)
                n += 1
            time.sleep(INTERVAL)

    print("exit code %s after %.0f s; %d samples, peak %.1f MB"
          % (proc.returncode, time.time() - t0, n, peak / 1024.0))
    print("wrote %s -- now: python3 gc_soak_report.py %s gc_soak_phases.txt"
          % (out_path, out_path))
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
