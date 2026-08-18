# femm-qt's changes to the vendored ulab

`ulab/code/` is upstream micropython-ulab with the modifications below applied
in place. `regen.sh` does **not** touch this directory (it removes only
`build-embed/` and `micropython_embed/`), so these survive a MicroPython
regeneration — but they would be lost by a naive re-vendoring of ulab, which is
what this file exists to prevent. Read it before updating ulab.

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

### The one semantic difference from numpy

**Arithmetic on int32 produces float64.** `int32 + int32` is a `float64` array,
not an `int32` one.

This is deliberate, and it is what kept the change small enough to be
maintainable. Every int32 value — and every sum or difference of two of them —
is exactly representable in a double, so nothing is lost; and ulab's
macro-expanded arithmetic core, the part that would have to be re-merged on
every upstream update, is untouched. Upstream already applies the same rule to
a combination it cannot represent: `uint16 + int16 => float`.

Products of two large int32 values can exceed 2^53 and lose precision — but
numpy's own `int32 * int32` *wraps* in that range, so float64 is the more
accurate of the two, not the less.

### Why not int64, uint32, float32

`int64`/`uint64` cannot round-trip through float64 above 2^53, so the
widen-to-float design would silently lose precision instead of being exactly
right — the failure this change exists to remove. `uint32` and `float32` are
the same pattern as int32 and could be added if a use case appears; nothing
needed them.

### Where the changes are

| file | what |
|---|---|
| `code/ndarray.h` | the enum value, and the rationale in full |
| `code/ndarray.c` | `ndarray_widen_int32()`, the binary-op interception, printing, unary ops, dtype validation, boolean-assignment matrix |
| `code/ulab_tools.c` | the five generic float accessor tables, and the upcasting rule |
| `code/numpy/numpy.c` | exposes `np.int32` |
| `code/numpy/numerical.c` | `sum`, `mean`/`std`, `argmin`/`argmax`, `sort`, `argsort`, `diff` |
| `code/numpy/compare.c` | widening at the compare entry, and in `nonzero()` |
| `code/numpy/vector.c`, `code/numpy/create.c` | iteration and `arange` |

### How it was verified, and how to re-verify

Every dtype chain in ulab ends in an unconditional float branch, so a function
that has not been taught about int32 does not raise — it reinterprets the bytes
as doubles. That is invisible to inspection, so the check is behavioural:
`testing/UlabInt32Test.py` pins the contract, and it was developed by sweeping
*every* callable in `dir(numpy)` with an int32 array and comparing against the
identical float64 array. That sweep is how `sort`, `argsort`, `median` and
`nonzero` were found. Re-run that sweep after any ulab update.
