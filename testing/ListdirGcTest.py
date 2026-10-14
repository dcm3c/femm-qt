# ListdirGcTest.py -- os.listdir must survive a garbage collection mid-build.
#
# The C++ -> Python list converter (libfemm/py_bind.h, caster<std::vector<T>>)
# used to convert every element into a std::vector<mp_obj_t> and only then
# build the list. Each element is a new string object; the collector (bdwgc)
# scans the stack, registers and static data but NEVER malloc'd memory, so a
# collection triggered by element k freed elements 0..k-1 while the vector
# still pointed at them. os.listdir is the call that reaches it with one
# allocation per file name.
#
# A big directory of long names gives the loop plenty of allocations, and
# churning garbage between calls keeps the collector busy. Every listing is
# compared with the names actually written: a freed-and-reused string shows
# up as a name that was never created, a wrong length, or a crash.
#
# Run:  femm_headless -run ListdirGcTest.py
import os, gc

N = 3000
ROUNDS = 40
base = os.getcwd() + "/_listdir_gc_tmp"

def rmtree(d):
    try:
        for f in os.listdir(d):
            os.remove(d + "/" + f)
        os.rmdir(d)
    except OSError:
        pass

rmtree(base)
os.mkdir(base)
expected = set()
for i in range(N):
    name = "file_%05d_%s.txt" % (i, "x" * (i % 40 + 20))
    expected.add(name)
    open(base + "/" + name, "w").close()

fails = []
try:
    for r in range(ROUNDS):
        junk = [str(j) * 7 for j in range(2000)]   # garbage, so collections keep coming
        del junk
        got = os.listdir(base)
        if len(got) != N:
            fails.append("round %d: %d names, want %d" % (r, len(got), N))
            break
        bad = [g for g in got if g not in expected]
        if bad:
            fails.append("round %d: %d names that were never created, e.g. %r"
                         % (r, len(bad), bad[0][:60]))
            break
        if set(got) != expected:
            fails.append("round %d: the set of names differs" % r)
            break
finally:
    rmtree(base)

if fails:
    for m in fails:
        print("   " + m)
    print("ListdirGc: %d FAILED" % len(fails))
else:
    print("ListdirGc: %d listings of %d files each came back intact while "
          "the collector ran" % (ROUNDS, N))
