# pyFEMM for femm-qt

Python interface to femm-qt using TCP IPC. Drop-in replacement for the
original pyFEMM — same function names, no COM/ActiveX dependency.

## Installing

Not on PyPI yet. Install from a checkout.

**For development** -- an editable install, so `git pull` is all it takes to
pick up changes:

```
pip install -e pyfemm
```

from the `femm-qt` directory. This is what the maintainer's machine runs.

**Plain install**, if you would rather not have a live link to the source tree:

```
pip install ./pyfemm
```

Either way the module is imported as `femm`, matching original pyFEMM, while
the distribution is named `pyfemm-qt`:

```
python -c "import femm; print(femm.__file__)"
```

**Without pip at all**, copy `pyfemm/femm.py` next to your own script, or into
the directory holding `femm_gui` and `femm_headless`.

> An editable install records an absolute path. If you move or rename the
> femm-qt directory, `import femm` breaks with no obvious clue -- `pip show
> pyfemm-qt` prints the stale *Editable project location*. Re-run the install
> from the new location to fix it.

## Setup

Set `FEMM_DIR` to the directory containing `femm_gui` and `femm_headless`:

```
set FEMM_DIR=C:\path\to\femm-qt\build\bin
```

On macOS an installed `.app` has no `bin/`; point at the bundle instead:

```bash
export FEMM_DIR=/Applications/femm_gui.app/Contents/MacOS
```

The binding files themselves ship **inside** the bundle on macOS, since a
distributed `.app` has no sibling folders to put them in:

```
/Applications/femm_gui.app/Contents/Resources/bindings/pyfemm
```

The examples are alongside them, in `Contents/Resources/examples/` -- but those
are read-only **masters**. Use the `Examples` folder the DMG carries next to the
app: drag it somewhere of your own and work there. Example scripts need that,
not just convenience: the FEMM idiom is `open("model.fem")` followed by
`mi_saveas("temp.fem")` so the original is never modified, and both paths are
relative to the script's own directory. Inside the read-only bundle the save
fails and the script stops.

Or place `femm.py` in the same directory as the executables.

## Single Instance (backward compatible)

Works exactly like original pyFEMM:

```python
from femm import *

openfemm()          # launch GUI, connect on port 48015
newdocument(0)      # new magnetics problem
mi_addnode(0, 0)
mi_addnode(1, 0)
mi_addsegment(0, 0, 1, 0)
mi_saveas('test.fem')
mi_analyze()
mi_loadsolution()
val = mo_getpointvalues(0.5, 0)
closefemm()
```

## Headless Mode

For batch scripting without a GUI window:

```python
from femm import *

openfemm(1)         # launch femm_headless (no GUI)
newdocument(0)
# ... build model, solve, extract results ...
closefemm()
```

## Multi-Instance

Run multiple FEMM instances in parallel on different ports:

```python
from femm import FemmSession

f1 = FemmSession(port=48015)                    # GUI on default port
f2 = FemmSession(port=48016, headless=True)     # headless on 48016

f1.newdocument(0)
f2.newdocument(0)

f1.mi_addnode(0, 0)    # goes to instance 1
f2.mi_addnode(1, 1)    # goes to instance 2

f1.mi_analyze()
f2.mi_analyze()

result1 = f1.mo_getpointvalues(0, 0)
result2 = f2.mo_getpointvalues(1, 1)

f1.close()
f2.close()
```

You can also mix global and object styles:

```python
from femm import *

openfemm()                              # default instance
f2 = FemmSession(port=48016)            # second instance

mi_addnode(0, 0)                        # goes to default
f2.mi_addnode(1, 1)                     # goes to f2
```

## Connecting to an Existing Instance

If femm-qt is already running (e.g. you launched it from the desktop),
`openfemm()` and `FemmSession()` will connect to it without launching
a new process.

To launch on a specific port from the command line:

```
femm_gui --port 48016
femm_headless --port 48016
```

## Available Functions

All `mi_*` (magnetics preprocessor) and `mo_*` (magnetics postprocessor)
functions from original pyFEMM are available, both as module-level
functions and as methods on `FemmSession`.

Helper functions: `mi_drawline`, `mi_drawarc`, `mi_drawrectangle`,
`mi_makeABC`, `AWG`, `newdocument`, `callfemm`.
