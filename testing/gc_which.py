# Which collector is in this binary?  A platform bring-up check.
#
#     build/bin/femm_headless -run testing/gc_which.py
#
# Worth running FIRST on a new platform, because the failure it catches is
# invisible: if CMake ever configures -DFEMM_GC_BDWGC=OFF, or the py/gc.c
# exclusion stops matching after a MicroPython bump, you get a working FEMM
# built on the old block allocator. Every ordinary script -- including heavy
# ones like examples/mySPMLossScriptAGB.py -- runs identically either way,
# because nothing they do exceeds what the old allocator could serve.
#
# So this asks the two questions that DO separate them, and prints a verdict.
# Exits 1 if the answer is "the fallback", so it can gate a script. (Via
# `raise SystemExit`, not sys.exit -- this build's sys has no exit attribute
# at all, and calling it fails with an AttributeError that looks like a real
# script bug.)
import gc, time
from ulab import numpy as np

facts = []
verdict_bdwgc = True

# 1. Idle heap. py/gc.c starts on the 2 MB static s_heap in py_state.cpp and
#    MICROPY_GC_SPLIT_HEAP_AUTO has already malloc'd more segments by the time a
#    script runs -- measured 67 MB. bdwgc starts near nothing and grows only on
#    demand. Indicative rather than decisive; the array below is the proof.
idle = gc.mem_alloc() + gc.mem_free()
facts.append("idle heap total      %8.0f KB   (bdwgc ~250 KB; the block "
             "allocator is already tens of MB by here)" % (idle / 1024.0))
if idle > 1024 * 1024:
    verdict_bdwgc = False

# 2. A single allocation above the old per-split cap. gc_get_max_new_split()
#    capped each new heap segment at 64 MB, so 240 MB in one array was a
#    MemoryError no matter how much RAM the machine had. This is the decisive
#    one: it is chosen to sit above that ceiling on purpose.
try:
    t = time.ticks_ms()
    a = np.zeros(30000000)                       # 240 MB
    dt = time.ticks_diff(time.ticks_ms(), t)
    facts.append("np.zeros(30_000_000) %8d ms   (block allocator: MemoryError)" % dt)
    # 3. And it must be cheap to collect, because the payload is allocated
    #    pointer-free (atomic) and is never scanned.
    t = time.ticks_ms()
    gc.collect()
    facts.append("gc.collect() with it live %3d ms   (scanning it would cost ~10x)"
                 % time.ticks_diff(time.ticks_ms(), t))
    a = None
except MemoryError:
    facts.append("np.zeros(30_000_000)     MemoryError  <-- the 64 MB split cap")
    verdict_bdwgc = False

for f in facts:
    print(f)
print("")
if verdict_bdwgc:
    print("VERDICT: bdwgc (third_party/bdwgc via micropython/femm_gc.c)")
else:
    print("VERDICT: MicroPython's block allocator (py/gc.c) -- NOT bdwgc.")
    print("         Check that FEMM_GC_BDWGC is ON and that CMake's py/gc.c")
    print("         exclusion still matches; see docs/bdwgc-handoff-plan.md.")
    raise SystemExit(1)
