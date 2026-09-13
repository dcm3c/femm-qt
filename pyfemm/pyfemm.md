# pyFEMM for femm-qt

Python interface to femm-qt using TCP IPC. Drop-in replacement for the
original pyFEMM — same function names, no COM/ActiveX dependency.

## Installing

> **If FEMM 4.2 is also on this machine, read [Alongside FEMM
> 4.2](#alongside-femm-42) first.** Both provide a module named `femm`, so
> installing this one takes over `import femm` for that interpreter and the
> instructions below are the wrong ones for you.

Not on PyPI yet. Install from a checkout.

**For development** -- an editable install, so `git pull` is all it takes to
pick up changes:

```
pip install --user -e pyfemm
```

from the `femm-qt` directory. This is what the maintainer's machine runs.

`--user` because this is not a package yet. Until it is on PyPI it is
somebody's checkout, and a checkout belongs in your own site-packages rather
than in the interpreter's. Drop the flag once it is a real package.

**Plain install**, if you would rather not have a live link to the source tree:

```
pip install --user ./pyfemm
```

> **If pip answers `error: externally-managed-environment`**, that interpreter
> is owned by a package manager -- Homebrew, or a Linux distribution -- and
> PEP 668 stops pip writing to it. `--user` is blocked too, which is the part
> that surprises people. Three ways on, cheapest first:
>
> * **Use the interpreter you actually drive FEMM from.** It is often not the
>   first `python3` on PATH. On macOS, Apple's `/usr/bin/python3` is not
>   externally managed and takes `--user` without complaint:
>
>   ```bash
>   /usr/bin/python3 -m pip install --user -e pyfemm
>   ```
>
> * **A virtual environment**, to keep it sealed off:
>
>   ```bash
>   python3 -m venv ~/.venvs/femm
>   ~/.venvs/femm/bin/pip install -e pyfemm
>   ```
>
> * **`--break-system-packages`**, which does what it says and is a last
>   resort.
>
> Whichever you pick, it must be the SAME interpreter you then run your femm
> scripts with. Installing into one and importing from another is the
> commonest way to get `ModuleNotFoundError: No module named 'femm'` while
> looking at an install that reported success.

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

## Alongside FEMM 4.2

Original FEMM 4.2's pyFEMM and this one **both install a module called
`femm`**. The distributions have different names -- `pyfemm` and `pyfemm-qt`
-- but the import name is what collides, and pip installs both without
complaint. Whichever went in last owns `import femm` for that interpreter, and
uninstalling either can take the other's `femm.py` with it. So on a machine
with both, **do not follow the install instructions above**.

There is no need to install this one at all. `femm.py` is a single file with
no dependencies. Point Python at the directory holding it and import it under
whatever name you like:

```python
import sys
sys.path.insert(0, r"C:\femm-qt\pyfemm")     # the path on YOUR machine
import femm as femm5

femm5.openfemm(1)          # 1 = headless
femm5.newdocument(0)
```

FEMM 4.2's pyFEMM stays installed and untouched; scripts that do not have
those two lines go on getting 4.2.

Directory to use on the second line:

| Windows | `C:\femm-qt\pyfemm` |
|---|---|
| macOS | `/Applications/femm_gui.app/Contents/Resources/bindings/pyfemm` |
| Linux, FreeBSD | `<prefix>/opt/femm-qt/bindings/pyfemm` |

Even simpler if you would rather not edit the script at all: **copy `femm.py`
into the folder your script lives in**. Python searches the script's own
directory first, so that copy wins for that script and for nothing else, and
plain `import femm` -- or `from femm import *` -- keeps working unchanged. A
4.2 script then becomes a 5.0 script by copying one file next to it.

### Which one am I actually importing?

```python
import femm
print(femm.__file__)
```

A path under `femm-qt`, or a copy you made from one, is this binding; anything
under a FEMM 4.2 directory is the original.

### If you need BOTH in one program

`import femm as femm5` is only a local alias: it still registers the module as
`femm`, so a later `import femm` anywhere in that same run returns the 5.0 one
too. That is exactly what you want in a 5.0 script and no use at all if you
are comparing the two versions side by side. For that case, load the file
under a genuinely separate name:

```python
import importlib.util, sys

_s = importlib.util.spec_from_file_location(
    "femm5", r"C:\femm-qt\pyfemm\femm.py")
femm5 = importlib.util.module_from_spec(_s)
sys.modules["femm5"] = femm5
_s.loader.exec_module(femm5)
```

Now `femm5` is femm-qt and `import femm` still reaches 4.2, in the same
program.

### If you have a lot of scripts

A virtual environment per version keeps `import femm` meaning 5.0 inside it
and changes nothing outside, so existing scripts run unedited:

```bash
python3 -m venv ~/.venvs/femm5
~/.venvs/femm5/bin/pip install -e pyfemm     # Windows: ~\.venvs\femm5\Scripts\pip
```

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

**pyFEMM's own examples sit in an `examples` folder beside `femm.py`**, not in
the shared examples directory:

| | |
|---|---|
| Windows | `{app}\pyfemm\examples` |
| macOS | `Contents/Resources/bindings/pyfemm/examples` |
| Linux, FreeBSD | `<prefix>/opt/femm-qt/bindings/pyfemm/examples`, and a copy in `~/Documents/FEMM/examples/pyfemm` |

They are kept apart because there are two incompatible kinds of `.py` here and
no way to tell them apart by name. The shared examples directory holds scripts
for femm-qt's **own** script console, where the commands are globals and a
script opens with `create(3)`; these open with `import femm` and `openfemm()`
and drive femm-qt over TCP from CPython. Two of them even share a filename.
Running one in the other's runtime fails confusingly, so the directory is what
tells you which is which.

Wherever you find them, they are read-only **masters**: copy them somewhere of
your own and work there. That is not merely tidiness -- the FEMM idiom is
`open("model.fem")` followed by `mi_saveas("temp.fem")` so the original is
never modified, and both paths are relative to the script's own directory. In a
read-only location the save fails and the script stops. On macOS the DMG
carries an `Examples` folder next to the app for exactly this; on Linux and
FreeBSD the installer already leaves you a writable copy under
`~/Documents/FEMM`.

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
