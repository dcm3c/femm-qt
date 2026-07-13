# MicroPython Vendored Patches

These patches are applied to the generated `micropython_embed/` package
after every regen (`regen.sh` applies them automatically).

They fall into two groups: FEMM-specific behaviors that deliberately
diverge from CPython (01, 02), and fixes that restore CPython parity
where MicroPython falls short (03, 04).

## FEMM-specific (deliberately non-CPython — do not upstream)

### 01-objstr-complex-format.patch
`py/objstr.c`: Makes `%g`/`%f`/`%e` string formatting accept complex
values, producing CComplex::ToString output (`re+I*im`) instead of
TypeError. Two sites: the `%`-operator and `str.format()`.

### 02-objcomplex-compare-int.patch
`py/objcomplex.c`: Adds `<`/`<=`/`>`/`>=` comparison operators (compare
on real part, matching Lua CComplex) and `int()` conversion (extracts
and truncates real part).

These violate CPython semantics that MicroPython correctly follows:
- CPython raises TypeError for `"%g" % complex_val`
- CPython removed ordering comparisons from complex (no natural total order)
- CPython raises TypeError for `int(complex_val)`

They are domain-specific conventions from FEMM's CComplex type system,
not general-purpose improvements.

## CPython-parity fixes (MicroPython gaps — upstreamable in principle)

### 03-numeric-tower-attrs.patch
`py/objfloat.c`, `py/objint.c`: Exposes the CPython numeric-tower
attributes on `float` and `int` — `x.real`, `x.imag`, `x.conjugate()` —
which MicroPython omits (CPython has had them since 3.5). Without these,
generic numeric code that works in CPython fails here. `objint.c` falls
through via `MP_OBJ_SENTINEL` so the existing `locals_dict` (e.g.
`to_bytes`) still resolves.

### 04-objcomplex-integer-power.patch
`py/objcomplex.c`: Computes a small integer power by repeated
multiplication (binary exponentiation) instead of the polar
`exp(z2*ln(z1))` form.

MicroPython routes *every* complex power through the polar form, which
must evaluate `cos()`/`sin()` of a multiple of pi. Pi is not
representable in binary, so the result carries dirt: `(1j)**2` returned
`-1+1.2246e-16j` instead of exactly `-1`. The imaginary part is literally
`sin(pi_double)` — pi's own representation error — so no libm quality or
FPU setting can make it zero; only avoiding the polar route can.

This mirrors CPython's `complex_pow`/`c_powi` (same `|exponent| <= 100`
cutoff) and femm's own `CComplex pow(x, int)`. Integer exponents are now
exact and bit-identical to CPython, including signed zeros. Non-integer,
complex, and large exponents still use the polar form as before.
