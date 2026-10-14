# MaterialLibHistoryTest.py -- the shipped material libraries must be in the
# collector's history table.
#
# libfemm/material_lib_path.cpp offers to replace a user's library copy when it
# is an UNEDITED older shipped library. One of its two tests for "unedited" is
# a table of canonical content hashes of every library ever shipped
# (kKnownShippedLibraries). A revision that forgets to add itself leaves the
# users who fork from it unable to be offered the next one. So: the current
# shipped libraries' hashes must be in the table, and the Python mirror that
# produces those hashes (tools/matlib_canon_hash.py) must agree with the C++
# -- checked indirectly, since the table was written from the mirror and the
# C++ reads the same file the mirror does.
#
# Run:  femm_headless -run MaterialLibHistoryTest.py   (from testing/)
import os, sys

here = os.getcwd()
root = here[:here.rfind("/")] if "/" in here else ".."
sys.path.insert(0, root + "/tools")
import matlib_canon_hash as canon

fails = []

# the table, read straight out of the source so there is one copy of the truth
src = open(root + "/libfemm/material_lib_path.cpp").read()
start = src.find("kKnownShippedLibraries[] = {")
end = src.find("};", start)
if start < 0 or end < 0:
    fails.append("kKnownShippedLibraries not found in material_lib_path.cpp")
    table = set()
else:
    body = src[start:end]
    table = set()
    for tok in body.replace(",", " ").split():
        if tok.endswith("ULL") and tok[:-3].isdigit():
            table.add(int(tok[:-3]))

for name in ("matlib.dat", "statlib.dat", "heatlib.dat", "condlib.dat"):
    path = root + "/matlibs/" + name
    blocks = canon.parse_library(path)
    if not blocks:
        fails.append("%s: parsed no blocks" % name)
        continue
    h = canon.canonical_hash(blocks)
    if h not in table:
        fails.append("%s: canonical hash %d (%d blocks) is not in kKnownShippedLibraries "
                     "-- add it (python3 tools/matlib_canon_hash.py matlibs/%s)" % (name, h, len(blocks), name))

# a re-serialised copy must hash the same: round-trip the parse through a
# re-formatted text and compare
def reformat(blocks):
    out = []
    for b in blocks:
        out.append("<BeginBlock>")
        out.append('<BlockName> = "%s"' % b["name"])
        for k in b["props"]:
            v = b["props"][k]
            try:
                v = repr(float(v))          # different digits, same number
            except ValueError:
                pass
            out.append("<%s> = %s" % (k, v))
        for r in b["rows"]:
            out.append("   " + "\t".join(r.split()))
        out.append("<EndBlock>")
    return "\n".join(out) + "\n"

tmp = here + "/_matlib_history_tmp.dat"
try:
    blocks = canon.parse_library(root + "/matlibs/statlib.dat")
    with open(tmp, "w") as f:
        f.write(reformat(blocks))
    h1 = canon.canonical_hash(blocks)
    h2 = canon.canonical_hash(canon.parse_library(tmp))
    if h1 != h2:
        fails.append("canonical hash is not stable under re-serialisation (%d vs %d)" % (h1, h2))
finally:
    try:
        os.remove(tmp)
    except OSError:
        pass

if fails:
    print("material library history: %d FAILED" % len(fails))
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("material library history: all 4 shipped libraries are in kKnownShippedLibraries, "
      "and the canonical hash survives re-serialisation")
