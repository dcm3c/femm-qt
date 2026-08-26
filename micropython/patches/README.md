# MicroPython Vendored Patches

These patches are applied to the generated `micropython_embed/` package
after every regen (`regen.sh` applies them automatically).

**These files are modified third-party code, and are treated as such:**
MicroPython's own copyright header stays at the top of every patched file, no
femm-qt copyright is added to any of them, and each change is marked inline
with a `FEMM patch NN` comment so a reader can tell ours from upstream's
without a diff. MIT does not require a modification notice the way Apache-2.0
does, but leaving a patched file indistinguishable from upstream would be
unkind to whoever next compares the two. (Contrast `pylib/`, where nothing
vendored is modified at all — see `pylib/README.md`.)

**Write patch files with LF line endings.** The regenerated sources are copied
straight from upstream and are LF; a patch saved with CRLF has CRLF in its
context lines and `git apply` then finds no match, failing with a bare
"patch does not apply". Generating with `git diff ... > file.patch` from bash
is safe; a Python `write_text()` on Windows is not. Patch 07 was written that
way and failed on the next regen, with nothing in the message to suggest line
endings.

They fall into three groups: behaviors that deliberately diverge from
CPython (01, 02), fixes that restore CPython parity where MicroPython
falls short (03, 04), and platform correctness (05, 06, 07). 01's
divergence is narrow — it accepts a complex where CPython raises, but
prints what CPython prints.

## FEMM-specific (deliberately non-CPython — do not upstream)

### 01-objstr-complex-format.patch
`py/objstr.c`: Makes `%g`/`%f`/`%e` string formatting accept complex
values instead of raising. Both the `%`-operator and `str.format()`.

The output is CPython's own — `format(3+4j, 'g')` is `3+4j`, `format(1j,
'g')` is `0+1j`, `format(3+0j, 'g')` is `3+0j`, and a signed zero real
part survives as CPython keeps it. Nothing is collapsed or parenthesised;
that is `str()`/`repr()`'s job, and MicroPython already matches CPython
there. The `a+I*b` spelling belongs to the TCP wire format alone
(libfemm/py_state.cpp), which is an interface contract with the octave /
matlab / mathematica clients — it has no business in a script's own
output. This patch printed it until 2026-08.

The two paths share one helper (`femm_print_complex`), which renders into
a vstr and then pads it as a unit, so width and alignment mean the same
thing for a complex as for a real: `"{:14g}"` right-aligns either.

`str.format()` needed a third change to work at all. Its float branch is
guarded by `arg_looks_numeric()`, which knows about int and float only, so
a complex fell through to the "not a number" branch and raised
`unknown format code 'g'` — the complex case in the switch was written but
unreachable, and only `%g` ever used it. The guard (and the default-
alignment test beside it) now admit complex. Type codes that make no sense
for a complex — `{:d}`, `{:s}` — still land on the same "unknown format
code" error a float would give, which is also what CPython raises.

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

## Platform correctness

### 05-vfs-posix-windows-paths.patch
`extmod/vfs_posix_file.c`, `extmod/vfs_posix.c`: Windows only. Routes the
VFS's file open and its import-time existence test through
`femm_wopen_utf8` / `femm_wstat_utf8_kind` (femm_port_hooks.c) instead of
the CRT's narrow `open()` and `stat()`, and opens in binary mode.

**UTF-8 paths.** MicroPython strings are UTF-8, but the narrow calls
decode a path as ANSI. libfemm widens to UTF-16 at the OS boundary
(`common/utf8_io.h`), so before this patch
`open("C:/temp/Prüfung/x.txt", "w")` and
`mi_saveas("C:/temp/Prüfung/x.fem")` reached two *different* directories
— and Python's side round-tripped consistently with itself, which made
the disagreement invisible until something crossed between them. The
import-stat call is the same problem one level up: `import mymodule` did
not find a module sitting in a non-ASCII directory on `sys.path`.
Non-UTF-8 input falls back to the narrow call, so an ASCII path behaves
exactly as before.

**Translation mode.** The open now states `_O_BINARY` or `_O_TEXT`
instead of inheriting the CRT's global default, which is text for *every*
mode — including `"rb"` and `"wb"`. Text mode on a binary file rewrites
`\n` as `\r\n` both ways, which broke two things: `f.write(b"\n\n\n")`
put 6 bytes on disk, and `import` of a script-written module failed once
the file exceeded the reader's ~48-byte buffer — the collapsing makes a
full read come back short, `mp_reader_vfs_readbyte` reads a short read as
EOF, and the module is truncated mid-statement into a `SyntaxError`.
(Under the buffer size it worked, which is why it survived.)

Text mode keeps its translation, because that is what CPython does on
Windows: its text mode writes `os.linesep` and reads it back as `\n`, so
the string round-trips while the byte count does not. Bare-metal
MicroPython ports translate nothing, but a script here is far more likely
to be written by someone with CPython habits — and a results file that
opens correctly in Notepad matters more than matching a port with no
filesystem.

`os.*` does not depend on this patch (libfemm/py_os.cpp is native and
already widens); it is `open()` and `import` that route through the VFS.

### 06-modtime-host-timezone.patch
`extmod/modtime.c`: Gives the port a real `gmtime`, a local `localtime(t)`
and a local `mktime`, under `MICROPY_PY_TIME_HOST_TIMEZONE`.

Upstream assumes a machine with no timezone, which a microcontroller is
and a desktop is not. It aliases `gmtime` to `localtime` and converts a
passed-in timestamp with `timeutils_seconds_since_epoch_to_struct_time`,
which is pure UTC arithmetic. The port supplies only the *current* time
(`mp_time_localtime_get`, and femm's is local, as it should be), so the
two forms ended up returning each other's answer:

| call | before | correct |
|---|---|---|
| `time.localtime()` | local ✓ | local |
| `time.gmtime()` | local ✗ | UTC |
| `time.localtime(t)` | UTC ✗ | local |
| `time.gmtime(t)` | UTC ✓ | UTC |
| `time.mktime(lt)` | UTC inverse ✗ | local inverse |

Anything built on those inherited it. `datetime.now()` — the reason this
was found — reported UTC wearing a local label, four hours off in EDT,
and `mktime(localtime(t)) != t` everywhere but Greenwich.

The patch factors the 8-tuple build out of `time_localtime` so a separate
`time_gmtime` can share it, then routes the three timezone-aware
conversions to functions the port's `MICROPY_PY_TIME_INCLUDEFILE` must
supply (`micropython/femm_modtime.c`): `mp_time_localtime_epoch`,
`mp_time_now_seconds`, `mp_time_mktime_local`. All of it is `#if`-guarded
on a macro that defaults to 0, so an unpatched port is byte-for-byte
unchanged — upstreamable in principle for the unix port, which has the
same bug.

Two mpconfigport.h flags go with it, and are part of the same fix:
`MICROPY_TIME_SUPPORT_Y1969_AND_BEFORE` / `..._Y2100_AND_BEYOND`. Upstream
enables them only on 64-bit `MP_SSIZE_MAX`, which this build does not have,
so `timeutils` was using an *unsigned* timestamp — `time.gmtime(-1)` came
back as year 53052 (a huge day count with the year truncated into a
`uint16_t`) instead of 1969-12-31. `datetime.fromtimestamp(0)` tripped
over exactly that, because it probes one day earlier to detect DST.

### 07-vfs-newline-arg.patch
`extmod/vfs.c`, `extmod/vfs_posix_file.c`: adds `newline=` to `open()`,
supporting `None` (the default, unchanged) and `""`.

`open(path, "w", newline="")` is how CPython's own csv documentation says to
open a CSV file, and it raised `TypeError` here. The consequence is Windows-
specific and looks like a csv bug rather than an `open()` one: the writer emits
`

`, text mode then translates the `
` to `

`, every record ends
`


`, and Excel shows a blank row between each pair. CPython behaves the
same way, which is precisely why the parameter exists.

Threaded to the file layer as a private mode character (`'N'`) rather than as a
new argument, because the VFS `open` protocol passes exactly two and widening
it would touch every VFS type. Mode parsers ignore characters they do not know,
so the marker is inert for the VFS types that never translated anything. The
file stays *text* at the Python level (str in, str out) and becomes
untranslated at the OS level -- which is what `newline=""` means.

`"
"`, `"
"` and `"

"` are **rejected with ValueError** rather than
accepted. They have real CPython semantics (translate to the given terminator)
that are not implemented, and accepting them while doing something else is the
worse failure. `newline=""` is accepted and validated on every platform, so a
script stays portable; only Windows text mode has anything to stop doing.

No mpconfigport.h change and no new qstr (`MP_QSTR_newline` was already in the
generated header), so this needs no regen and no re-stamp.

---

## Not a patch: `py/gc.c` is not compiled at all

Since 2026-08-25 the collector is bdwgc. `py/gc.c` stays in the regenerated
tree — a regen will keep producing it — but `CMakeLists.txt` filters it out of
the source list, and `micropython/femm_gc.c` implements the same `py/gc.h`
interface on top of `third_party/bdwgc` instead. See
`docs/bdwgc-handoff-plan.md`.

There is nothing to re-apply after a regen. What there *is* to do, when bumping
MicroPython:

* **Re-read `py/gc.h` against `femm_gc.c`.** The shim implements that header;
  a new entry point added upstream is a link error at best and a silently
  missing behaviour at worst. The interface has been stable since the
  split-heap work in 1.20, and the likely future delta is a weakref or
  threading hook — `femm_gc.c` already stubs those under `#if`.
* **Check the CMake exclusion still matches.** It fails the configure loudly if
  `py/gc.c` moves, precisely so this cannot go unnoticed.

`mpconfigport.h` keeps `MICROPY_GC_SPLIT_HEAP*` under `#ifdef
FEMM_GC_MICROPYTHON`, which `-DFEMM_GC_BDWGC=OFF` sets. That fallback exists so
a field problem can be bisected to the collector in one build; it is meant to
be removed after a release without incident.
