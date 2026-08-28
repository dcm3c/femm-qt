# Bulk getters: same name, two independent implementations

*Design, 2026-08-26; rewritten 2026-08-28 when the approach below replaced an
earlier one. Follows `docs/array-pipeline.md`, which establishes why in-process
bulk mesh access is worth having at all.*

## The approach

A bulk getter is implemented **twice, independently**:

* **On the wire**, in each binding's own language, as a client-side function that
  loops over the scalar getters and accumulates text. It never sends the name of
  the local function.
* **In-process**, as a real MicroPython/C++ function of the same name, returning
  typed arrays.

They do not collide because neither calls the other. `mo_getallelements()` in
Octave is an `.m` file; `mo_getallelements()` in a MicroPython script is a bound
C++ function returning arrays. A user of either writes the same name and gets
their language's natural type.

This is how the earlier Lua 5.4 build of femm-qt did it -- the Octave function
was a Lua closure sent over the wire, and no C++ existed for it -- and it is
**already shipped here** for electrostatics. `octavefemm/eo_getallelements.m`:

```matlab
n = callfemm('eo_numelements()');
callfemm(['straccum(' num2str(n*70) ')']);
callfemm(['for k in range(1,int(eo_numelements())+1): strappend(*eo_getelement(k))']);
raw = callfemm('strfinish()');
z = reshape(raw, 7, [])';
```

`straccum`/`strappend`/`strfinish` (`py_state.cpp:391-416`) are the C++-side
accumulator: the string is built on the native heap, and `strappend` formats each
value through `format_value()` -- the same function that formats every other wire
result. Three round-trips, because a `for` statement parses as
`MP_PARSE_FILE_INPUT` and so has no return value to capture; `strfinish()` is the
eval that retrieves it.

## Why the magnetics names are the only obstacle

`eo_`, `ho_` and `co_` have no C++ bulk getters, so the scheme applies to them
with nothing in the way.

`mo_getallelements`, `mo_getallnodes`, `mo_getallb` and `mo_getalla` **are** C++
functions (`py_mag_postproc.cpp:244+`) that write straight to the wire buffer via
`py_set_result` and return `None`. Those four names are occupied, which is
exactly why in-process they return nothing.

### First measurement (superseded twice -- kept for the trail)

Both paths produce the same tab-separated text, so the comparison is clean.
`examples/my_SPM_motor_AGB`, 18,016 elements / 10,494 nodes, best of 3:

```
elements    C++  22.1 ms    script loop  53.2 ms    2.4x
nodes       C++   7.7 ms    script loop  15.7 ms    2.0x
```

Only 2.4x, because at the time **the cost was the `%.17g` formatting, not the
iteration** -- both paths formatted the same 126k doubles. Decomposed on the same
mesh (this is the BEFORE state; see the next section for what replaced it):

```
loop only (no formatting)   24.2 ms     <- WRONG READING, see below
loop + strappend            52.8 ms     -> formatting 28.6 ms
C++ mo_getallelements       21.5 ms     essentially all formatting
```

The 24.2 ms row was read as "the per-element call is irreducible". It is not:
~21 ms of it was name resolution, removable by aliasing. That error survived two
revisions of this note and into a commit message.

### The fastio trick applies, and it has now been applied

`common/fastio.h` replaced `snprintf("%.17g")` with `std::to_chars` shortest
round-trip for the `.ans` hot loops. Measured on the exact value mix
`mo_getallelements` emits (126k values, 18,016 elements):

```
snprintf %.17g, everything        26.8 ms
snprintf %.17g + int fast path    20.0 ms
to_chars, everything               2.6 ms
to_chars + int fast path           1.9 ms      round-trip mismatches: 0
```

**10x on the formatter**, and it was applied on 2026-08-28 in four places, which
is everything that formats a double for the wire:

* `format_value`, `format_complex`, `format_complex_display`
  (`libfemm/py_state.cpp`) -- via a new `append_g()`;
* `CComplex::ToString` (`common/complex.cpp`) -- its only femm-qt caller is
  `appendComplex` on this same wire path;
* `appendDouble` in `libfemm/py_mag_postproc.cpp` and
  `libfemm/py_elec_postproc.cpp` -- now `fio_append_g` from `common/fastio.h`.

`%.17g` existed here only to avoid losing precision, and `to_chars` keeps that
guarantee in fewer digits, so there was no reason for the two to differ.

### Measured after the change

In-process, same mesh:

```
loop only (no formatting)   23.2 ms
loop + strappend            26.8 ms     -> formatting 3.6 ms   (was 28.6)
```

Over the wire, `my_SPM_motor_AGB`:

| | before | after |
|---|---|---|
| `mo_getallelements` | 22.1 ms | **8.0 ms** |
| `mo_getallnodes` | 7.7 ms | **2.1 ms** |
| script loop, in-process | 52.8 ms | **26.8 ms** |
| script loop, over the wire | -- | 53.6 ms (3 round trips + 1.4 MB) |

**The win is speed, not size.** Measured on real solver output the text is only
1.9-3.7% shorter (elements 2.4%, nodes 3.7%, B 2.8%, A 1.9%). fastio.h's 30-40%
figure does not transfer: that is for `.ans` files, which are mostly round INPUT
values, whereas computed field values carry full mantissas and have nothing to
drop. Do not repeat the 30% claim for wire data.

### What the formatter change delivered for the deletion

Its acceptance test. Verified over a real socket on 18,016 elements:
`mo_getallelements()` and the script loop now return **byte-identical** strings
(1,421,669 B), as do `mo_getallnodes()` and its loop (412,373 B). The wire also
carries **one precision** again -- scalar `mo_getelement(1)` and the first seven
fields of `mo_getallelements()` agree character for character.

(This section previously argued the change made deletion 6.7x more expensive.
That held only while the loop was written at module scope; see below.)

### How the loop is WRITTEN matters more than whether it is C++

This supersedes an earlier claim in this note that "87% of the cost is
`mo_getelement()` itself". That was wrong, and wrong in a way worth recording:
the measurement that produced it looked up `mo_getelement` by name once per
element, and **name resolution, not the call, was almost all of it.**

Measured in-process, 18,016 elements, best of 5:

```
empty for-loop, nothing in it       0.2 ms
calls only, name looked up          23.2 ms
calls only, aliased to a local       2.1 ms     <- 11x
full loop, names looked up          27.0 ms
full loop, names aliased             5.7 ms     <- 4.7x
C++ mo_getallelements                2.9 ms
```

So the per-element call costs ~0.12 us, not the ~1.3 us it appeared to. The
difference is `ge = mo_getelement` before the loop instead of naming it inside.

**The cause is not established.** Micro-probes of bare name lookup disagree with
each other (16 ns in one script, 512 ns in another, ~100 ns in a third), so the
1.17 us figure implied above should not be treated as a per-lookup constant, and
"the globals dict is doing a linear scan" is a hypothesis that did not survive
testing. What IS solid is the effect: it reproduces in-process and over the wire,
at 4.7x, with byte-verified output every time. Worth its own investigation, since
if it generalises it is a broad scripting speedup.

### Every strategy, one table

All rows on `my_SPM_motor_AGB`, 18,016 elements, output **verified identical at
1,421,669 bytes**.

In-process, no wire:

| strategy | time | vs C++ |
|---|---|---|
| C++ `mo_getallelements` | 2.9 ms | 1.0x |
| script loop, names aliased | 5.7 ms | 2.0x |
| script loop, plain names | 27.0 ms | 9.3x |
| pure Python `join` + `'%.17g'` | 67.6 ms | 23x |

Over the wire, end to end, which is what a binding sees:

| strategy | time | vs C++ |
|---|---|---|
| C++ `mo_getallelements` | 5.4 ms | 1.0x |
| **comprehension, aliased** | **8.2 ms** | **1.5x** |
| `def` via `exec`, aliased | 8.4 ms | 1.6x |
| `any(genexp)`, aliased | 8.3 ms | 1.5x |
| comprehension, not aliased | 29.6 ms | 5.5x |
| module-scope `for` (today's shipped shape) | 52.8 ms | 9.8x |
| pure Python `join`, module scope | 73.3 ms | 13.6x |

Fixed costs: an empty round trip is 0.036 ms and transferring 1.42 MB is 2.6 ms,
so round trips are irrelevant and three of them cost nothing worth optimising.

Pure Python formatting is out for a second reason anyway: it cannot reproduce the
bytes. `'%.17g'` emits the long form the wire no longer uses, and `str()`/`repr()`
emit `1.0` where the wire emits `1` (and cost 226 ms and 154 ms).

### The form to use

Two properties are doing the work: **a comprehension has its own scope**, and
`for x in [obj]` binds a **comprehension-local alias**. Together they get
function-scope speed out of a single expression -- no `exec`, no newlines, and no
backslashes, which matters because pyfemm's `callfemm` doubles every backslash to
protect Windows paths, so an `exec("...")` carrying a newline would arrive as a
literal.

```
straccum(n*80)
[sa(*ge(k)) for sa in [strappend] for ge in [mo_getelement] for k in range(1,n+1)]
strfinish()
```

The comprehension builds a list of n `None`s (144 kB at 18k elements, 1.2 MB at
156k) which is discarded. `any(sa(*ge(k)) for ...)` avoids that at the same
speed, but it **short-circuits on a truthy return** -- if `strappend` ever stops
returning `None`, it would silently produce a partial result. Not worth the
144 kB.

The response to the comprehension is `[None, None, ...]` truncated to 256 bytes
by `append_via_print`. Harmless -- both bindings fail to parse it and hand back a
string nobody reads -- but it is why the accumulator is drained by a separate
`strfinish()` rather than by the same call.

### The generic helper, sketched

One helper per binding, then every bulk getter is a one-liner. `octavefemm`:

```matlab
% femmbulk.m -- generic wire-side bulk getter.
% nfun/getter are command NAMES; ncols is the width of one record.
function z = femmbulk(nfun, getter, ncols)
    n = callfemm([nfun '()']);
    callfemm(['straccum(' num2str(n*80) ')']);
    % Comprehension scope + local aliases: ~6x faster than a module-scope for.
    callfemm(['[sa(*ge(k)) for sa in [strappend] for ge in [' getter ']' ...
              ' for k in range(1,' num2str(n+1) ')]']);
    raw = callfemm('strfinish()');
    z = reshape(raw, ncols, [])';
```

```matlab
function z = mo_getallelements()
    z = femmbulk('mo_numelements', 'mo_getelement', 7);
```

`pyfemm`, as a method on the connection object:

```python
    def _bulk(self, nfun, getter, ncols):
        """Generic wire-side bulk getter: one record per row, ncols wide."""
        n = int(self.callfemm(nfun + '()'))
        self.callfemm('straccum(%d)' % (n * 80))
        # A comprehension gets its own scope, and `for x in [obj]` binds a
        # comprehension-local alias -- together ~6x a module-scope for loop.
        # Deliberately no backslashes: callfemm doubles them.
        self.callfemm('[sa(*ge(k)) for sa in [strappend] for ge in [%s]'
                      ' for k in range(1,%d)]' % (getter, n + 1))
        raw = self.callfemm('strfinish()')
        return [raw[i:i + ncols] for i in range(0, len(raw), ncols)]

    def mo_getallelements(self):
        return self._bulk('mo_numelements', 'mo_getelement', 7)
```

Both keep each binding's existing return type -- an `N x ncols` matrix for
octavefemm, a list of `ncols`-lists for pyfemm -- so no caller changes.

### What this does to the deletion arithmetic

**~1.5x, not the ~10x this note claimed twice.** Deleting the four C++
`mo_getall*` and serving every physics from the generic helper costs about 3 ms
per call on this mesh, ~25 ms on a 156k-element one. The performance objection
does not survive writing the loop properly.

Verified working today, one helper, all four physics -- including heat flow and
current flow, which have no bulk getters at all in any binding:

```
magnetics     my_SPM_motor_AGB.fem   18016 elems / 10494 nodes
electrostat   bdemo1.fee             11303 elems /  5819 nodes
heat flow     auto-htutor.feh         6860 elems /  3572 nodes
current flow  ACElec2.fec            23802 elems / 12110 nodes
```

### What C++ still owes, and it is small

Only **element-indexed scalar field getters**. `xx_getelement(k)` is geometry
only (`p1 p2 p3 cx cy area group`); the sole way to read a field value per
element today is `xx_getpointvalues(x, y)`, which performs a **point location** --
4.5 us measured, so ~81 ms of pure searching on an 18k mesh before any formatting.
That is what `mo_getallb`/`mo_getalla` avoid by reading the element directly.

So `mo_getb(k)` / `mo_geta(k)` and the eo/ho/co equivalents are needed before the
generic helper can serve field data. They are owed anyway -- they are among the 62
names in `pyfemm-inprocess-compat.md`. That is a handful of small tuple-returning
functions, against ~16-20 string-building bulk getters under the alternative.

### The wire text does not move -- verified, not assumed

Both paths now format through `std::to_chars`: the C++ getters via
`fio_append_g`/`CComplex::ToString`, the script loop via `strappend` ->
`format_value` -> `append_g`. Node indices come back from the scalar getter as
doubles, and `1.0` renders as `1` exactly as `appendInt` writes it.

Checked over a real socket rather than reasoned about: on 18,016 elements the two
strings are equal byte for byte, for elements and for nodes. Re-run that check
per physics before deleting anything -- it is cheap, and it is the whole
acceptance test.

(An earlier draft here said to delete first and switch the formatter second, so
that the diff stayed meaningful. That ordering no longer applies: the formatter
switch has landed, both sides moved together, and the diff passes as it stands.)

## The other half: local array getters

Separate from anything above, and driven by the numpy pipeline rather than by
wire coverage:

```
mo_getnodes()     -> (nn, 2) f8                     coordinates
mo_getelements()  -> ((ne,3) i4, (ne,2) f8,         connectivity, centroids,
                      (ne,) f8, (ne,) i4)            area, group
mo_getb()         -> (ne, 2) f8   or (ne,) c16      per element, no point search
mo_geta()         -> (ne,) f8     or (ne,) c16
```

Built straight into `gc_alloc_atomic()` payloads so the collector never scans
them. Splitting is the point: connectivity and group are integers, and an
`N x 7` all-double matrix both wastes half the memory on indices and loses the
distinction.

Build these only where the transient/numpy work actually needs them -- they are
NOT the way to close wire coverage, which the generic helper does for free.
Whether they take the freed `mo_getall*` names or new ones is a naming choice,
not a design one; taking the freed names is what delivers the shared-name
property this design exists for.

## Known edge: the name typed by hand

A user who bypasses the binding -- `callfemm('mo_getallelements()')` in Octave,
or the same text typed at a `femm-lua` prompt -- reaches the *local* function and
gets `capture_result`'s fallback for an unrecognised object:
`append_via_print` into `char tmp[256]` (`py_state.cpp:200`), i.e. **silently
truncated at 256 bytes**. Nothing does this today, because no bound function
returns a list or an ndarray.

Two possible answers, neither urgent:

* teach `format_value` to flatten an ndarray to tab-separated text through the
  per-value formatter it already has -- which makes the hand-typed call return
  the right thing and removes the truncation as a class; or
* leave it, and document that bulk getters are binding functions.

Worth deciding before the first array-returning getter lands, since after that
the truncation is reachable.

## What to build first

1. **The generic helper in one binding**, on the shape in "The form to use",
   replacing `octavefemm/eo_getallelements.m`. Byte-compare against the current
   output; it should be identical and ~6x faster.
2. **`ho_`/`co_` geometry bulk getters** through that helper. This closes the
   coverage gap that started the whole exercise, and needs no C++ at all.
3. **`mo_getb` / `mo_geta` scalar, element-indexed**, plus the eo/ho/co
   equivalents. Owed anyway, and the prerequisite for field data through the
   helper.
4. **Delete the four C++ `mo_getall*`**, serving them from the helper. ~1.5x, and
   the byte-identity check is the acceptance test.
5. **The local array getters**, driven by what the transient work needs.

**The benchmark trap, restated because it will mislead whoever measures this:**
reading B per element today goes through `mo_getpointvalues(cx, cy)` at the
centroid, which is a *point location* -- a search for the element containing a
point the caller already identified, 4.5 us measured. A bulk getter removes the
search as well as the loop. Comparing "loop vs bulk" without separating those two
credits the array API with a speedup that belongs to not searching.

So the honest comparison is three-way: per-element loop with point location
(today), per-element loop with direct element read, and bulk.

## Testing

`MOBlockIntegralTest.py` and friends already pin the values these getters expose,
against Original FEMM. So the test for a bulk getter is **agreement with the
scalar getter it replaces**, element by element, over a real mesh -- not a table
of expected numbers:

* `mo_getelements()` columns against `mo_getelement(k+1)` for every k;
* `mo_getb()` against `mo_getpointvalues()` at each centroid, which is also the
  check that the point-location and direct-read paths agree;
* dtypes explicitly: connectivity and group must come back integer, not float.

Two the wire cannot give you:

* the arrays must survive a `gc.collect()` with the results document closed,
  since they are copies rather than views into `s_rdoc`;
* the same script, run in-process and over the wire through pyfemm, must reach
  the same numbers from the same call -- which is the property the shared name is
  claiming, and the only test that actually checks the two implementations agree.

That last one is the load-bearing test of this design. Two independent
implementations of one name will drift unless something compares them.
