# Solver wall-clock on shipped models, for comparing two machines or two builds.
#
# Put this file in the femm-qt directory (beside examples/) and run:
#
#     build/bin/femm_headless -run bench_solve.py          (Linux/macOS)
#     build\bin\femm_headless.exe -run bench_solve.py      (Windows)
#
# -run chdirs to the script's own directory, so the relative paths below
# resolve on any platform with no editing.
#
# Times mi_analyze(1), which is meshing + solve + writing the .ans -- the whole
# thing a user waits for, not just the linear algebra.  Mesh size is printed
# alongside, because a different mesh is a different problem rather than a
# different speed: compare rows only where the node counts match.
import time

MODELS = [
    "../examples/ProximityExact.fem",
    "../examples/CoggingTorque.fem",
    "../examples/coil.fem",
    "../examples/my_SPM_motor_AGB.fem",
]


def main():
    print("%-26s %9s %10s %10s" % ("model", "nodes", "elements", "analyze"))
    for rel in MODELS:
        try:
            open(rel)
        except Exception as e:
            print("%-26s  skipped: %s" % (rel, e))
            continue
        t0 = time.ticks_ms()
        try:
            mi_analyze(1)
        except Exception as e:
            print("%-26s  failed: %s" % (rel, e))
            continue
        dt = time.ticks_diff(time.ticks_ms(), t0)
        mi_loadsolution()
        nn, ne = int(mo_numnodes()), int(mo_numelements())
        mo_close()
        print("%-26s %9d %10d %7d ms" % (rel.split("/")[-1], nn, ne, dt))


main()
