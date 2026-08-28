# Raising ULAB_MAX_DIMS from 2 to 4

*Investigated 2026-08-26. Not started. The flag flip is one line; the work is
everything the flag flip breaks.*

## What the setting is

`micropython/ulab/code/ulab.h:48` — the maximum **rank** of an ndarray. Legal
values 1–4, currently **2**, so `np.zeros((3,4,5))` raises
`TypeError: too many dimensions`.

It is a **code-size** setting, not a memory one. ulab uses it at 717 sites as
`#if ULAB_MAX_DIMS == N` blocks with the nested loops written out separately per
rank, so raising it duplicates loop bodies throughout the library. ulab's own
comment frames it as flash conservation, because ulab is a microcontroller
library. On a desktop that trade is close to free — **measured +132 KB, +1.8%**
on `femm_headless` (7.31 → 7.44 MB), building clean at 4 with no source changes.

So the bdwgc work is not what unblocks this. What bdwgc changed is that arrays
of interesting size can now exist at all, which is what makes a rank limit start
to bite rather than being academic.

## Why it would be worth having

The recurring shape in this codebase is a time series of per-element field data.
`examples/mySPMLossScriptAGB.py` already writes

```python
bx_ts = np.zeros(len(iron_idx) * ns)      # flattened by hand
```

which is a 2-D `(ns, n_iron)` array flattened into 1-D, because the natural form
for a *vector* field over time — `(ns, n_elements, 2)` — is 3-D and cannot be
expressed. The transient work has exactly this shape. So does any parameter
sweep with more than one swept variable.

## What it breaks, and why it is not a flag flip

**femm-qt's complex linear algebra produces silently wrong answers at
`MAX_DIMS=4`.** Measured, same script, two builds:

```
MAX_DIMS=2:  np.linalg.det([[4,1],[1,3]])  ->  (11+0j)     correct
MAX_DIMS=4:  np.linalg.det([[4,1],[1,3]])  ->  0j          WRONG, no error
             np.linalg.matrix_rank(...)    ->  RuntimeError("rank: failed")
```

`rank` at least fails loudly. `det` does not.

### The cause

ulab stores `shape[]` **right-aligned**: `ndarray.c:113-118` assigns
`shape[ULAB_MAX_DIMS-1] = d` (the last dimension), `[ULAB_MAX_DIMS-2] = c`, and
so on. A 2-D array under `MAX_DIMS=4` therefore has `shape = {1, 1, rows, cols}`.

`libfemm/py_clinalg.cpp` builds shapes **left-aligned**:

```c
size_t vsh[ULAB_MAX_DIMS] = { (size_t)n, (size_t)n };
```

At `MAX_DIMS=2` that is `{n, n}` and both conventions coincide, which is why
this has been latent since the clinalg work landed. At `MAX_DIMS=4` it is
`{n, n, 0, 0}`, and `ndarray_new_dense_ndarray()` reads the last two entries —
so it builds a 0×0 array.

Note the asymmetry: **the reading side is already correct.** `mat_dims()`
(`py_clinalg.cpp:91-94`) reads `shape[ULAB_MAX_DIMS-2]` and `[-1]`, which is
right-aligned and works at any setting. Only construction is wrong.

### The nine sites

`libfemm/py_clinalg.cpp`, lines 60, 78, 79, 114, 115, 138, 150, 180, 202 —
`eig`, `qr` (×2), `svd` (×2), `solve`/`lstsq`, `inv`, an intermediate, and
`pinv`. Each needs the dimensions moved to the end of the array rather than the
start. A helper — `shape2(m, n)` returning a properly right-aligned
`size_t[ULAB_MAX_DIMS]` — would remove the class of bug rather than fixing nine
instances of it, and would read correctly at 2 as well.

### One more assumption to retire

`libfemm/py_init_script.cpp:381`, in `_fft_along`:

> *"ulab is built with ULAB_MAX_DIMS=2, so there is no third case."*

An explicit rank-2 assumption in the FFT axis handling. It is correct today and
becomes wrong at 4. Grep for others before starting; these two files are the
only ones in `libfemm/` that mention `ULAB_MAX_DIMS` at all.

## What the tests did not catch, and why

`NumpyProxyTest.py` exercises `matrix_rank` and would have caught the loud
failure. **Nothing caught the wrong determinant**, because every clinalg test
uses square 2-D inputs, where the left- and right-aligned conventions agree at
`MAX_DIMS=2` and the wrongness only appears once the setting changes.

So this work needs its tests extended *first*, not after:

* a non-square case for every function that builds an output shape — `qr`,
  `svd`, `lstsq`, `pinv` are where an m/n transposition hides;
* a value check on `det`, `inv` and `solve`, not just "it returned something";
* once at 4, a genuine 3-D case: construct, index, slice, reduce along each
  axis, and round-trip through the PyBridge to matplotlib.

The existing suite passing at `MAX_DIMS=4` would otherwise mean very little: on
the trial build, 41 of 45 rows passed *while `det` was returning zero*.

## Suggested order

1. Add the non-square and value-checking clinalg tests **at `MAX_DIMS=2`**,
   where they must pass. This is useful on its own — it closes a real gap.
2. Introduce the right-aligned shape helper and convert the nine sites. Still at
   2; the suite must stay green, byte-for-byte on `FQReference`.
3. Retire the `_fft_along` rank-2 assumption.
4. Flip `ULAB_MAX_DIMS` to 4 in `ulab.h`, record it in
   `micropython/ulab/FEMM-MODIFICATIONS.md` as a dated section like the int32
   one, and add 3-D coverage.
5. Re-measure binary size on all platforms, and check whether the extra
   branching costs anything in the 1-D/2-D hot paths (`Fft` and
   `PyBridgeBench` are the sensitive rows).

Steps 1–3 are worth doing regardless of whether the flag is ever flipped: they
fix a latent correctness bug and a gap in the tests. Only step 4 costs 132 KB.

## Aside: how this was found

By building at `MAX_DIMS=4` and running the full suite, which is also how the
`matlib.dat` staging gap surfaced — the material libraries are hand-copied into
`build/bin`, exactly like `femm.lua` was before `06288f9`, so a fresh build
directory has no material libraries and `MaterialLibTest` fails. Unrelated to
this note, but worth fixing the same way.
