# pyFEMM scripts run in-process — except for 62 names

*Audited 2026-08-23. Deferred to a later cycle; nothing here is started.*

## What already works

`libfemm/py_init_script.cpp` (search for "pyfemm drop-in compatibility") makes a
script written for pyFEMM run unchanged **inside** femm-qt, not only under
CPython through the wire binding:

```python
def openfemm(*a, **k):  pass       # we ARE femm; and closefemm must not exit
def closefemm(*a, **k): pass
_fsys.modules['femm'] = _FemmProxy()   # femm.mi_* -> the in-process commands
```

So `import femm; femm.ei_addnode(...)` works, and so does `from femm import *`
and the bare command form. Verified: `pyfemm/examples/strips.py` runs to
completion under `femm_headless -run` with no modification.

## What does not

Of the 595 functions pyFEMM exposes, **62 do not resolve in-process**.
`pyfemm/examples/demo2.py` is the example that trips over this — it calls
`mo_getb`, and stops with `Script error: mo_getb`.

Nearly all of them are conveniences the *wire binding* defines in Python on top
of commands femm-qt already has. They were never added to the in-process side.

**Point-value accessors — slices of `*_getpointvalues` (24)**

| | |
|---|---|
| `mo_geta getb getconductivity getenergydensity getfill geth getj getmu getpe getph` | indices into `mo_getpointvalues` |
| `eo_getd gete getenergydensity getperm getv` | into `eo_getpointvalues` |
| `ho_getf getg getk gett` | into `ho_getpointvalues` |
| `co_gete getj getk getv` | into `co_getpointvalues` |

For example, `mo_getb(x,y)` is `z = mo_getpointvalues(x,y); return [z[1], z[2]]`.

**Spellings of commands that exist (36)**

| missing | is |
|---|---|
| `mi_ ei_ hi_ ci_ mo_ eo_ ho_ co_ _hidenames()` | `*_shownames(0)` |
| `*_snapgridon() / _snapgridoff()` | `*_gridsnap("on"/"off")` |
| `mo_ eo_ ho_ co_ _smoothon() / _smoothoff()` | `*_smooth("on"/"off")` |
| `mo_ eo_ ho_ co_ _snapgrid(...)` | `*_gridsnap(...)` |
| `mi_setcurrent(name, x)` | `mi_modifycircprop(name, 1, x)` |

**Genuinely absent — REMOVED 2026-08-29**

`*_getnodeindex` and `*_getelementindex` were implemented nowhere in femm-qt —
not in-process and therefore not over the wire either, since the wire server
executes the same command set.

It was **eight** names, not the two this audit first reported: counting pyFEMM's
name list alone missed that scifemm and juliafemm each declared all four
physics, juliafemm `export`ing them. All eighteen declarations are now deleted.

**Provenance, settled by search rather than assumed.** They are in no engine
command, in neither manual, and in no upstream: checked all four legacy pyFEMM
variants in `OneDrive/femm43src` (`pyfemm`, `pyfemm_ctypes`, `pyfemm_dev_013`,
`pyfemm_dev_014` — including the newest 2129-line `femm/__init__.py`), upstream
`octavefemm`, `mathfemm`, `scifemm`, and the FEMM 4.3 engine sources. Zero hits,
against a control of `getpointvalues` that hits in every one of them. Note that
`femm42src/pyfemm/` is only a packaging skeleton — its MANIFEST lists a
`femm/__init__.py` that is not there — so searching THAT tree proves nothing;
femm43src is where the actual binding source lives.

So they were invented during this port. Git agrees: pyfemm's pair arrived in the
first pyfemm commit (`c323b05`, 2026-03-26), and `1900c10` (2026-04-04), *"Add
missing functions to scifemm and juliafemm from pyfemm cross-check"*, propagated
them into two more bindings and from 2 names to 8 — **a consistency sweep that
took one binding as ground truth and copied a phantom outward.**

Why no test caught them: every recorder is driven by `contract.tsv`, which is
derived from octavefemm. octavefemm never had these, so they were never in the
contract and never called, and nothing reports a name present in a binding but
absent from the contract. The suite answers "does every octavefemm wrapper have
a correct counterpart elsewhere", not "does every binding's exported surface
work".

## Fixing the rest

The 60 are Python one-liners and belong in the same compat block in
`py_init_script.cpp`; no C++ binding work is involved. Note the tail: putting
them there makes them ENGINE commands, so `testing/DocDriftTest.py` will then
require an entry for each in the femm-qt manual, which today documents them
only in the octavefemm manual.

## Two traps for whoever audits this again

**The commands do not exist until a document does.** `open` and `newdocument`
are always defined; `mi_*` appears only once a magnetics document exists, and
`mo_*` only after a solution is loaded. An audit that enumerates names before
opening anything concludes that everything is missing — which it will do
convincingly, twice, if you let it.

**Post-processor names need a solved fixture.** The audit that produced the
list above opens `testing/rolled.fem`, `electrostatic1.fee`, `heatFlow1.feh`
and `currentFlow1.fec`, analyses and loads each, and probes each physics in
turn. Probing only magnetics finds 31 of the 62.

To reproduce: extract the names with
`sed -n 's/^def \([a-z][a-z0-9_]*\)(.*/\1/p' pyfemm/femm.py | sort -u`, drop
the helpers (`callfemm`, `doargs`, `num`, `numc`, `quote`, `quotec`,
`smartmesh`), then `eval()` each in-process after opening the right document
and catch `NameError`.

## Related: the pyFEMM examples ship nowhere

`pyfemm/examples/` is the only binding example directory not carried by any
distribution — `octavefemm`, `mathfemm`, `scifemm`, `juliafemm` and `luafemm`
all have theirs staged into the flat `examples/` folder by
`packaging/femm-qt.iss` and `packaging/stage_extras.sh`.

Its contents are the Python counterparts of the same set the others ship:
`demo2.py`, `roters1b.py`, `strips.py`, `htutor.py`, `ACElec2.py`,
`coilgun.py`, plus `Phase3.py` and `ReturnErrorMsg.py`.

Two obstacles, and they are why this is not simply an oversight to correct:

1. **`ACElec2.py` and `coilgun.py` collide by name** with core examples that
   are already shipped. The two sets are not the same scripts — the core ones
   are in-process scripts using bare commands, these use `import femm` — so
   flattening both into one directory would overwrite.
2. Shipping them only makes sense once the 62 above are closed; otherwise some
   would fail when run the in-process way, which is how a user who found them
   in an examples folder would most likely run them.
