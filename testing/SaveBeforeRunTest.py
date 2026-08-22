# Meshing and analysis must refuse when the document cannot be saved.
#
#     build/bin/femm_headless -run testing/SaveBeforeRunTest.py
#
# Both *_createmesh and *_analyze write the document out before running,
# because the mesher and the solvers are separate processes that read it back
# from disk. Until 2026-08-22 both ignored whether that write succeeded, so an
# unwritable file meant the run went ahead against whatever was already there --
# the PREVIOUS version of the model -- and produced results that looked entirely
# ordinary. There is nothing in the output to suggest the geometry on screen was
# not the geometry solved.
#
# Ways to reach it: a read-only file, a full disk, a folder without write
# permission, the file held open by another program. On macOS it is also what
# happens to an example opened from inside the application bundle, which is
# read-only by design.
#
# The unwritable path here is made by deleting the containing directory rather
# than by changing permissions, because MicroPython's os has no chmod -- and it
# exercises the same femm_fopen failure.

import os
import tempfile

fails = []


def vanished_path():
    """A path whose directory no longer exists, so femm_fopen must fail."""
    d = tempfile.mkdtemp()
    return d, d + "/gone"


def check(label, doctype, saveas, mesh, analyze, close):
    """Every callable here must be a THUNK. The per-physics commands do not
    exist until a document of that type is open, so a bare `mi_saveas` in the
    argument list raises "isn't defined" before newdocument() has run."""
    d, base = vanished_path()
    newdocument(doctype)
    # Enough geometry that meshing would have something to do, so a pass here
    # means the save was checked rather than the run failing for its own reasons.
    saveas(base + EXT[doctype])
    os.remove(base + EXT[doctype])
    os.rmdir(d)

    for name, fn in (("createmesh", mesh), ("analyze", analyze)):
        try:
            fn()
            fails.append("%s %s returned normally on an unwritable document; it "
                         "would have run against the previous version"
                         % (label, name))
        except Exception as e:
            msg = str(e)
            if "Could not save" not in msg:
                fails.append("%s %s failed, but not for the right reason: %r"
                             % (label, name, msg[:120]))
    close()


EXT = {0: ".fem", 1: ".fee", 2: ".feh", 3: ".fec"}

check("magnetics", 0, lambda p: mi_saveas(p), lambda: mi_createmesh(), lambda: mi_analyze(1),
      lambda: mi_close())
check("electrostatics", 1, lambda p: ei_saveas(p), lambda: ei_createmesh(),
      lambda: ei_analyze(1), lambda: ei_close())
check("heat flow", 2, lambda p: hi_saveas(p), lambda: hi_createmesh(), lambda: hi_analyze(1),
      lambda: hi_close())
check("current flow", 3, lambda p: ci_saveas(p), lambda: ci_createmesh(),
      lambda: ci_analyze(1), lambda: ci_close())

# And the normal case still works -- a check that refuses everything would pass
# all of the above.
# A CLOSED region, not just a few nodes: mi_createmesh() segfaults on a
# document with no segments (empty, or nodes only), which is a separate bug --
# tangle_mesh_fem dies inside runMeshPipeline rather than returning an error
# code. Two nodes and one segment gets a clean error; a closed square meshes.
d = tempfile.mkdtemp()
newdocument(0)
mi_probdef(0, "inches", "planar", 1e-8, 1, 30)
for x, y in ((0, 0), (1, 0), (1, 1), (0, 1)):
    mi_addnode(x, y)
mi_addsegment(0, 0, 1, 0)
mi_addsegment(1, 0, 1, 1)
mi_addsegment(1, 1, 0, 1)
mi_addsegment(0, 1, 0, 0)
mi_saveas(d + "/ok.fem")
try:
    mi_createmesh()
except Exception as e:
    fails.append("createmesh refused a perfectly writable document: %r" % (str(e)[:120],))
mi_close()

if fails:
    for m in fails:
        print("   " + m)
    print("save-before-run: %d FAILED" % len(fails))
else:
    print("save-before-run check: createmesh and analyze both refuse an "
          "unwritable document in all four physics, naming the file, and still "
          "accept a writable one")
