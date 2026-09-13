# Collector regression suite for the bdwgc-backed MicroPython heap
# (micropython/femm_gc.c; docs/bdwgc-handoff-plan.md). Runs inside femm_headless:
#
#     build/bin/femm_headless -run testing/GcTest.py
#
# Every case here is something the previous py/gc.c configuration got wrong or
# did slowly, so a green run is the evidence that the replacement is doing its
# job; a red one names the regression. Thresholds are loose (3-10x the measured
# value) so a loaded CI machine passes; the point is the order of magnitude.

import gc, time
from ulab import numpy as np

fails = []


def check(label, cond, detail=""):
    if not cond:
        fails.append("%s%s" % (label, (": " + str(detail)) if detail else ""))


def ms(f, *a):
    t0 = time.ticks_us()
    r = f(*a)
    return (time.ticks_us() - t0) / 1e3, r


# --- 1. Large single allocations are bounded by RAM, not by a split cap -------
# py/gc.c with gc_get_max_new_split() = 64 MB raised MemoryError above ~63 MB.
for n in (9_000_000, 30_000_000):
    try:
        a = np.zeros(n)
        check("zeros(%d) shape" % n, a.shape == (n,))
        del a
    except MemoryError:
        check("zeros(%d) allocates" % n, False, "MemoryError")
gc.collect()

# --- 2. Views and frombuffer results keep their storage alive -----------------
def make_view():
    base = np.linspace(0.0, 1.0, 1_000_000)
    return base[::2][10:20]           # base goes out of scope here
v = make_view()
gc.collect(); gc.collect()
check("view outlives base", abs(float(np.sum(v)) - 0.000290) < 1e-6, float(np.sum(v)))

def make_fb():
    raw = bytearray(8 + 5 * 8)
    b = np.frombuffer(raw, dtype=np.float, offset=8)
    for i in range(5):
        b[i] = i * 1.5
    return b                          # raw goes out of scope; b aliases raw+8
fb = make_fb()
gc.collect(); gc.collect()
check("frombuffer(offset) outlives source", list(fb) == [0.0, 1.5, 3.0, 4.5, 6.0], list(fb))

# --- 3. Numeric payloads are not scanned: no false retention -------------------
# 500 int32 pairs that spell the addresses of 500 garbage objects. Under
# conservative scanning of the payload this pinned ~520 KB; atomic storage pins
# nothing (the residue is stack conservativeness, well under 100 KB).
PROBE_BLOCKS = 20000
PROBE_BYTES = 1000
PROBE_KB = PROBE_BLOCKS * PROBE_BYTES // 1024   # what the probe itself allocates


def retention_probe(pin):
    gc.collect()
    base = gc.mem_alloc()
    big = [bytearray(PROBE_BYTES) for _ in range(PROBE_BLOCKS)]
    idx = np.zeros((500, 2), dtype=np.int32)
    if pin:
        for r in range(500):
            a = id(big[r * 40])
            idx[r, 0] = a & 0xffffffff
            idx[r, 1] = (a >> 32) & 0xffffffff
    del big
    gc.collect()
    return (gc.mem_alloc() - base) // 1024, idx
# gc.mem_alloc() is not a stable baseline across a large free under bdwgc: the
# 240 MB array from section 1 comes off the heap at whatever collection Boehm
# gets round to, which can be one INSIDE a probe -- so the probe's base was
# taken with it counted and its final reading without, and the delta comes back
# at about -234000 KB. On Ubuntu that made this check fail 3 times in 10 with
# "16 KB retained vs -234360 KB control", where 16 KB is the healthy answer and
# the control is the nonsense.
#
# The same race can HIDE a regression rather than invent one -- land the release
# in the second probe instead and r1 - r0 goes hugely negative, which passes.
# So both readings are required to be sane before the comparison means anything,
# and a warm-up runs first to take the settling out of the measured pair. The
# 100 KB threshold below is untouched; it was never the problem.
# A reading is INVALID, rather than merely small, when it is more negative than
# the probe's own allocation: a probe cannot release memory it never allocated,
# so anything past -PROBE_KB is the baseline moving underneath it, not a
# measurement. A few KB either side of zero is ordinary noise and stays valid --
# an earlier version of this guard demanded r >= 0 and rejected a healthy -4 KB.
def sane(r):
    return r >= -PROBE_KB


retention_probe(False)                    # warm-up, discarded
for _attempt in range(4):
    r0, _ = retention_probe(False)
    r1, keep = retention_probe(True)
    if sane(r0) and sane(r1):
        break                             # neither reading straddled a release
check("int32 pairs do not pin garbage",
      sane(r0) and sane(r1) and r1 - r0 < 100,
      "%d KB retained vs %d KB control" % (r1, r0))
del keep

# --- 4. Multi-block churn is not quadratic --------------------------------------
# py/gc.c advanced its free-block hint only after a 1-block allocation, so a
# loop allocating only tuples rescanned the allocation table every time:
# 2.5 s for 400k iterations, vs 41 ms with one float allocated per iteration.
def churn(n, mix):
    s = 0
    for i in range(n):
        t = (i, i + 1, i + 2)
        if mix:
            f = i * 1.0
        s += t[1]
    return s
t_plain, _ = ms(churn, 400_000, False)
t_mixed, _ = ms(churn, 400_000, True)
check("tuple-only churn 400k < 500 ms", t_plain < 500, "%.0f ms" % t_plain)
check("tuple-only vs mixed within 3x", t_plain < 3 * t_mixed + 20, "%.0f vs %.0f ms" % (t_plain, t_mixed))

def bagrow():
    ba = bytearray()
    for i in range(200_000):
        ba += b"0123456789abcdef"
    return len(ba)
t_ba, n_ba = ms(bagrow)
check("bytearray += x200k < 1000 ms", t_ba < 1000 and n_ba == 3_200_000, "%.0f ms" % t_ba)

# --- 5. Collection cost does not scale with bulk numeric data ------------------
big = np.zeros(30_000_000)           # 240 MB float64
t_gc, _ = ms(gc.collect)
check("gc.collect() with 240 MB array live < 50 ms", t_gc < 50, "%.1f ms" % t_gc)
del big
gc.collect()

# --- 6. Heap shrinks back after bulk garbage ------------------------------------
gc.collect(); gc.collect()
before = gc.mem_alloc()
junk = [(i, i + 1, i + 2) for i in range(1_000_000)]
del junk
gc.collect(); gc.collect()
after = gc.mem_alloc()
check("mem_alloc returns after 1M-tuple list", after - before < 1_000_000, "+%d bytes" % (after - before))

# --- 7. A dropped file object is finalised by a collection --------------------
# MEASURED, and NOT what the handoff plan predicted. It expected the drain in
# gc_alloc/the VM hook to close a dropped file "within a few allocations"; it
# does not, and cannot. bdwgc only QUEUES a finalisable object when a
# collection finds it unreachable, and with free_space_divisor(2) ("grow rather
# than collect") 20k small allocations trigger no collection at all -- so there
# is nothing to drain and the file stays open.
#
# Note what does NOT prove a finaliser ran: reading the content back.
# vfs_posix_file writes straight to the fd, so the bytes are on disk whether or
# not anything was finalised; the original form of this case would have passed
# with finalisers completely broken. os.remove() is the real probe -- Windows
# refuses to unlink a file that still has an open handle.
import os
tmp = "GcTest_finaliser.tmp"
def write_and_drop():
    f = open(tmp, "w")
    f.write("finalised" + chr(10))
    # no close(); the reference dies on return
write_and_drop()

with open(tmp) as f:
    got = f.read()
check("dropped file reached disk", got == "finalised" + chr(10), repr(got))

churn(20_000, True)                  # allocate, but do not collect
try:
    os.remove(tmp)                   # succeeds only if something already closed it
except OSError:
    gc.collect()                     # drain point 1 -- this is what closes it
    try:
        os.remove(tmp)
        check("gc.collect() runs the finaliser and closes the file", True)
    except OSError as e:
        check("gc.collect() runs the finaliser and closes the file", False, e)

# --- 8. Introspection entry points still answer --------------------------------
# (micropython.mem_info is not in the CORE ROM level; gc.* is what scripts use.)
check("gc.mem_alloc int", isinstance(gc.mem_alloc(), int))
check("gc.mem_free int", isinstance(gc.mem_free(), int))

# --------------------------------------------------------------------------------
if fails:
    print("GcTest: %d FAILURE(S)" % len(fails))
    for f in fails:
        print("  " + f)
    raise SystemExit(1)
print("GcTest: all checks passed")
