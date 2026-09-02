# femm-qt's changes to the vendored ulab

`ulab/code/` is upstream micropython-ulab with the modifications below applied
in place. `regen.sh` does **not** touch this directory (it removes only
`build-embed/` and `micropython_embed/`), so these survive a MicroPython
regeneration — but they would be lost by a naive re-vendoring of ulab, which is
what this file exists to prevent. Read it before updating ulab.

**Vendored version: ulab 6.12.0** (`ULAB_VERSION` in `code/ulab.c`). Record the
new number here when you update.

## Re-applying after an upstream ulab update

**There is no patch mechanism for ulab, and that is the thing to understand
before starting.** The MicroPython core has one — seven numbered files in
`micropython/patches/`, which `regen.sh` re-applies automatically and which
*fail loudly* if upstream has moved (`FAILED: NN-....patch — manual re-apply
needed`, then `exit 1`). ulab has none of that. `ulab/code/` is edited in place,
this file is the only record, and an upstream merge that clobbers a change
produces a clean compile and a working build.

So the update procedure is: diff the new upstream against `ulab/code/`, re-apply
each item below by hand, then run the checks. The reason to be careful is in the
last column — **almost nothing here fails loudly.**

| # | change | files | how it fails if dropped |
|---|---|---|---|
| 1 | `NDARRAY_INT32` dtype | `ndarray.h`, `ndarray.c`, `ulab_tools.c`, `numpy/{numpy,numerical,compare,vector,create}.c` | `np.int32` disappears, or worse, survives partially — an untaught function reads int32 bytes as doubles and returns plausible numbers |
| 2 | int32 arithmetic kernel | `ndarray_operators.{c,h}` + **two routing points**: `ndarray_binary_op()` in `ndarray.c` and `bitwise_binary_operators()` in `numpy/bitwise.c` | drop the kernel and it will not link. **Drop either routing point and it compiles**: the `ndarray.c` one silently restores widen-to-float (`conn - 1` goes float, `np.take` refuses it); the `bitwise.c` one returns `MP_OBJ_NULL`, which MicroPython reports as "local variable referenced before assignment" — an error pointing nowhere near the cause |
| 3 | int32 scalars in `ndarray_from_mp_obj()` | `ndarray.c` | silent: `int32_array - 1` goes back to float64 because the `1` becomes a uint8 |
| 4 | `np.delete` overrun fix (rewind before `HEAPSORT1`) | `numpy/transform.c` | silent heap corruption; the crash lands at an unrelated allocation later. **Check whether upstream has fixed it before re-applying** |
| 5 | atomic ndarray payloads | `ndarray.c` (`gc_alloc_atomic`) | silent: arrays go back under the collector's scanner — slower collections and false retention, no failure |
| 6 | complex support / `ULAB_MAX_DIMS` config | `ulab.h` | `ULAB_MAX_DIMS` must stay **2**; see `docs/ulab-max-dims.md` before raising it |
| 7 | `nonzero()` returns int32 indices, counted in a `size_t` | `numpy/compare.c` | **the worst one here**: back to uint16, and an input longer than 65535 returns indices reduced mod 65536 (silently wrong), while an input with more than 65535 *hits* wraps the COUNT and then overruns the allocation it just sized from it — measured as a hard SIGSEGV at 200k, and as silent heap corruption at 70k |
| 8 | `arange()` escapes int16 → int32 rather than → float | `numpy/create.c` | `np.take(x, np.arange(n))` starts failing for n > 32768 and working below it — the same line, deciding on length |
| 9 | `uint16+int16` and `int8+uint16` promote to int32 | `ulab_tools.c` + the upcast test in `ndarray_binary_op()` (`ndarray.c`) | drop the `ulab_tools.c` half and the promotions revert (one to float, one silently wrong for negative int8). **Drop the `ndarray.c` half and it compiles**: the pair upcasts to int32, misses the kernel, and falls into a dtype chain that assumes float |
| 10 | the `@` operator | `ndarray.c` (handler + hook), `numpy/transform.h` (a real prototype for `transform_dot`), `libfemm/py_clinalg.cpp` (registers the hook) | loudly: `@` goes back to "unsupported types for `__matmul__`". Drop only the **prototype** and it still compiles — with a warning — and segfaults, because C then assumes `transform_dot` returns `int` |

Then run, in this order:

```
rm micropython/build-test/libmpyembed.a                  # see the note below
```

**Clear that archive before anything else.** `build_test.sh` caches the
compiled embed tree and only rebuilds it when the file is absent, so after an
edit under `ulab/code/` a stale archive fails at the LINK with an undefined
reference naming the *caller*, not the edit. Then:

```
build/bin/femm_headless -run testing/UlabInt32Test.py    # the sweep — see below
build/bin/femm_headless -run testing/GcTest.py           # atomic payloads
build/bin/femm_headless -run testing/FftTest.py          # Bluestein + the fast path
bash testing/_verify_all.sh                              # everything else
```

Two structural checks that no test can make for you, because both fail silently:

* **`ndarray_new_ndarray()` must still be the only payload allocation site** — a
  new `m_new(byte, ...)` for payloads upstream puts arrays back under the
  scanner. (Detail in the atomic-payload section below.)
* **The dtype chains must still not contain `NDARRAY_INT32`.** That is the
  invariant the kernel design buys: if a future change adds int32 leaves to the
  chains as well, the two implementations will disagree somewhere and only the
  sweep will notice.

## int32 (2026-08-16)

Upstream's integer dtypes stop at 16 bits: `int8`, `uint8`, `int16`, `uint16`,
plus one float (whichever `mp_float_t` is) and complex. That is a sound
tradeoff for a microcontroller — ulab's arithmetic is macro-expanded over every
*pair* of dtypes, so each new type widens a matrix of 126 dispatch sites — and
the wrong one for a desktop finite-element program, where 32767 does not index
a mesh and exceeding it wraps silently:

```python
np.array([40000], dtype=np.int16)[0]     # -25536, no warning
```

MicroPython itself is not the constraint: its own `array` module has
`i`/`I`/`l`/`L`/`q`/`Q`, and `mp_binary_get_size`/`mp_binary_get_val_array`
already understand those typecodes. The limit is ulab's alone.

### What was added

`NDARRAY_INT32 = 'i'` (4 bytes, signed), supported for construction, element
access and assignment, slicing and views, boolean masking, `reshape`,
`tobytes`/`frombuffer`, printing, sorting, and every one-array numpy function.
`np.int32` is exposed from the `numpy` module, which is why the qstr pool had
to be regenerated (`regen.sh`).

### Arithmetic (revised 2026-08-28)

int32 is a **full dtype**: `int32 + int32` is an `int32` array, and integer
semantics match numpy — `+`, `-`, `*` wrap; `//` and `%` floor toward negative
infinity; division or modulo by zero yields 0; a negative integer power raises;
comparisons give a boolean array; `/` is the one operator that leaves the
integers. In-place operators follow what ulab already does for its own integer
dtypes: `/=` and `**=` are refused, `+=`, `-=`, `*=`, `%=` preserve the dtype.

It is **not** implemented by adding an int32 row and column to each of the ten
macro-expanded dtype chains — that is ~100 leaves to re-apply on every upstream
update, and a missed one is silent, because every chain ends in a trailing
"assume float" branch that would reinterpret the bytes. Instead
`ndarray_binary_op()` routes: int32 with float or complex (or true division) is
widened to float and handled by ulab's own chains, which therefore still never
see an int32; int32 with another integer type goes to `ndarray_binary_int32()`,
one kernel for every operator. `numpy/bitwise.c` has its own separate chains and
routes to the same kernel.

#### What this replaced, and why

Until 2026-08-28 **arithmetic on int32 produced float64**, via
`ndarray_widen_int32()`. That was exact — every int32, and every sum or
difference of two, is representable in a double — but it had two problems:

* It **diverged from numpy**, verified against CPython numpy 2.4.4, which keeps
  `int32 -1 / +1 / *2 / -int32 / //1` as int32. It was not ulab's general
  behaviour either: every *upstream* integer dtype preserves itself under
  arithmetic; only this one widened.
* It **broke the use the dtype was added for**. `conn - 1` on a 1-based mesh
  connectivity array came back float64, and `np.take` refuses a float index —
  so the type could store a mesh index but not compute one.

The rationale recorded here cited `uint16 + int16 => float` as upstream
precedent. That does not hold up: numpy returns **int32** there, and upstream
widens only because it lacks the 32-bit type this patch adds. The maintenance
argument was real but roughly double the true cost — of the 126 `BINARY_LOOP`
sites, `logical` (30) needs nothing and `true_divide` (25) always yields float.

Products of two large int32 values can exceed 2^53, which is why the old design
looked attractive — but numpy's `int32 * int32` wraps there too, so matching
numpy costs nothing that numpy itself does not also cost, and one rule for all
operators beats an exception for one.

### Why not int64, uint32, float32

`int64`/`uint64` cannot round-trip through float64 above 2^53, so the
widen-to-float design would silently lose precision instead of being exactly
right — the failure this change exists to remove.

**`uint32` is not "could be added if a use case appears" — it should NOT be
added** (revised 2026-09-01, after the question was asked directly). NumPy
promotes `int32 + uint32` to **int64**, which is ruled out above, so adding it
manufactures a type pair with no correct destination. Nor is there a use for
it: int32 reaches 2147483647, and an index that large means an array of 2.1e9
elements — 17 GB at float64 — so it cannot be approached. And index types
*want* to be signed, because `idx - 1` (1-based connectivity to 0-based, the
use int32 was added for) turns element 0 into a huge positive number on an
unsigned type instead of -1. numpy's own `nonzero`/`argsort`/`arange` all
return signed. int32 is the last integer type that fits this design.

`float32` remains the same pattern as int32 and could be added; nothing needs
it.

### Where the changes are

| file | what |
|---|---|
| `code/ndarray.h` | the enum value, and the rationale in full |
| `code/ndarray.c` | `ndarray_widen_int32()`, the binary-op routing, int32 scalars in `ndarray_from_mp_obj()`, the empty-result dtype, printing, unary ops, dtype validation, boolean-assignment matrix |
| `code/ndarray_operators.c` | `ndarray_binary_int32()` and `ndarray_inplace_int32()` — the whole arithmetic implementation, in one place |
| `code/ulab_tools.c` | the five generic float accessor tables, `ndarray_get_int32_function()`, and the upcasting rule |
| `code/numpy/bitwise.c` | routes int32 to the same kernel; raises instead of returning `MP_OBJ_NULL` for an uncovered combination |
| `code/numpy/numpy.c` | exposes `np.int32` |
| `code/numpy/numerical.c` | `sum`, `mean`/`std`, `argmin`/`argmax`, `sort`, `argsort`, `diff` |
| `code/numpy/compare.c` | widening at the compare entry, and in `nonzero()` |
| `code/numpy/vector.c`, `code/numpy/create.c` | iteration and `arange` |

## 2026-09-01 — the three places int32 had not reached

Found by asking why `np.arange` widens int16 to *float* when int32 exists. All
three predate the int32 dtype and were never revisited; all three are items
7–9 in the re-apply table above.

**`nonzero()` (`numpy/compare.c`) — wrong answers past 65535, and a heap
overrun.** The hit counter was a `uint16_t` and the indices came back in an
`NDARRAY_UINT16` array, so both the *length* and the *values* wrapped. Two
distinct failures, and the second is worse than it first looked:

* **Wrong indices**, whenever an index exceeds 65535. Measured before the fix:
  a single nonzero at position 99999 was reported as 34463, and `np.take`
  gathered element 34463 without complaint.
* **A heap overrun**, whenever the number of HITS exceeds 65535. The count
  wraps, the result array is allocated from the wrapped count, and the fill
  loop then writes the true number of entries into it. `np.nonzero(np.zeros(70000) + 1.0)`
  returned length 4464 having written 70000 — 131 kB past the end — and
  survived; adding a 200000 case took the process down with SIGSEGV. Same
  shape as the upstream `np.delete` overrun in the section below, and equally
  liable to crash somewhere unrelated.

FE meshes pass 65535 routinely — this tree ships a 156k-node example — and
`nonzero()` over a group mask is the obvious way to select part of one. The
tell that it is an oversight rather than a limit accepted on purpose:
upstream's own `argsort()` guards exactly this ceiling with an explicit
"axis too long".

**`arange()` (`numpy/create.c`) — escaped to float.** Upstream jumps int16 →
float because float is the only wider type it has. Visible as a length-dependent
failure, since `np.take` refuses a float index: `np.take(x, np.arange(32768))`
worked and `np.arange(32769)` did not. Note the `dtype == NDARRAY_INT16` guard
now wrapping that test — it also fires for float arguments, harmlessly upstream
because float is both the default and the escape, so retargeting it without the
guard makes `arange(100000.5)` return integers.

**Promotion (`ulab_tools.c`, `ndarray.c`) — two pairs that need a wider signed
type.** `uint16 + int16` gave float and `int8 + uint16` gave uint16, where NumPy
gives int32 for both; the second was silently *wrong* for a negative int8. The
`ndarray.h` note already identified the first as upstream precedent rather than
NumPy behaviour — the reasoning had landed, the code had not. Two halves, and
only one of them fails loudly: `ndarray_binary_op()` must also route a pair
whose *upcast* is int32 even when neither operand is, or it falls into a dtype
chain whose trailing branch assumes float.

While there: `ndarray_binary_op()`'s empty-array case kept its own hand-written
copy of the promotion table, which is how an empty result could come back a
different dtype from a non-empty one. It now calls `ndarray_upcast_dtype()`
like everything else, and that function is compiled unconditionally (its guard
was `NDARRAY_BINARY_USES_FUN_POINTER | ULAB_NUMPY_HAS_WHERE`, so turning
`np.where` off would have failed the link). `ndarray_upcast_dtype()` also grew
a complex rule, which it needed once it was typing empty results.

### An upstream bug fixed along the way

`code/numpy/transform.c`, `np.delete()`: the loop that fills `index_array`
advances the pointer as it writes, and upstream rewinds it — but only *after*
`HEAPSORT1` has already sorted `index_len` elements starting from the end of the
allocation. That is a heap overrun on every `np.delete()` call with more than one
index, for **every dtype**, and it takes the process down at some unrelated
allocation later. The rewind now happens before the sort. Not an int32 issue;
found because the dir(numpy) sweep is the first thing here to call `delete` with
an index array.

### How it was verified, and how to re-verify

Every dtype chain in ulab ends in an unconditional float branch, so a function
that has not been taught about int32 does not raise — it reinterprets the bytes
as doubles. That is invisible to inspection, so the check is behavioural:
`testing/UlabInt32Test.py` pins the contract by sweeping *every* callable in
`dir(numpy)` and comparing int32 against an oracle array — 190 calls over four
passes. That sweep is how `sort`, `argsort`, `median` and `nonzero` were found
in 2026-08, and how the `np.delete` overrun above was found in 2026-08-28.
**Re-run it after any ulab update**; it is the only thing that will notice.

Three things about the sweep that are not obvious, and that anyone extending it
will otherwise get wrong:

* **It needs two oracles.** float64 for the elementwise maths, but **int16** for
  the two-argument and 2-D passes — because `np.bitwise_and`, `np.take` and
  friends are *supposed* to reject a float array. Compared against float64 they
  report correct behaviour as a failure; compared against int16 they ask the
  sharper question, which is whether int32 is as well supported as the narrow
  dtype it exists to replace.
* **It carries a canary.** The last two checks drive the same comparator with a
  pair that must differ (int16 wrapping 40000). A sweep that reports nothing is
  worthless unless it is known to be capable of reporting something, and a
  broken comparator otherwise looks exactly like a clean run.
* **Constructors are skipped deliberately.** `zeros`/`ones`/`eye`/`full`/
  `arange`/`linspace` read their arguments as a *shape*; handed an array of
  values they try to allocate that many elements and take the process down
  before anything is reported. They also test nothing about int32 data handling.

---

## 2026-08-25 — ndarray payloads are allocated pointer-free (atomic)

### What

One line in `code/ndarray.c`, in `ndarray_new_ndarray()`:

```c
array = m_new0(byte, len);        // was
array = gc_alloc_atomic(len);     // is
```

with the `m_malloc_fail(len)` that `m_new0` used to do internally, and a new
`#include "femm_gc.h"`.

`gc_alloc_atomic()` is `GC_MALLOC_ATOMIC` plus a `memset` (see
`micropython/femm_gc.c`). Atomic means the collector never scans the block for
pointers. Under the fallback collector (`-DFEMM_GC_BDWGC=OFF`) the same name
resolves to an ordinary allocation in `femm_port_hooks.c`, so this file
compiles and behaves either way.

### Why

An ndarray payload is bulk numeric data. It never contains a pointer, and the
old allocation left the collector scanning every byte of it. Two consequences,
both measured (`docs/bdwgc-handoff-plan.md` §0):

* **Scan cost.** `gc.collect()` with a 48 MB float64 array live took 28 ms; not
  scanning it takes ~0. At 240 MB the difference is the whole collection.
* **False retention.** 500 int32 pairs whose bit patterns happened to spell
  heap addresses pinned ~520 KB of dead objects. Numeric data that a
  conservative collector reads as pointers is not a hypothetical: an int32 pair
  is exactly 64 bits, and mesh index arrays are full of small integers.

### The re-verification rule, after any ulab update

**Grep that `ndarray_new_ndarray()` is still the only payload allocation site.**
It is the funnel today — `ndarray_new_dense_ndarray`, `_linear_array`,
`copy_view`, `frombuffer` and all of `numpy/*.c` reach it — but an upstream
change that adds a second `m_new(byte, ...)` for payloads would silently put
those arrays back under the scanner, and nothing would fail. The remaining
`m_new` calls in `linalg.c`, `poly.c` and `optimize.c` are small scratch
buffers and are deliberately left alone.

`ndarray_new_view()` and `frombuffer()` need no change: `origin` already points
into the payload, and the collector runs with interior pointers enabled, so any
pointer into a payload keeps it alive. `testing/GcTest.py` pins both cases —
a view outliving its base, and `frombuffer(offset=8)` outliving its source.

## 2026-09-01 — the `@` operator

`A @ B` raised "unsupported types for `__matmul__`". Nothing was missing from
MicroPython: its compiler already emits `MP_BINARY_OP_MAT_MULTIPLY` and
delivers it to the ndarray type's `binary_op` slot. Upstream ulab simply has no
case for it, so it fell out of the switch as `MP_OBJ_NULL`.

Routed to `transform_dot()`, the same code `np.dot` uses. That is exact rather
than approximate: numpy distinguishes `matmul` from `dot` only above two
dimensions, and `ULAB_MAX_DIMS` is 2 here. **Raising MAX_DIMS makes them
differ** — add that to the list in `docs/ulab-max-dims.md`.

Two things about it are not obvious, and both cost time:

* **The handler's POSITION.** It sits immediately after the operands become
  ndarrays and *before* `ndarray_can_broadcast()`, not in the switch below.
  Matmul shapes are deliberately not broadcastable — `(2,3) @ (3,4)` is the
  entire point — so a case in the switch would reject every non-trivial matrix
  product with "operands could not be broadcast together".
* **`numpy/transform.h` declared only the callable OBJECT**, never the
  function. Calling `transform_dot()` from `ndarray.c` therefore compiled with
  nothing worse than `-Wimplicit-function-declaration`, C assumed a return type
  of `int`, the `mp_obj_t` pointer was truncated to 32 bits, and every `@`
  segfaulted. The prototype is now in the header.

**Complex goes through a hook.** `ndarray_matmul_complex_hook` is a function
pointer in `ndarray.c`, NULL in a plain ulab build, set by
`libfemm/py_clinalg.cpp` to a wrapper over the clinalg kernel. It exists
because the `@` handler is C inside ulab and cannot reach the Python-level
numpy proxy where complex support lives — without it `A @ B` would raise
"not implemented for complex dtype" on arrays `np.dot(A, B)` handles, which is
a worse state than not having `@`. Coercion of a real operand is `x * (1+0j)`,
ulab's own multiply, exactly as the proxy's `_asc()` does it.

Known divergence, deliberately not chased: `a @= b` rebinds rather than writing
in place, so where numpy raises on an output shape that does not match the left
operand, this quietly rebinds. Every other observable is the same.
