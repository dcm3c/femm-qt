# The array pipeline: three gaps that are one gap

*Written 2026-08-26, after the bdwgc collector landed. Nothing here is started.
Its purpose is to stop these being picked up independently, because done in the
wrong order two of the three are nearly worthless.*

## The use case

Get a whole field over a whole mesh over a whole set of time steps into an
array, and operate on it without a Python loop. That is what the
noise-spectrum transient workload needs, and it is what every core-loss,
harmonic and sweep script approximates today by other means.

Concretely, the shape wanted is `(n_steps, n_elements, 2)` for a vector field,
or `(n_steps, n_elements)` for a scalar one.

## Why it is one gap and not three

The pipeline has three stages, and **all three are currently broken**:

| stage | what is missing | tracked in |
|---|---|---|
| **store** it | ~~a heap that can hold it~~ | **DONE** — bdwgc, `bdwgc-handoff-plan.md` |
| **shape** it | rank > 2 (`ULAB_MAX_DIMS` is 2) | `ulab-max-dims.md` |
| **fill** it | a bulk mesh getter that returns an ndarray | this note, §1 |
| **use** it | ~20 array compositions (`vstack`, `gradient`, …) | `project_numpy_shims` (memory) |

The collector fixed *store* only. A 240 MB array is now an ordinary
allocation — but it can only be rank 2, it can only be filled one element at a
time from Python, and the operations you would then want on it are the ones
still missing from the proxy.

**So the order matters.** Raising `ULAB_MAX_DIMS` alone buys a shape nobody can
fill efficiently. Adding bulk getters alone forces them to return rank-2 arrays
and re-flatten, which is what scripts already do by hand. Doing either without
the compositions leaves the user writing the Python loop anyway.

## 1. The missing piece: bulk mesh getters that return arrays

`mo_getallelements` exists, and is **wire-only by design**: it builds a
tab-separated string (`py_mag_postproc.cpp:244`, `buf.reserve(ne * 70)`) and
hands it to `py_set_result`, so in-process it returns `None`. That is correct
for what it is — the IPC contract — and should not change.

What does not exist is an in-process equivalent. So a script loops:

```python
for m in range(ne):
    e = mo_getelement(m + 1)          # returns a 7-tuple
    cx[m] = e[3]; cy[m] = e[4]; ar[m] = e[5]; gp[m] = e[6]
```

which is exactly what `examples/mySPMLossScriptAGB.py:143` does. **Measured**,
`examples/my_SPM_motor_AGB` — 18,016 elements, 10,494 nodes:

```
per-element loop            229 ms     12.7 us/element
mo_getpointvalues x18016     81 ms      4.5 us/call
```

12.7 µs per element is a tuple allocation, seven boxed floats and a bind, per
element. Filling an ndarray from C++ is sub-microsecond. On the 156k-element
meshes the transient work uses, the loop is ~2 s per extraction and is repeated
per sweep step.

The shape the plan sketched (`bdwgc-handoff-plan.md` §9, "the API half of the
original question"):

```
mo_getnodes()     -> (nn, 2)  f8      node coordinates
mo_getelements()  -> (ne, 3)  i4      connectivity
                     (ne, 2)  f8      centroids
                     (ne,)    f8      area
                     (ne,)    i4      group
mo_getb()         -> (ne, 2)  c16     or (ne,) c16 per component
```

Built in C++ straight into `gc_alloc_atomic()` payloads, so they are never
scanned. The wire-format `mo_getall*` are untouched.

Note `mo_getb` **does not exist in-process at all** today — not as a bulk
getter and not as a scalar one; `mySPMLossScriptAGB.py:164` carries the comment
"No mo_getb; getpointvalues[1] and [2] are Bx and By". It is one of the 62 names
in `pyfemm-inprocess-compat.md`, so the scalar form is already owed.

### The trap worth stating

`mo_getpointvalues(cx[m], cy[m])` at each centroid — which is what the SPM
script does to read B per element — is a **point location** per call: a search
for which element contains that point, when the caller already knows the
element index. Measured at 4.5 µs, so ~81 ms for this mesh per time step, and
it scales worse than linearly with mesh size. A bulk getter reading the
element's own stored value removes the search entirely, not just the loop
overhead. Any benchmark comparing "loop vs bulk" must not accidentally compare
"point-location vs direct read" and credit the wrong change.

## 2. Rank, and why it is coupled

`(nn, 2)` node coordinates are rank 2, so the getters themselves fit today. But
the *use* case is `(n_steps, n_elements, 2)`, which is rank 3. With
`ULAB_MAX_DIMS = 2` a caller must flatten by hand — exactly the
`bx_ts = np.zeros(len(iron_idx) * ns)` in the SPM script — and index with
`i * ns + kk`, which is where off-by-one bugs live.

`ulab-max-dims.md` has the details, including the finding that raising it today
makes `np.linalg.det` return silently wrong answers.

## 3. The compositions, and why they are last but not optional

Having `(n_steps, n_elements)` is not much use without `gradient` (dB/dt for
loss), `percentile` and `histogram` (loss distribution), `vstack`/`stack`
(assembling per-step results), `cumsum` (integrating), `unique` and
`searchsorted` (group handling). Those are in the ~20 "small compositions" and
~12 "real work" categories in the numpy-shims memory.

Its three design rules apply and are the valuable part — particularly **refuse
arguments you do not implement, never ignore them**: a `gradient` that silently
drops a spacing argument returns a plausible wrong number, which for a loss
calculation is the worst possible failure.

## Suggested order

1. **Bulk getters at rank 2** — `mo_getnodes`, `mo_getelements`,
   `mo_getb`/`mo_geta` per element. Useful immediately and on their own: they
   remove the 12.7 µs/element loop and the per-element point location from every
   existing script, with no other change. Add `mo_getb`'s scalar form while in
   there, since it is owed anyway.
2. **The clinalg shape fix and its tests** (`ulab-max-dims.md` steps 1–3).
   Independent of everything else, fixes a latent correctness bug, and must
   precede the flag.
3. **`ULAB_MAX_DIMS = 4`**, then extend the getters with a step axis.
4. **The compositions**, driven by what a real transient post-processing script
   turns out to need rather than by completing a list.

Step 1 is the one with standalone value. Steps 2–4 are only worth doing if the
transient noise-spectrum work is actually going to be written in Python against
these arrays — which is the question to answer before starting any of it.

## What is deliberately not here

Making `mo_getall*` return arrays in-process. They are the wire contract, they
are correct as they are, and `project_bulk_getters` (memory) records that
returning `None` in-process is by design rather than an oversight. New names,
not changed ones.
