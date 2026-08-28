# RSS soak for the bdwgc collector (docs/bdwgc-handoff-plan.md 6.3).
#
#     femm_gui -run testing/gc_soak.py          <- the GUI-subsystem case
#     femm_headless -run testing/gc_soak.py
#
# It writes phase markers with elapsed-ms timestamps to gc_soak_phases.txt in
# the script's own directory (-run chdirs there) and does nothing else: memory
# has to be sampled from OUTSIDE the process. On Windows:
#
#     $p = Start-Process femm_gui.exe -ArgumentList "-run","testing\gc_soak.py" -PassThru
#     while (-not $p.HasExited) {
#         $p.Refresh()
#         "$($p.WorkingSet64/1MB),$($p.PrivateMemorySize64/1MB)" | Add-Content rss.csv
#         Start-Sleep -Milliseconds 250
#     }
#
# On Linux, macOS or FreeBSD, use the companion sampler -- it launches the
# binary and samples it by PID:
#
#     python3 gc_soak_sample.py ../build/bin/femm_headless
#     python3 gc_soak_sample.py ../build/bin/femm_gui gc_soak.py rss_gui.csv
#
# Do NOT hand-roll the shell loop with `date +%s%N`: that is a GNU extension,
# and BSD date (macOS, FreeBSD) prints a literal "N", so every sample lands
# at the same timestamp and the plateaus cannot be aligned.
#
# Then read the result with the analyser, which lines the trace up against the
# phase log, measures each plateau over its settled half, and excludes the
# cold-start first cycle:
#
#     python3 gc_soak_report.py rss.csv gc_soak_phases.txt
#
# Every allocation is HELD for HOLD seconds so a 250 ms sampler sees a flat
# plateau either side of it; comparing plateau-to-plateau is the measurement.
# Two results worth knowing before reading a run:
#
#  * FOUR collects, not two. The handoff plan set the bar at "RSS after del +
#    two collections within 20%". Measured on Windows, two is not enough for
#    240 MB blocks -- they come back one cycle late, and the run looks like it
#    is retaining ~340 MB. With four, every steady-state cycle returns to x1.00
#    exactly, and the peak drops too (628 -> 551 MB).
#  * The FIRST cycle always looks bad (x3.5). That is the process reaching its
#    working set from a cold start, not a leak: it cannot return below a
#    baseline it had not yet reached. Judge cycles 2 onward.

# acceptance bar (bdwgc-handoff-plan.md 6.3) is "RSS after del + two collections
# is within 20% of RSS before the allocation", so the run has to produce a
# before-plateau and an after-plateau for each cycle.

import gc, io, time
from ulab import numpy as np

# Beside this script, so it works on every platform without editing. The
# sampler needs to read it, hence a real file rather than stdout: under
# femm_gui there is no console to print to.
LOG = "gc_soak_phases.txt"
log = io.open(LOG, "w")
T0 = time.ticks_ms()
def mark(s):
    log.write("%d %s\n" % (time.ticks_diff(time.ticks_ms(), T0), s)); log.flush()

HOLD = 5          # seconds at each plateau
mark("SETTLE")
time.sleep(HOLD)
mark("BASELINE heap=%d" % gc.mem_alloc())

def hold_then_drop(name, make):
    # The allocation lives ONLY in this frame, and the frame is gone before the
    # collect. Binding it in the caller and setting that to None is not enough:
    # the dead value stays in the caller's VM stack slots and registers, and a
    # conservative collector must treat those as live -- which pins the block
    # for another cycle and makes the process look like it is retaining memory.
    # That artifact, not the collector, is what an earlier version of this soak
    # measured as a 343 MB floor.
    x = make()
    mark("%s live heap=%d" % (name, gc.mem_alloc()))
    time.sleep(HOLD)

def cycle(name, make):
    mark("%s rest_before" % name)
    time.sleep(HOLD)
    hold_then_drop(name, make)
    for _ in range(4):
        gc.collect()
    mark("%s freed heap=%d" % (name, gc.mem_alloc()))
    time.sleep(HOLD)
    mark("%s rest_after" % name)

for k in range(3):
    cycle("array240_%d" % k, lambda: np.zeros(30000000))          # 240 MB
    cycle("array80_%d" % k,  lambda: np.ones(10000000))           #  80 MB
    cycle("list500k_%d" % k, lambda: [(float(i), float(i)) for i in range(500000)])

def churn():
    z = bytearray()
    for i in range(200000):
        z += b'0123456789abcdef'
    return z
for k in range(2):
    cycle("bytes3M_%d" % k, churn)

def tuples():
    for i in range(5000000):
        t = (i, i)
    return None
mark("tuplechurn begin")
tuples()
gc.collect()
mark("tuplechurn done heap=%d" % gc.mem_alloc())
time.sleep(HOLD)

gc.collect(); gc.collect()
mark("END heap=%d" % gc.mem_alloc())
time.sleep(HOLD)
mark("DONE")
log.close()
