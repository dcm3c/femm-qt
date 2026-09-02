# Bulk getters: one implementation, flattened for the wire

*Design, 2026-08-26. Rewritten 2026-08-28 (twice — see "How this design
arrived"). Follows `docs/array-pipeline.md`, which establishes why in-process
bulk mesh access is worth having at all.*

## The approach

A bulk getter is **one function**: a bound C++ function returning typed ulab
arrays. Nothing else is written.

The wire falls out of it. `format_value()` learns to flatten an ndarray to
tab-separated text through the per-value formatter it already uses, so
`callfemm('mo_getallelements()')` returns exactly the bytes it returns today.
Each binding keeps a one-line reshape shim — the shims it already has.

```
in-process   mo_getallelements()  ->  (conn i4, ctr f8, area f8, group i4)
over a wire  mo_getallelements()  ->  the same arrays, flattened row-major to
                                      the same tab-separated text as today
binding      z = reshape(raw, 7, [])'        <- unchanged
```

Three things follow, and they are the whole argument for this shape:

* **No per-binding code.** No `straccum`/`strappend`/`strfinish` dance, no
  comprehension-scope trick, no loop written five times in five languages.
* **No drift.** There is one implementation, so there is nothing for a test to
  keep in agreement.
* **The 256-byte truncation stops being a hazard** rather than becoming one.
  See "The name typed by hand", below.

Cost: measured at 3-8% over today's hand-written C++ string builders, with
byte-identical output. Details under "Measurements".

## Shapes

Six of the seven magnetics getters are a single homogeneous array, which
flattens in C order — that *is* wire order, so they need no new concept at all.
Only `mo_getallelements` is mixed-dtype.

| getter | wire record | array return | flatten |
|---|---|---|---|
| `mo_getallnodes()` | `x y` | `(nn,2) f8` | C order |
| `mo_getallb()` | `B1 B2` | `(ne,2)` f8 / c16 | C order |
| `mo_getalla()` | `A` | `(ne,)` f8 / c16 | C order |
| `mo_getallgapb(name[,npts])` | `Br Bt` | `(npts,2)` f8 / c16 | C order |
| `mo_getallgapa(name[,npts])` | `A` | `(npts,)` f8 / c16 | C order |
| `mo_getallgapharmonics(name)` | `acc acs brc brs btc bts` | `(nh,6)` f8 / c16 | C order |
| `mo_getallelements()` | `p1 p2 p3 cx cy area grp` | **tuple:** `(ne,3) i4`, `(ne,2) f8`, `(ne,) f8`, `(ne,) i4` | table rule |

`f8` at DC and `c16` at AC, following `s_rdoc->Frequency` — the same rule the
scalar getters have used since the zero-imaginary collapse was dropped
(2026-08-16). A script that assumes one or the other breaks between DC and AC,
which is already true of the scalar getters and is therefore consistent rather
than new.

**Byte-identity holds either way, so this is a choice about the script side, not
a constraint.** `CComplex::ToString` emits only the real part when `im == 0`
(`common/complex.cpp`), and `format_complex` has the identical branch — so a DC
result is the same text whether the array is `f8` or `c16`. `f8` is chosen
because it is what a script actually wants at DC, and it halves the memory.

### The other physics: what the element structs actually hold

The analogues of `mo_getallb`/`mo_getalla` exist for every physics, but the
storage differs in a way that will silently produce wrong answers if copied
blindly from magnetics:

| physics | element field | nodal potential | has `Frequency`? |
|---|---|---|---|
| magnetics | `CComplex B1, B2` — two components, **each a phasor** | `CComplex A` | yes |
| electrostatics | `CComplex D` — **ONE VECTOR**, re = Dx, im = Dy | `double V` | no |
| heat flow | `CComplex D` (`HRElement` is an alias of `ERElement`) — one vector, re = Fx, im = Fy | `double V` (temperature) | no |
| current flow | `CComplex Jx, Jy` — two components, **each a phasor** | `CComplex V` | yes |

**The trap: for electrostatics and heat flow, that `CComplex` is a 2-D VECTOR
packed into re/im, not a phasor.** Flattening it as a complex number would put
`Dx+Dy*I` on the wire where two real columns belong. It is the same convention
the line integrals use for normal/tangential components — do not "fix" it, unpack
it. Those two physics get `(ne,2) f8` *always*; their documents have no
`Frequency` member at all.

Current flow does have a frequency, so it follows the magnetics rule: `f8` at DC,
`c16` at AC, for both `co_getallj` and the nodal voltage.

So the field/potential getters still to write are:

```
eo_getalld   (ne,2) f8            eo_getallv   (nn,) f8
ho_getallf   (ne,2) f8            ho_getallt   (nn,) f8
co_getallj   (ne,2) f8 / c16      co_getallv   (nn,) f8 / c16
```

Names above are provisional — check them against the vocabulary the
`*_getpointvalues` getters and the manual already use for each quantity before
committing to them, since these become permanent API.

**The original design scoped four `mo_getall*`. There are seven** — the three
gap getters are C++ too. They are not loops over an element-indexed scalar
getter (they evaluate an AGE harmonic series), but they return arrays like
everything else and are in scope.

## The flatten rule

```
ndarray                         -> values in C order, tab-separated, through
                                   the SAME per-value formatter every scalar
                                   result already goes through
tuple of ndarrays, all with the -> a TABLE: row-major, each row contributing
same leading dimension             every array's row-k slice in tuple order
any other tuple                 -> today's behaviour, tab-join the items
```

The guard on the middle rule — *all* items ndarrays, *same* leading dimension —
is what leaves every existing tuple-returning command untouched.

**Dispatch dtype once, outside the loop.** The measured cost below assumes a
tight typed loop over the payload. If `format_value`'s existing per-value
`mp_obj_is_type` chain runs per element instead, the numbers do not hold.

A naive "flatten each array in the tuple in turn" is wrong and worth stating
explicitly: it emits `[all conn][all centroids][all areas][all groups]`, which
is column-block order, not record order, and does not reproduce the wire text.

## Decisions, and why

### Connectivity stays 1-based

The wire emits `p[0]+1`, `mo_getelement` takes a 1-based index, and Original
FEMM does the same (`femm42src/femm/femmviewLua.cpp:2357-2361`). Byte-identity
with the wire is the acceptance test, so the array must match.

This was the awkward half of the design until 2026-08-28, because ulab's
`np.take` refuses a float index and `conn - 1` used to produce float64. That is
fixed — int32 is now a full arithmetic dtype
(`docs/ulab-int32-arithmetic.md`), so the gather idiom is simply:

```python
conn, ctr, area, grp = mo_getallelements()
nodes = mo_getallnodes()
x0 = np.take(nodes[:, 0], conn[:, 0] - 1)
```

**Document the 1-based convention loudly.** An off-by-one here does not raise;
it draws a plausible wrong mesh. Note also that the `.ans` element records store
the raw **0-based** `p[]` — anyone moving between a parsed `.ans` and the
scripting API crosses that boundary with nothing warning them. The manual never
states the convention anywhere; fixing that is part of "done" (see Testing).

### `mo_getallelements` returns a tuple, not one `(ne,7)` f8 array

Two independent reasons, both measured:

* **Speed.** The single-array form is 36% slower to flatten (172 ms vs 132 ms at
  1M elements), because its four integer columns lose the integer fast path.
* **Usability.** float64 connectivity is not usable as a gather index at all;
  `np.take` requires an integer dtype, and there is no `.astype()` in ulab.

Both point the same way, so the tuple wins despite being the one new concept.

### What ulab can and cannot do with these arrays

Verified by running, 2026-08-28 — not read off the source. This bounds what the
array half is actually good for, and the design should not oversell it:

* `ULAB_MAX_DIMS = 2`. Every shape above fits, but `np.take(nodes, conn, axis=0)`
  would be 3-D and fails; gathers run one column at a time.
* **No integer fancy indexing.** `nodes[conn]` raises `NotImplementedError`.
  `np.take` is the only gather.
* Boolean masking is **1-D only**: `area[grp == 3]` works, `nodes[nodes > 0.5]`
  does not.
* No `.astype()`; `np.array(a, dtype=...)` is the recast.

So the value here is **elementwise field maths** — `|B|` per element, loss
densities, group-masked sums, integrals — not connectivity-heavy mesh
manipulation. That is what the transient and noise work needs, so it is fine,
but "numpy on the mesh" oversells it.

## Measurements

### The flatten is free

Standalone benchmark of the exact formatters (`append_g`, `fio_append_int`,
`format_complex`), realistic full-mantissa values, best of N.
**Output byte-identical in every row**, which is the point:

`mo_getallelements`, 7 columns:

| | 18,016 elem | 156,000 | 1,000,000 |
|---|---|---|---|
| today: walk structs + format | 2.17 ms | 19.3 ms | 122 ms |
| **split columns, flattened row-wise** | **2.27 ms** | **20.0 ms** | **132 ms** |
| single `(ne,7)` f8 array | 2.95 ms | 26.7 ms | 172 ms |
| bytes (all three identical) | 1,342,845 | 11,628,274 | 74,543,912 |

`mo_getallb`, 2 complex columns — a wash: 2.39 ms today vs 2.42 ms flattened at
18k; 135.5 vs 140.2 ms at 1M; 81,735,284 bytes either way.

Building the array costs 0.04-0.75 ms at 18k-156k and ~7 ms at 1M — noise.

Two things fell out that matter:

* **`to_chars(1.0)` emits `"1"`**, the same as `appendInt`. That is why a
  float64 array still reproduces the wire bytes exactly, and why the wire never
  needed the integer/float distinction. The dtypes are for the *script* side.
* The single-array form's 36% penalty is entirely the four integer columns
  losing `fio_append_int`. Keeping dtypes split — which the numpy side wants
  anyway — is also the fast option.

Caveat on scope: this is a microbenchmark of formatting and the array walk, with
synthetic values. It excludes `ndarray_new_dense_ndarray`'s allocation and the
`mp_obj` dispatch, both per-array rather than per-value, so neither can move a
2 ms number — but they are unmeasured. Benchmark source is not in the tree; it
is reproducible from the description above in about twenty minutes.

### Memory

At 1M elements: array 38 MB (split) on the GC heap + 71 MB of text on the native
heap, against 71 MB today. The comment at `py_mag_postproc.cpp:242` cites GC-heap
pressure as the reason those getters write via `py_set_result`; that reason is
now half-obsolete — bdwgc has no fixed heap, and the text still lands in a native
`std::string`, so only the array is collector-visible. ulab payloads are already
`gc_alloc_atomic` (`ndarray.c:39`), so the collector never scans them.

### Over the wire, before any of this

Measured earlier on `my_SPM_motor_AGB`, after the `to_chars` formatter change
landed:

| strategy | time | vs C++ |
|---|---|---|
| C++ `mo_getallelements` | 5.4 ms | 1.0x |
| comprehension, aliased | 8.2 ms | 1.5x |
| module-scope `for` (the shipped `eo_` shape) | 52.8 ms | 9.8x |

An empty round trip is 0.036 ms and transferring 1.42 MB is 2.6 ms, so round
trips are irrelevant and the transfer dominates any of these.

## The name typed by hand — resolved, not deferred

The previous design had an open hazard: a user bypassing the binding
(`callfemm('mo_getallelements()')`, or the same text at a `femm-lua` prompt)
would reach the local function and get `capture_result`'s fallback for an
unrecognised object — `append_via_print` into `char tmp[256]`, i.e. **silently
truncated at 256 bytes**. It was going to become reachable the moment the first
array-returning getter landed.

Teaching `format_value` to flatten is exactly the fix, so under this design the
hazard never appears. It also removes the truncation as a class for any future
array-returning command.

## Build order

**Nothing is deleted.** The existing C++ getters are rewritten IN PLACE: the
loop body stays, and the `appendComplex`/`appendDouble`-into-a-`std::string`
tail becomes a store into an ndarray payload. `py_set_result` goes away from
these functions and the return type changes from `void` to `mp_obj_t`. That
makes the acceptance test a genuine before/after on the same function.

(An earlier revision had a "delete the C++ `mo_getall*`" step. That belonged to
the superseded plan, where a wire-side loop in each binding replaced them.)

**Nine C++ getters exist, not seven** — magnetics has all seven, and
electrostatics has `eo_getallelements` and `eo_getallnodes`. Only heat flow and
current flow have none.

1. **`format_value` ndarray flatten** (homogeneous arrays only) **+
   `mo_getallnodes()` rewritten to return `(nn,2) f8`.** The smallest end-to-end
   slice: a single homogeneous array needs no new concept, and it proves the
   flatten rule, the wire contract, the existing shim and the speed at once.
   Byte-compare over a real socket against the pre-change output.
2. **The table rule + `mo_getallelements()`.** The one new concept, on its own,
   with its own byte-compare.
3. **The remaining five magnetics getters**, including the three gap getters.
4. **`eo_getallelements` / `eo_getallnodes`** — the same rewrite, and the same
   byte-compare, on functions that already exist.
5. **ho/co geometry** — genuinely new C++, since those physics have no bulk
   getters at all. This closes the coverage gap that started the exercise and
   takes mathfemm's seven missing wrappers, the binding suite's last Layer 1
   failure, to zero.
6. **Field and potential getters for eo/ho/co** — also new, and the only step
   with no existing output to byte-compare against, so it is pinned against the
   scalar getters instead (see Testing). Read "The other physics" above first:
   electrostatics and heat flow store the field as a VECTOR in one `CComplex`,
   not as a phasor, and current flow follows the magnetics DC/AC rule.

Steps 1-3 are magnetics only.

**A live bug to fix while in here:** `octavefemm/eo_getallelements.m` builds its
result with the `straccum`/`strappend` loop even though the C++
`eo_getallelements` has existed since the first MicroPython commit (`4880a60`).
Three round trips and a module-scope for loop — the slowest shape measured, 9.8x
— to reproduce what one `callfemm('eo_getallelements()')` already returns. That
file was held up by the superseded design as the *template* for the whole
per-binding-loop approach; it was never that, just a wrapper that forgot its own
C++ existed. It should be the same one-line reshape shim the `mo_` wrappers use.

## Can the getters themselves be written in Python?

**Yes, but the answer is C++ — decided 2026-08-28.** Recorded because the
measurements say Python is viable and someone will propose it again.

The deciding argument is not speed, it is what the getter *is*. Today
`mo_getallb()` builds 1.4 MB of text and returns `None` in-process: called
locally it pays the entire formatting cost for a result it cannot use. Having
the C++ return an array inverts that — the local case never formats at all, and
the wire case gets its text from the generic flatten. A Python getter cannot do
that, because it has to go through the scalar getters to obtain the values in
the first place, so it always pays for an intermediate representation.

Concretely, C++ wins on three things Python cannot reach:

* **No boxed intermediates.** The Python path builds a flat list of 126k Python
  floats at 18k elements, 7M at 1M. C++ writes straight into the ndarray payload
  — one `gc_alloc_atomic` allocation, no churn. This is the one that bites at
  the sizes the transient work targets.
* **No point location for field data.** C++ reads `meshelem[k].B1/B2` directly.
* **No aliasing cliff** (below).

The measurements that made Python look attractive follow. They stand; they just
lost.

Yes — **all of them, with no new C++.** Measured 2026-08-28 on
`my_SPM_motor_AGB`, 18,016 elements, in-process:

| | Python | C++ | ratio |
|---|---|---|---|
| geometry, typed tuple | 2.52 ms | ~2.3 ms | **1.1x** |
| field (`mo_getallb`) | 6.30 ms | 2.27 ms | **2.8x** |

Both produce correct dtypes and values verified against the scalar getters. A
Python getter returns a real ulab array, so it flattens to the wire exactly like
a C++ one and the binding shims do not care which it is.

**The geometry spelling matters enormously.** Build ONE flat f8 matrix, then
slice the typed columns out of it — every step C-level:

```python
flat = []
for k in range(1, ne + 1):
    flat.extend(mo_getelement(k))        # one C-level extend per element
m = np.array(flat).reshape((ne, 7))
conn = np.array(m[:, 0:3], dtype=np.int32)
ctr, area = m[:, 3:5], m[:, 5]
grp = np.array(m[:, 6], dtype=np.int32)
```

Building the seven columns as Python lists instead is **573 ms** — 227x worse.
`np.array(ndarray, dtype=...)` converts natively in ulab; `np.array(list,
dtype=<int>)` goes through a Python truncation walk in the proxy and is slow.

**Two corrections to earlier versions of this file, both of which pointed the
wrong way:**

* *"`mo_getb`/`mo_geta` do not exist, so field getters need C++."* Wrong on both
  counts. Those are **coordinate**-based (`mo_getb(x,y)`) and are a subset of
  `mo_getpointvalues`, which is available in-process; the bindings already
  implement them that way. What this file previously asked for was an
  *element-indexed* `mo_getb(k)`, a different function that was never the only
  option.
* *"point location costs 4.5 us, so ~81 ms of pure searching on an 18k mesh."*
  Measured in-process it is **0.35 us** — 13x cheaper, and the whole field
  getter is 6.3 ms. Caveat, unmeasured: a bulk getter queries in element order,
  where each search starts adjacent to the last, so some of that may be
  locality rather than raw speed. It is the realistic access pattern either way.

**What is still unmeasured, and would decide it for large meshes:** the flat
list is 126k boxed Python floats at 18k elements; at 1M it is 7M of them before
`np.array` runs. C++ writes straight into the payload with no intermediates.
Measure that before choosing Python for the sizes the transient work targets.

Also live: the aliasing cliff. `[mo_getelement(k) for k in ...]` unaliased
measured 22.7 ms against 2.4 ms aliased — 9.4x, from an effect nobody explained
(see "How this design arrived"). A Python getter has to be written carefully and
stays exposed to it; a C++ one does not.

**So Python was never blocked on capability — it loses on memory and robustness
at scale**, which is why the build order above stays C++. Keep this section:
Python remains the right fallback if a physics ever needs a bulk getter that C++
does not already have the data for, and it is the cheapest way to prototype a
new one before committing it to C++.

## The manual

Coverage today, by `getall` mentions per physics chapter:

| chapter | documented |
|---|---|
| `magnpy.tex` | 7 — all of magnetics |
| `elecpy.tex` | 2 — `eo_getallnodes`, `eo_getallelements` |
| `heatpy.tex` | **0** |
| `cflowpy.tex` | **0** |

So every getter added in steps 5 and 6 needs writing up, and heat flow and
current flow need the subsection created rather than extended.

**The magnetics ones need no rewrite — the code is about to start matching what
they already say.** The manual documents `mo_getallnodes()` as *"Returns the
coordinates of all mesh nodes as a single flat array of N x 2 values"*. In
process it returns **`NoneType`** — verified, all four of
`mo_getall{nodes,elements,b,a}` — because the function writes to the wire buffer
via `py_set_result` and returns `void`. The documented behaviour has only ever
been true through a binding. After the rewrite the function genuinely returns
the values, so the implementation catches up to the documentation rather than
the other way round.

One editorial point that does need attention. The present wording is written
from the wire's point of view — *"a flat array of N x 2 values ... the binding
wrappers reshape it into an N x 2 matrix"*. After the change that stays exactly
true over the wire, but in process the call returns a shaped array directly and
nothing reshapes anything. The text has to cover both paths without implying the
in-process caller gets a flat list.

And carry over the standing manual TODO: **mesh indices are 1-based and no
chapter says so** — `*_getelement`, `*_getnode`, and the `.ans`-is-0-based
asymmetry. Doing that at the same time is cheaper than a second pass.

## Testing

`MOBlockIntegralTest.py` and friends already pin these values against Original
FEMM, so the test for a bulk getter is **agreement with what it replaces**, not
a table of expected numbers:

* **Byte-identity over a real socket**, per getter and per physics, against the
  current C++ output. This is the acceptance test for steps 1-4 and it is the
  reason the whole design is safe to land incrementally.
* `mo_getelements()` columns against `mo_getelement(k+1)` for every k.
* Dtypes explicitly: connectivity and group must come back integer, not float;
  field getters must follow `Frequency`.
* The arrays must survive a `gc.collect()` with the results document closed —
  they are copies, not views into `s_rdoc`.
* A **1-based** regression: `np.take(nodes[:,0], conn[:,0] - 1)` against
  `mo_getnode(conn[k][0])` for every k. An off-by-one here is silent.
* Add a **Layer 2 scenario** to `testing/bindings` for a bulk getter — a large
  text reply plus a reshape is a marshalling shape nothing currently covers, and
  it is what proves the shims still work across all five bindings and six hosts.
* **Manual**: state the 1-based convention in `magnpy.tex`, `elecpy.tex`,
  `heatpy.tex`, `cflowpy.tex` (for `*_getelement` and `*_getnode`), and note the
  `.ans`-is-0-based asymmetry. Nothing says it today except two comments we
  wrote ourselves.

## How this design arrived

Kept because three conclusions in this file were wrong, and re-deriving them
costs a day each.

**First plan: keep the C++ getters, add array getters under new names.** Rejected
— two implementations of the same idea, and the wire coverage gap for eo/ho/co
stays open.

**Second plan (2026-08-28 morning): implement each bulk getter twice under one
name — a client-side loop in each binding's own language for the wire, and an
array getter in-process.** This is how the earlier Lua 5.4 femm-qt did it, and
`octavefemm/eo_getallelements.m` still ships that shape. Rejected once the
flatten was measured: it needs a loop written in five languages, it needs a test
to keep two implementations in agreement, and it is *slower* than the single
implementation (8.2 ms vs 5.4 ms over the wire).

**Superseded measurements, each of which was quoted as settled at some point:**

* *"Deleting the C++ getters costs 2.4x"* — measured when `%.17g` formatting
  dominated both paths, so both were slow.
* *"...6.7x"* — measured after the `to_chars` change, which sped the C++ side
  more than the script side.
* *"...1.5x"* — measured after writing the loop properly. All three are
  obsolete: under this design the answer is 3-8%, because there is no loop.
* *"87% of the cost is `mo_getelement()` itself"* — wrong. Name resolution was
  almost all of it; aliasing the name to a local was 4.7x. The cause was never
  established (bare-name micro-probes disagreed by 30x across three scripts), so
  do not treat any per-lookup constant from that work as real. The *effect*
  reproduced every time, in-process and over the wire, with byte-verified
  output.
* *"The wire text will shrink 30-40%"* — no. That figure is `fastio.h`'s, for
  `.ans` files, which are mostly round input values. Computed field values carry
  full mantissas: measured 1.9-3.7% shorter. **The win is speed, not size.**

Pure-Python formatting is out for a second reason beyond speed: it cannot
reproduce the bytes. `'%.17g'` emits the long form the wire no longer uses, and
`str()`/`repr()` give `1.0` where the wire gives `1`.
