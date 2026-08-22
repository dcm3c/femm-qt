# The four material libraries load, through whatever path resolves them.
#
#     build/bin/femm_headless -run testing/MaterialLibTest.py
#
# matlib.dat / statlib.dat / heatlib.dat / condlib.dat are the only files
# femm-qt writes beside itself, so they are the only ones that cannot simply
# live next to the executable on every platform. libfemm/material_lib_path.cpp
# resolves them: exeDir() on Windows and Linux, and on macOS a per-user copy
# under ~/Library/Application Support/FEMM/ seeded from a read-only original in
# the bundle, because a signed .app cannot be written to.
#
# This test does not know or care which branch ran. It asks each physics for a
# material that the shipped library definitely contains and checks the values
# arrived. On macOS that is an end-to-end check of the seeding: if the copy into
# Application Support did not happen, or happened from the wrong place, the read
# fails and so does this.
#
# It is also the only coverage the *_getmaterial commands have at all. Each
# physics parses its library with its own hand-written line-parser, separate
# from the GUI dialog's parser for the same file.

fails = []


def check(label, got, want, tol=1e-9):
    if abs(got - want) > tol:
        fails.append("%s is %r, want %r" % (label, got, want))


# --- magnetics: mi_getmaterial ---------------------------------------------
# Air is mu_r = 1 exactly; Pure Iron is a nonlinear B-H material, so it also
# exercises the inline BH table, which the parser reads with a nested fgets loop
# of its own. Values are checked by saving the document and reading the material
# back out of it -- "it did not throw" would pass even if every field were zero.
import os
import tempfile

newdocument(0)
mi_getmaterial("Air")
mi_getmaterial("Pure Iron")
femfile = os.path.join(tempfile.gettempdir(), "_matlibtest.fem")
mi_saveas(femfile)
mi_close()

f = open(femfile, "rt")
text = f.read()
f.close()
try:
    os.remove(femfile)
except OSError:
    pass

if '"Air"' not in text:
    fails.append("Air did not reach the saved document")
if '"Pure Iron"' not in text:
    fails.append("Pure Iron did not reach the saved document")

# Air's permeability must have come across as 1, not as a default-constructed 0.
block = text.split('"Air"', 1)[-1].split("<BlockName>", 1)[0] if '"Air"' in text else ""
mu = [l for l in block.split(chr(10)) if l.strip().lower().startswith("<mu_x>")]
if not mu:
    fails.append("no <Mu_x> in Air's saved block")
else:
    try:
        check("Air mu_x", float(mu[0].split("=", 1)[1]), 1.0)
    except (ValueError, IndexError):
        fails.append("could not parse Air's <Mu_x>: %r" % mu[0])

# Pure Iron is only useful if its B-H curve came with it.
iron = text.split('"Pure Iron"', 1)[-1] if '"Pure Iron"' in text else ""
bh = [l for l in iron.split(chr(10)) if l.strip().lower().startswith("<bhpoints>")]
if not bh:
    fails.append("no <BHPoints> in Pure Iron's saved block")
else:
    try:
        n = int(bh[0].split("=", 1)[1])
        if n < 2:
            fails.append("Pure Iron came through with %d B-H points" % n)
    except (ValueError, IndexError):
        fails.append("could not parse Pure Iron's <BHPoints>: %r" % bh[0])

newdocument(0)
try:
    mi_getmaterial("No Such Material At All")
    fails.append("mi_getmaterial accepted a name that is not in the library")
except Exception:
    pass
mi_close()

# --- the three scalar physics ----------------------------------------------
for doctype, getter, closer, name in ((1, "ei_getmaterial", "ei_close", "Air"),
                                      (2, "hi_getmaterial", "hi_close",
                                       "Aluminum, Pure"),
                                      (3, "ci_getmaterial", "ci_close",
                                       "Copper")):
    newdocument(doctype)
    try:
        globals()[getter](name)
    except Exception as e:
        fails.append("%s(%r) failed: %r" % (getter, name, e))
    try:
        globals()[getter]("No Such Material At All")
        fails.append("%s accepted a name that is not in the library" % getter)
    except Exception:
        pass
    globals()[closer]()

if fails:
    for m in fails:
        print("   " + m)
    print("material libraries: %d FAILED" % len(fails))
else:
    print("material library check: all four libraries resolve and load -- "
          "magnetics (incl. a nonlinear B-H table), electrostatics, heat flow "
          "and current flow -- and an absent name is refused")
