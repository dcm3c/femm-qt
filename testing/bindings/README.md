# Binding regression suite

The engine has 18 accuracy suites in `testing/`. None of them drives a
**binding**, which is why `num.m` sent MATLAB's `1+2i` instead of `1+I*2` for
five months through a permanently-green build: every one of those suites runs
in-process, so no wrapper was ever executed.

This suite covers the layer they miss. It is in two parts, because two different
things can be wrong and they need different oracles.

## Layer 1 -- does every wrapper emit a well-formed command? (built)

Each binding is ~624 wrappers whose whole job is to build a command string. Any
one of them can have the wrong name, wrong argument order, a missing separator
or the wrong quoting, and nothing downstream would notice. Layer 1 executes
**every wrapper in every binding** with a stubbed transport and compares what
they emit.

    matlab -batch "probe_octavefemm"     # derive the contract
    python3 make_contract.py
    matlab -batch "emit_octavefemm"      # then one per installed host
    python3 emit_pyfemm.py
    WScilex-cli -quit -f emit_scifemm.sce
    julia emit_juliafemm.jl
    math -script emit_mathfemm.m
    python3 compare_emissions.py         # exits 1 on an untriaged difference

### Where the contract comes from

`probe_octavefemm.m` calls every octavefemm wrapper with integer sentinels and
records the command each one builds. That yields the name, the arity, and --
from which sentinels came back quoted -- which arguments are strings.

Nothing is parsed. A hand-written call table would drift from the code and a
static parse of the wrappers gets multi-branch `nargin` dispatch wrong (measured:
`ci_addnode` looks like 6 arguments to a regex and is 2). Executing the binding
cannot be wrong about what the binding does.

octavefemm is the source because it is the wire-token authority: the other four
were derived from it and its `.m` files are closest to Original FEMM's.

### Why the comparison is semantic

For the same argument the five bindings legitimately emit `1`, `1.0`,
`1-I*0.25`, `(1-0.25*I)`, `complex(1,-0.25)` and `Complex(1.,-0.25)`. All six
are the same number to the interpreter, so `compare_emissions.py` compares the
command name, the argument count, and the argument VALUES -- never the text.

That is also the level at which the bug this suite exists for shows up:
`1+2i` parses as no number at all.

### Three passes

Every wrapper is called three times, because number formatting is where the
bugs are: `int` (1, 2, 3...), `real` (-1.25, -2.75...) and `cplx`
(1-0.25i, 2-0.5i...). The `cplx` pass is the one that catches a `num.m`.

### Per-host notes

* **octavefemm runs on two hosts.** MATLAB and Octave both drive the same
  binding, and the Layer 2 drivers are ONE file each rather than two, which
  takes some care: MATLAB requires a script's local functions to come LAST,
  Octave requires them to be defined BEFORE use, and those cannot both be
  satisfied. So `*_octave.m` has no local functions at all -- helpers are
  anonymous functions and per-case bodies are inlined in loops. Both hosts then
  produce byte-identical output. (A stale `~/.octaverc` here adds a path from
  another tree; harmless, but it is why Octave prints a warning first.)
* **octavefemm, Layer 1, on both hosts.** The probe and the recorder run
  unchanged under Octave as well as MATLAB. `BIND_EMIT_SUFFIX` appends to the
  output name so one host does not clobber the other:

      matlab -batch "emit_octavefemm"                            -> emitted_octavefemm.tsv
      BIND_EMIT_SUFFIX=_octave octave-cli -q emit_octavefemm.m   -> emitted_octavefemm_octave.tsv

  Measured 2026-08-28: **632 wrappers, byte-identical except the four
  `*_getview`**, which embed a `tempname` -- MATLAB's `tp<hex>`, Octave's
  `oct-<rand>`. Zero non-tempname differences. The derived contract is identical
  too, so `nargin()` agrees between the hosts. Both were assumptions until now,
  and using Octave's recording as the reference gives the cross-binding
  comparison exactly the same result as MATLAB's.
* **octavefemm** -- the recorder shadows `callfemm.m` by putting
  `stub_octavefemm/` later on the path. Runs under MATLAB or Octave.
* **pyfemm** -- the session is built with `__new__`, so no engine is launched
  and no socket opened.
* **scifemm** -- `callfemm` is redefined after the `exec`. Two traps here.
  scifemm passes a command as a string MATRIX (`['mi_addarc(', num(x), ')']` is
  1x3 in Scilab) and the real transport joins it with `strcat`; a stub that
  forgets that records three fragments per call. And the contract must be driven
  against the names scifemm ACTUALLY DEFINES -- captured by snapshotting the
  namespace around the `exec` -- because `isdef()` also matches Scilab builtins.
  scifemm has no `messagebox` wrapper, so driving by `isdef` called Scilab's own
  GUI `messagebox()`, which popped a modal dialog and waited for a click. Run
  time went from four minutes to 3.3 seconds once that stopped happening.
* **juliafemm** -- the transport is replaced inside the module with `Core.eval`.
* **mathfemm** -- the awkward one, twice over. Its symbols are sealed with
  `SetAttributes[..., {Locked, ...}]` (inherited from OF), and `Locked` cannot be
  removed, so the transport cannot be replaced at all; the harness loads a copy
  with only those 577 attribute statements stripped, and asserts that is the
  only edit. It also must run on Mathematica 9, where `ReadString`,
  `Association` and `KeyExistsQ` do not exist and — worse — stay silently
  unevaluated, which reads as "the loop ran and found nothing".

## Layer 2 -- does a value survive the round trip? (built)

Layer 1 proves the command text is well formed. It cannot prove the engine
ACCEPTS it -- seven of the eight `*_gridsnap` commands rejected the documented
call while Layer 1 reported them perfect -- and it never watches a value make
the round trip at all.

    python3 run_layer2.py            # skips hosts that are not installed

Layer 2 runs `testing/EOCondPropTest.py`'s scenario through each binding and
hands the result to the **same comparer and the same Original FEMM golden** the
in-process suite uses. That is the key property, and it came from the
maintainer's suggestion to base this on the existing suites rather than invent a
scenario: a binding is then measured against Original FEMM, not against us, and
there is no new oracle to maintain.

    pyfemm      ok    compared 4 conductor-property components: 4 agree, 0 flagged
    octavefemm  ok    ...
    scifemm     ok    ...
    juliafemm   ok    ...
    mathfemm    ok    ...

Each driver is a line-for-line translation of the Python test, so the output
format is identical and `CondPropCompare.py` needs no changes. `FEMM_DIR` is set
by the runner so every binding launches the local build rather than an installed
FEMM; all five honour it.

**Verified that it can fail**, which five greens do not otherwise establish:
perturbing one induced charge by 9.4% is flagged, `err=9.5%`, exit 1.

Two host notes worth keeping:

* **mathfemm** writes exponents as `*^`, which the comparer's `float()` cannot
  parse. `NumberForm` with an explicit `NumberFormat -> (Row[{#1, "e", #3}] &)`
  and `ExponentFunction -> (# &)` emits a C-style `1.e4` / `4.476961604e-5`
  instead. `StringPadRight` is v10.1+ and had to be hand-rolled -- the third
  time a v10 function silently no-opped on Mathematica 9 in this work.
* **scifemm** needs its format strings written to the file directly; a `
`
  routed through a shell heredoc arrives as a real newline and the script will
  not parse.

### Scenarios

`run_layer2.py` holds a table -- driver stem, comparer, golden, and the number
of components the in-process run compares. Adding a scenario is a row plus one
driver per binding.

    condprop      EOCondPropTest        4 components   real in, real pair back
    circuitprop   MOCircuitPropTest   105 components   complex results back

The component count is not decoration. The comparers SKIP reference keys absent
from the test file (`if k not in tv: continue`), so a driver that died after two
lines would report "compared 2: 2 agree" and read as a pass. A short count fails.

### What circuitprop found immediately

mathfemm returned DC values for every AC case -- 20 of 105 components flagged.
Not a parsing fault: `MIProbDef[60, "inches", "planar"]` matched **no pattern**.
mathfemm shipped only the maximal ProbDef arities (plus `MIProbDef[freq]`), so a
three-argument call stayed unevaluated, sent nothing, and the analysis ran at
whatever frequency the file already had. Silently. Every physics had the same
hole -- MI 1/6/7, EI 5, HI 5/7, CI 6, against octavefemm's mi 1-7, ei 3-5,
hi 3-7, ci 4-6. Twelve overloads added.

**Layer 1 could not have found this.** It calls each wrapper once, at the
contract's arity of 7, which does match the seven-argument overload. Only
driving a real analysis exposes an arity that silently does nothing.

### Complex arguments going out -- the `num.m` case, now covered

`condprop` and `circuitprop` are translations of existing suites, so they inherit
an Original FEMM golden. Neither sends a complex argument, because **no accuracy
suite does** -- the only `*Test.py` files containing complex literals are utility
tests (Csv, Fft, NumpyProxy, PyBridge, Stdlib, UlabInt32), none of the 18 physics
suites. That is the hole octavefemm's `num.m` lived in for five months.

`phasecurrent` closes it. `examples/three-phase-line.FEM` carries three solid
conductors in circuits A, B and C at 60 Hz, and a balanced set is assigned:

    A = 1000                     0 degrees
    B = -500 - 866.0254038j   -120 degrees
    C = -500 + 866.0254038j   +120 degrees

chosen to sum to EXACTLY zero, so the check is exact rather than approximate.

There is no upstream golden for this -- the scenario does not exist in Original
FEMM -- so the reference is generated in-process by the runner (a golden entry
prefixed `@` is a script, not a file). But most of the checking needs no oracle
at all, which is the nice part:

  * `mo_getcircuitproperties` ECHOES the prescribed current, so the value read
    back must equal the complex number sent. That is a round trip through the
    argument marshaller and the reply parser, checked against a constant.
  * the three currents must sum to zero.

Both files -- reference and candidate -- are checked against the prescribed
currents, so a reference from a broken engine cannot license a broken binding.

**Verified to catch the original bug.** Restoring the pre-09409ab `num.m`
(`num2str(x, '%.17g')`, which emits MATLAB's `1+2i`) makes the octavefemm driver
die on its first command with `Error using callfemm / FEMM returns:` and the
comparer reports no records parsed, exit 1. Dropping just the imaginary part of
one phase is flagged twice: once on the echo, once on the sum.

### Extending further

The remaining 16 accuracy suites translate the same way, but the returns
diminish: the physics is already validated in-process, and what varies per
binding is marshalling, which is a type-coverage problem. `MOPointValueTest`
(a 23-value return) and one bulk getter would about exhaust it.

## The recorder must hook where the bytes leave

The first version of the mathfemm recorder hooked `MLPut`. `MLPutWait`, one
level down, strips an `mlput(...)` wrapper before writing to the socket -- OF's
Lua had a real `mlput` and femm-qt's port neutralises it in the transport rather
than editing 70 wrappers. So the recorder captured a string that never travels,
and ~100 mathfemm functions looked broken when they are fine. The report was
written, a shim was added to the engine, and it was all wrong.

Hook at the LAST point before the bytes leave, or replicate whatever is
downstream of the hook. The mathfemm stub now applies the same strip, which took
its disagreements from 303 to 54.

Same class of trap as the Scilab string matrix: in both cases the transport does
something to the command after the seam the stub replaced.

## A difference is not automatically a bug of equal severity

`mi_setblockprop` in scifemm quoted its `magdir` argument, which the harness
reported alongside `*_smoothoff` sending `"on"` instead of `"off"`. They are not
the same kind of finding, and the harness cannot tell them apart -- only the
engine can.

`magdir` may be a NUMBER (degrees) or a FORMULA string such as
`"atan2(y,x)+90"`, which is what the quote/no-quote branch exists for. And a
plain number is itself a valid formula, so `"45"` is accepted. What the engine
does with the two is not identical, though:

    mi_setblockprop("m",1,0.1,"",45,2,7)     ->  ... 0  45  2 7 0
    mi_setblockprop("m",1,0.1,"","45",2,7)   ->  ... 0   0  2 7 0  "45"

A number lands in the MagDir field; a string lands in MagDirFormula with MagDir
set to 0. Both solve, and presumably to the same answer -- so scifemm was not
producing wrong results for numeric magnet directions, it was storing them down
the formula path. Real divergence, different file content, and anything reading
MagDir numerically without evaluating the formula sees 0. But not the
silent-wrong-answer severity of `*_smoothoff`.

Worth stating because the reflex is to rank findings by how loud the diff looks.
Rank them by what the engine does with each form, which means testing both.

## What Layer 1 structurally cannot see

It calls every wrapper once, at the arity the contract records. So it checks
that a wrapper emits correctly AT ONE ARITY -- not that it offers the right set
of arities.

`MIAddPointProp` proved the gap. It was defined three times, and the third
repeated the first's pattern while emitting one fewer argument, so in Mathematica
it SHADOWED the correct definition and every three-argument call silently lost
its last argument. The harness saw the wrong emission, but could not see that a
two-argument overload had gone missing with it -- and the first fix, deleting the
offending definition, removed that overload for good. Reading the three sibling
bindings is what settled the intent.

`find_shadowed.py` covers that blind spot directly from the source. Every
language here resolves a redefinition the same way, last one wins, so a repeated
signature is always either a bug or dead code:

    mathfemm     967 definitions scanned, 0 shadowed
    juliafemm    687 definitions scanned, 0 shadowed
    scifemm      663 definitions scanned, 0 shadowed
    pyfemm      1217 definitions scanned, 0 shadowed
    octavefemm   n/a -- one function per file (647 checked for self-duplication)

It prints the number of definitions SCANNED alongside the number shadowed,
because a regex that quietly matches nothing otherwise reads as a clean result.
The Scilab scanner did exactly that on the first attempt -- an optional
return-value group backtracked, so `function ci_hidegrid()` was recorded as a
function named `d`, and it reported 513 false positives. Validate a scanner
against a known-positive before believing its zero: this one is checked against
the pre-fix mathfemm, where it finds MIAddPointProp.

## Status

Layer 1 is **fully triaged and the gaps are filled**. 32 wrappers were added:

    mathfemm   *_gettitle (8), *_smooth (4), *_snapgrid (4)
    pyfemm     *_gettitle (8), mi_addbhpoints, hi_addtkpoints
    scifemm    hideconsole, showconsole, messagebox
    juliafemm  hideconsole, showconsole, messagebox

What remains is 7 rows: mathfemm has no `mo_getall*`, which is deliberate --
those belong with the bulk-getter work in `docs/bulk-getters-design.md`. Until
then the suite exits 1, which is correct: there IS outstanding work.

`create` was deliberately NOT added to the three bindings missing it. It is an
engine alias for `newdocument`, which they all expose, so a second name would be
surface without capability.

22 real defects were found getting here, 21 of them byte-identical to FEMM 4.2
and therefore inherited. The exception is `mo_getgapharmonics` sending
`mo_gapintegral`, which is femm-qt's own -- the gap-harmonics API has no OF
counterpart to inherit from.

Confirmed real so far:

* `*_readdxf` -- octavefemm sends `print(mi_readdxf(...))`, so the wire returns
  nothing where the other four return the result. Byte-identical to FEMM 4.2, so
  inherited rather than introduced, but still a divergence.
* `*_getview` -- octavefemm and scifemm implement it via `*_savebitmap`; pyfemm
  and juliafemm do not have it at all. **All sixteen were broken, and no layer
  of this suite could have seen it**, because `*_savebitmap` does nothing
  without a view callback -- it needs the GUI, and every layer here runs
  stubbed or headless. mathfemm's `GetView` was the only correct one and is the
  model: `.png` name, read as PNG, delete both temp paths.
  * both: `imread(fname,'bmp')` on the PNG the engine writes. MATLAB errors
    outright (`Unrecognized BMP format signature 'P'`); IPCV silently ignores
    the argument, so the same line was fatal in one host and merely misleading
    in the other.
  * scifemm also: `fname=tempname` is MATLAB idiom that binds a function
    POINTER in Scilab, and the cleanup shelled out to a Windows `del` that was
    unreachable anyway since `imread` threw first.
  Fixed and verified end-to-end against a running GUI in both hosts, which
  agree: `uint8` 665x997x3, no temp files left behind. scifemm also went from
  24 refusals to 0 (24 = 8 wrappers x 3 passes).
* SIX mathfemm wrappers sent the wrong command outright, all byte-identical to
  FEMM 4.2 and so inherited rather than introduced -- **fixed**: `CIResize` sent
  `hi_resize`, `COResize` sent `ho_resize`, and all four of
  `MO/EO/HO/COBendContour` sent `*_selectblock`. Only the CO one showed in the
  first listing because the findings print is capped at 60 rows and sorts
  alphabetically; read the whole list before concluding a fault is isolated.

  Worth understanding why these were invisible: the wrong command is a VALID
  command. `mo_selectblock(10,1)` is accepted by the engine exactly as
  `mo_bendcontour(10,1)` is. No error, no diagnostic -- the contour simply never
  bends and a block gets selected instead. Nothing short of comparing what the
  wrapper emits against what it should emit can find that.
