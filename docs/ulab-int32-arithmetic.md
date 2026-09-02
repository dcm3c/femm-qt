# Making int32 a real dtype, not storage-only

*Scoping note, 2026-08-28. Split out of the bulk-getter work, which is where the
problem surfaced; it stands on its own and should not ride along with that
change.*

> **DONE, 2026-08-28.** Implemented, and the scope below turned out to be
> understated in two ways worth recording.
>
> **The estimate was low.** The plan named four operators (add / subtract /
> multiply / modulo). In fact `equality` and `more` had no int32 branches either
> and were working *only* because the widening reached them first, so removing
> it would have made every int32 comparison silently return all-false; `np.take`
> was unusable; `np.bitwise_*` failed outright; and `a += 1` silently changed the
> array's dtype to float64. `numpy/bitwise.c` has its own dtype chains, separate
> from `ndarray_operators.c`, which the plan did not account for at all.
>
> **The shape was better.** None of that needed the ~100 hand-written leaves.
> One kernel — `ndarray_binary_int32()` plus an in-place twin — reached from two
> routing points covers every operator, and leaves ulab's chains untouched, so
> the re-merge story is *better* than before rather than worse.
>
> **The sweep earned its place twice.** It found a heap-corruption bug in
> upstream's `np.delete()` (the rewind happens after the sort, so `HEAPSORT1`
> runs off the end of the buffer — every dtype, every multi-index call), and it
> caught a real out-of-bounds write in the first draft of the new kernel, which
> looped on the raw `shape` argument instead of `results->shape`. Neither was
> findable by inspection; both crashed at an unrelated call much later.
>
> Regression: `_verify_all.sh` fully green, FQReference at zero movement.
> `build_test.sh` also green, after fixing two stale things it had been failing
> on since earlier commits (a `%.17g` expectation left behind by 6a7097d, and a
> reference to `fkn_py.cpp`, deleted in 1edc420).

## What is wrong

femm-qt added `NDARRAY_INT32` to ulab on 2026-08-16 because upstream's integers
stop at 16 bits and `np.array([40000], dtype=np.int16)` silently gives `-25536`.
The addition covers construction, element access, slicing, views, boolean
masking, `reshape`, `tobytes`/`frombuffer`, printing, sorting and the one-array
numpy functions.

It does **not** cover arithmetic. `ndarray_binary_op` widens any int32 operand to
float64 before dispatch (`ndarray.c:1624`, via `ndarray_widen_int32()`), so
`int32 - 1` is a float64 array.

That is documented as a deliberate design choice, in three places
(`ndarray.h:125`, `FEMM-MODIFICATIONS.md:34`, `testing/UlabInt32Test.py:11`).
Two of the arguments given for it do not survive checking, and the third is a
cost estimate that is roughly double the real figure.

### It is a divergence from NumPy, and the cited precedent is not one

Measured against CPython NumPy 2.4.4:

| | NumPy | ulab here |
|---|---|---|
| `int32 - 1`, `+ 1`, `* 2`, `- int32`, `// 1` | **int32** | float64 |
| `int32 / 1`, `int32 - 1.0` | float64 | float64 |
| `int16 - 1` | int16 | **int16** ✓ |
| `uint16 + int16` | **int32** | float64 |

Two things follow. First, it is not "how ulab works" — every *upstream* integer
dtype preserves itself under arithmetic, matching NumPy. Only femm-qt's addition
widens. Second, the precedent quoted in all three places — *"upstream already
applies the same rule to a combination it cannot represent: `uint16 + int16 =>
float`"* — is a limitation this patch removed. NumPy returns `int32` there.
Upstream widens that case **because it has no 32-bit integer**, which is exactly
the type we added and then declined to promote into.

### Three gaps, papered over by one workaround

1. **`ndarray_from_mp_obj` was never taught int32** — zero occurrences of
   `NDARRAY_INT32` in it. A Python int scalar can never become int32, and any
   value outside `[-32768, 65535]` becomes float64 even though int32 holds it:

   ```c
   if((ivalue < -32768) || (ivalue > 65535)) { ...NDARRAY_FLOAT... }
   else if(other_type == NDARRAY_INT8 || other_type == NDARRAY_INT16) { ... }
   else { ...UINT8 or UINT16... }          // int32 never considered
   ```

   So even with the kernels fixed, `a32 - 100000` would still go to float.

2. **No `BINARY_LOOP` leaf produces `int32_t`.** The upcast function says so
   outright: *"int32 + int32 also becomes float rather than an int32 the kernels
   cannot produce."*

3. **`ndarray_upcast_dtype` short-circuits int32 to float** ahead of the
   identical-dtype rule (`ulab_tools.c:118`). Compiled only for `np.where` here,
   since `NDARRAY_BINARY_USES_FUN_POINTER` is 0.

`ndarray_widen_int32()` hides 2 and 3 at the single point where both operands are
known to be ndarrays. It is a workaround for unfinished work, not a design.

## Why it matters

int32 was added to index a mesh. The operations you perform on a mesh index are
`- 1`, `+ 1`, comparison, and use as a gather index — and the first two are
precisely what the widening breaks, because ulab's `np.take` rejects a float
index:

```python
np.take(x, conn[:, 0] - 1)                              # TypeError: can't convert float to int
np.take(x, np.array(conn[:, 0] - 1, dtype=np.int32))    # the working spelling
```

So the dtype delivers storage but not use. This surfaced while designing the
bulk mesh getters, whose `mo_getallelements()` returns int32 connectivity and
group columns — see `bulk-getters-design.md`.

## The cost estimate is about double

`FEMM-MODIFICATIONS.md` cites "a matrix of 126 dispatch sites". That count is
real but it is the wrong denominator — about half of it needs nothing:

| op | `BINARY_LOOP` leaves | needs int32 leaves? |
|---|---|---|
| `logical` | 30 | no — bitwise |
| `true_divide` | 25 | **no** — always yields float, and `ndarray_get_float_function` already has an int32 case (`ulab_tools.c:63`) |
| `subtract` | 25 | yes |
| `modulo` | 16 | yes |
| `multiply` | 15 | see the decision below |
| `add` | 15 | yes |

Every integer type fits in int32, so each new leaf has result type `int32_t` with
no per-pair reasoning. Commutative ops (`add`, `multiply`) already fall back to a
swapped re-dispatch, so they need only the int32 *row*, not the column.

Comparisons already work — `compare.c` has int32 cases, and `grp == 3` returning
a usable boolean mask is verified.

Estimate: **~50-70 mechanical `BINARY_LOOP` lines**, plus one branch in
`ndarray_from_mp_obj` and one rule in `ndarray_upcast_dtype`. Contained; not a
fork of the arithmetic core.

## Scope

**In:**

* `ndarray_from_mp_obj` (`ndarray.c`) — honour `other_type == NDARRAY_INT32`, and
  widen the int-scalar range to int32 before falling through to float.
* `ndarray_upcast_dtype` (`ulab_tools.c`) — int32 op int → int32; int32 op float
  → float. Frees `uint16 + int16` to return int32, matching NumPy.
* int32 rows/columns in `ndarray_binary_add`, `_subtract`, `_modulo`, and
  `_multiply` (subject to the decision below).
* Delete `ndarray_widen_int32()` and its two call sites.
* A dtype **sweep** test (below), and update the three places that document the
  old rule: `ndarray.h:125`, `FEMM-MODIFICATIONS.md:34`,
  `testing/UlabInt32Test.py`.

**Out:**

* int64/uint32/float32. The reason recorded for int64 still holds — it cannot
  round-trip through float64 above 2^53.
* `NDARRAY_BINARY_USES_FUN_POINTER`. Leave at 0; switching it is a separate
  question with its own performance answer.
* `.astype()` in the numpy proxy. Still worth having (see
  `project_numpy_shims`), but it becomes a convenience rather than the
  workaround it is today, so it need not block this.

## One decision to make: multiply

`FEMM-MODIFICATIONS.md:46` makes an argument for widening that *does* hold, and
only for `multiply`:

> Products of two large int32 values can exceed 2^53 and lose precision — but
> numpy's own `int32 * int32` **wraps** in that range, so float64 is the more
> accurate of the two, not the less.

That is correct. So `multiply` is a genuine choice:

* **Match NumPy** — `int32 * int32 -> int32`, wrapping above 2^31. Predictable,
  one rule, no surprise for anyone who knows NumPy.
* **Keep widening multiply only** — more accurate, but leaves one operator
  behaving differently from its siblings, which is the kind of exception that
  gets rediscovered as a bug later.

Add/subtract can also overflow int32, and NumPy wraps there too, so the same
question technically applies — but at ±2^31 for mesh indices it is theoretical,
whereas a product of two indices reaching 2^53 is merely unlikely. Recommend
matching NumPy everywhere and documenting the wrap; consistency is worth more
here than one operator's extra headroom.

## The real risk, and the only test that finds it

Not maintenance. This, recorded when int32 first landed and worth repeating:

> **Every dtype chain in ulab ends in an unconditional float branch.** A function
> not taught about a new dtype does not raise; it reads the bytes as doubles and
> returns plausible garbage.

That is how `sort`/`argsort`/`median`/`nonzero` were caught the first time.
Today the widening is an accidental safety net for the binary-op paths; removing
it exposes anything still untaught. Inspection will not find these.

### Why that fallthrough exists, and why the hazard is ours

It is tempting to read the trailing `else` as sloppiness. It is not, and the real
explanation is what makes the sweep non-negotiable. Compare the two designs.

**CPython NumPy dispatches, and raises when nothing matches.** Every ufunc carries
a list of typed loops; the input dtypes are resolved against them through the
promotion rules, and no match is an error rather than a default. Verified against
2.4.4 — nine out of nine attempts raised, none computed:

```
bitwise_and(float, float)  -> TypeError: ufunc 'bitwise_and' not supported for the input types
add on str + int           -> UFuncTypeError: did not contain a loop with signature matching types
float array as index       -> IndexError: arrays used as indices must be of integer type
```

Reinterpreting bytes is possible but must be asked for, and NumPy makes you name
which operation you mean:

```python
np.array([1,2,3], np.int32).view(np.float32)     # [1e-45, 3e-45, 4e-45]  reinterpret
np.array([1,2,3], np.int32).astype(np.float32)   # [1., 2., 3.]           convert
```

**ulab assumes a closed world instead.** The dtype set is fixed and *validated at
the boundary* — `ndarray.c:204` and `:256` both raise `"data type not
understood"` for anything outside the enum. Given that check, interior code can
legitimately treat "none of the ones I listed" as "must be float", and the
trailing `else` is correct. It also buys a real saving on the microcontrollers
ulab targets: no dispatch table, no per-loop signatures, no error strings.

**So the assumption is sound until someone extends the closed world — which is
what we did.** The boundary checks were updated when int32 landed (it appears in
both validation lists). The interior chains had to be found one at a time, and
any that were missed do not raise; they read four-byte integers as eight-byte
doubles and return plausible numbers.

The failure mode therefore inverts: in NumPy a forgotten dtype is a loud
`TypeError`; in ulab it is a wrong answer. The sweep below is doing by brute
force what NumPy's dispatch does structurally, which is why it is the acceptance
criterion and not a nice-to-have.

It also reframes what this work is. `ndarray_widen_int32()` is a hand-rolled
version of NumPy's casting step — *cast to a type the loops support, then
dispatch* — and that shape is right. The difference is only the target: NumPy
picks the narrowest safe common type from its promotion table, while the patch
always picks float64 because no int32 loops exist. Adding the loops is what makes
the cast unnecessary, so the change is additive rather than a redesign.

### The sweep

So the acceptance test is a **sweep**, not a table of expected values: iterate
`dir(numpy)`, call each function on an int32 array and on the float64 array with
the same values, and compare results and dtypes. Extend `UlabInt32Test.py`
rather than starting a new file.

Two more checks worth pinning:

* `np.take(x, conn[:, 0] - 1)` works without a recast — the motivating case.
* the four `.dtype` results in the NumPy comparison table above match NumPy.

## Ordering relative to the bulk getters

**Do this first.** Three reasons:

1. The bulk-getter design's one open question is whether `mo_getallelements()`
   returns a tuple with int32 columns or a single `(ne,7)` float64 array. The case
   for the tuple rests partly on int32 being genuinely useful; settle that here
   rather than guess.
2. Otherwise the manual documents the `np.array(x - 1, dtype=np.int32)` idiom as
   the way to use connectivity, and then has to un-document it.
3. It touches vendored ulab. The cheapest moment to do that is while nothing new
   is built on top of it.

**The decision rule if it goes badly:** if the sweep turns up a broad tail of
untaught functions, stop rather than push through — and take that as the answer
to the bulk-getter question too, shipping a single float64 array instead of the
int32 tuple.
