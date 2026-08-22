# octaveFEMM for femm-qt

MATLAB/Octave interface to femm-qt using TCP IPC. Drop-in replacement
for the original octaveFEMM — same function names, no ActiveX or
file-link dependency. Requires MATLAB R2014b+ (for `tcpclient`).

## Installing

There is nothing to build or compile -- octaveFEMM is a directory of `.m`
files. Put that directory on the MATLAB or Octave path:

```matlab
addpath('C:\path\to\femm-qt\octavefemm')
```

**The directory is not called the same thing everywhere**, which is the one
thing likely to trip you up:

| where | path to add |
|---|---|
| source checkout | `femm-qt/octavefemm` |
| Windows install | `c:\femm-qt\mfiles` |
| macOS app bundle | `/Applications/femm_gui.app/Contents/Resources/bindings/mfiles` |

The installers rename it to `mfiles`; only the source tree calls it
`octavefemm`.

**To make it stick** across sessions, rather than retyping `addpath` every
time:

```matlab
addpath('...')
savepath                 % MATLAB, and Octave
```

In Octave you can instead put the `addpath` line in `~/.octaverc` (or
`%USERPROFILE%\.octaverc` on Windows), which is often easier to keep track of
than a saved path.

Check it took:

```matlab
which openfemm
```

## Setup

Set `FEMM_DIR` to the directory containing `femm_gui` and `femm_headless`:

```matlab
setenv('FEMM_DIR', 'C:\path\to\femm-qt\build\bin')
```

On macOS an installed `.app` has no `bin/`; point at the bundle instead:

```bash
export FEMM_DIR=/Applications/femm_gui.app/Contents/MacOS
```

The binding files themselves ship **inside** the bundle on macOS, since a
distributed `.app` has no sibling folders to put them in:

```
/Applications/femm_gui.app/Contents/Resources/bindings/mfiles
```

The examples are alongside them, in `Contents/Resources/examples/` -- but those
are read-only **masters**. Use the `Examples` folder the DMG carries next to the
app: drag it somewhere of your own and work there. Example scripts need that,
not just convenience: the FEMM idiom is `open("model.fem")` followed by
`mi_saveas("temp.fem")` so the original is never modified, and both paths are
relative to the script's own directory. Inside the read-only bundle the save
fails and the script stops.

## Single Instance (backward compatible)

Works exactly like original octaveFEMM:

```matlab
openfemm()
newdocument(0)
mi_addnode(0, 0)
mi_addnode(1, 0)
mi_addsegment(0, 0, 1, 0)
mi_saveas('test.fem')
mi_analyze()
mi_loadsolution()
val = mo_getpointvalues(0.5, 0);
closefemm()
```

## Headless Mode

For batch scripting without a GUI window:

```matlab
openfemm(1)             % launch femm_headless
newdocument(0)
% ... build model, solve, extract results ...
closefemm()
```

## Multi-Instance

Run multiple FEMM instances on different ports. Use `femmselect` to
switch which instance receives commands:

```matlab
openfemm(0, 48015)      % GUI on port 48015
openfemm(1, 48016)      % headless on port 48016

femmselect(48015)        % commands go to first instance
newdocument(0)
mi_addnode(0, 0)
mi_analyze()

femmselect(48016)        % switch to second instance
newdocument(0)
mi_addnode(1, 1)
mi_analyze()

femmselect(48015)        % switch back
val1 = mo_getpointvalues(0, 0);

femmselect(48016)
val2 = mo_getpointvalues(1, 1);

closefemm(48015)         % close specific instance by port
closefemm()              % close current instance
```

The most recently opened or selected instance is the active one.
All `mi_*`, `mo_*`, and other commands go to whichever instance
is currently active.

## openfemm Arguments

```
openfemm()              % GUI, default port 48015
openfemm(0)             % GUI, default port 48015
openfemm(1)             % headless, default port 48015
openfemm(0, port)       % GUI on specified port
openfemm(1, port)       % headless on specified port
```

If an instance is already running on the specified port, `openfemm`
connects to it without launching a new process.

## Command Line

To launch femm-qt on a specific port from the command line:

```
femm_gui --port 48016
femm_headless --port 48016
```

## Available Functions

All `mi_*` (magnetics preprocessor) and `mo_*` (magnetics postprocessor)
functions from original octaveFEMM are available as individual .m files.

Core functions: `openfemm`, `closefemm`, `femmselect`, `callfemm`,
`callfemm_noeval`, `newdocument`.

Utilities: `num`, `numc`, `quote`, `quotec`, `AWG`, `IEC`.
