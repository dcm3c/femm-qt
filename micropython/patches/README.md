# MicroPython Vendored Patches

These patches are applied to the generated `micropython_embed/` package
after every regen (`regen.sh` applies them automatically). They are
FEMM-specific behaviors that violate standard Python semantics and are
not candidates for upstream contribution.

## Patches

### 01-objstr-complex-format.patch
`py/objstr.c`: Makes `%g`/`%f`/`%e` string formatting accept complex
values, producing CComplex::ToString output (`re+I*im`) instead of
TypeError. Two sites: the `%`-operator and `str.format()`.

### 02-objcomplex-compare-int.patch
`py/objcomplex.c`: Adds `<`/`<=`/`>`/`>=` comparison operators (compare
on real part, matching Lua CComplex) and `int()` conversion (extracts
and truncates real part).

## Why not upstream?
All three behaviors deliberately violate CPython semantics that
MicroPython follows:
- CPython raises TypeError for `"%g" % complex_val`
- CPython removed ordering comparisons from complex (no natural total order)
- CPython raises TypeError for `int(complex_val)`

These are domain-specific conventions from FEMM's CComplex type system,
not general-purpose improvements.
