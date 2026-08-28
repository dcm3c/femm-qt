# bdwgc handoff plan: replacing MicroPython's fixed heap with the Boehm collector

Status: design + measured prototype, ready for implementation.
Tree this was written against: `femm-qt` @ `9cefef7` (2026-08-23), MicroPython 1.28.0, vendored ulab.
Companion files: `micropython/femm_gc.c`, `micropython/femm_gc.h`, `testing/GcTest.py`, `tests/gc_harness.c`.

## 0. Decision and evidence

`py/gc.c` is replaced, whole, by a ~150-line implementation of the same `py/gc.h`
interface on top of the Boehm-Demers-Weiser collector (bdwgc, MIT/X11-style
licence). MicroPython keeps its object model, conservative pointer discovery,
finalisers and `gc` module; it stops having a heap of its own. `s_heap`,
`MICROPY_GC_SPLIT_HEAP*`, `gc_get_max_new_split()` and the `close_py()` area-
freeing loop all go away. ulab array payloads become pointer-free (`atomic`)
allocations, so bulk numeric data is never scanned.

This supersedes the earlier five-item plan (cap policy, `HostBuffer`, `owner`
field, mesh ndarray API, sweep test). Items 1–3 and 5 are unnecessary under
bdwgc; item 4 (in-process ndarray mesh getters) is still worth doing but is an
independent API change and is not part of this handoff.

Why not "just malloc": MicroPython has no rooting discipline and no per-type
trace functions. The collector must be able to take an arbitrary machine word
and decide whether it is the start of a live object; `gc.c` does that with its
allocation table over one contiguous pool. A malloc-per-object design needs the
same answer from a sparse address map plus a conservative mark-sweep over it,
which is what bdwgc is. Writing another one is not a good use of anyone's time.

Measured on the vendored 1.28 + ulab, same harness and scripts, Linux x86-64
(`tests/gc_harness.c`; the scripts are folded into `testing/GcTest.py`):

| | `gc.c` today | bdwgc shim |
|---|---|---|
| idle heap after `mp_init()` | 2 MB static | 64 KB |
| `np.zeros(9_000_000)` (68 MB) | `MemoryError` | ok |
| `np.zeros(30_000_000)` (240 MB) | `MemoryError` | ok |
| 1M-element list-of-tuples mesh copy | 0.92 s, 160 MB | 0.25 s, 145 MB |
| `gc.collect()` with that list live | 119 ms | 56 ms |
| `gc.collect()` with 48 MB float64 live | 28 ms | 0 ms |
| `gc.collect()` with 240 MB float64 live | n/a | 2.2 ms |
| int32-pair false-retention probe (500 pairs) | 520 KB pinned | 20 KB (stack residue) |
| 2M-iteration 3-tuple churn | 13,040 ms | 164 ms |
| `bytearray += 16 B` × 200k | 9,440 ms | 143 ms |
| dict / str-format / complex-arithmetic loops | ≈ same | ≈ same |

The last two rows are a pathology in `gc.c` that this change removes: the
allocator advances its "last free block" hint only after a *single-block*
allocation, so a loop that makes only multi-block objects (any tuple, small
list or dict — e.g. keeping the result of `mo_getpointvalues()` per iteration)
rescans the allocation table from the same point every time. Confirmed: the
same tuple loop with one boxed float added per iteration is 60× faster. The
cost grows with area size, so the current 64 MB growth areas make it worse.

## 1. Scope

In scope:
- vendor bdwgc; build it as one static library target
- `micropython/femm_gc.c` / `femm_gc.h` replacing `py/gc.c` in the link
- config, init and teardown changes in `mpconfigport.h`, `femm_port_hooks.c`, `py_state.cpp`
- ulab payload allocation → atomic, recorded in `ulab/FEMM-MODIFICATIONS.md`
- `build_test.sh`, `tests/test_py_bind.cpp`, `tests/test_py_state.cpp` kept building
- `testing/GcTest.py` regression suite + `_verify_all.sh` entry
- notices/licensing, docs

Out of scope (follow-ups, §9): atomic storage for `bytes`/`bytearray`/`array`
payloads, in-process ndarray mesh getters, the slow string-keyed dict
construction noted during measurement, and removing the `FEMM_GC` fallback
option after one release.

## 2. Vendor bdwgc

Location: `third_party/bdwgc/` (new top-level dir; `micropython/` is for the
MicroPython engine and its own patches, and bdwgc is a general library).

Source: the latest 8.2.x release tarball (8.2.8 at time of writing; verify on
GitHub `ivmai/bdwgc` releases). Keep: `include/`, `*.c` at top level,
`extra/gc.c`, `README.md`, `LICENSE`, `docs/README.macros` (the authority for
the compile-time defines below). Drop `tests/`, `tools/`, `cord/`, `doc`
except `README.macros`, `m4/`, autotools/CMake files. Record the version in
`third_party/bdwgc/FEMM-VENDORED.md` (tarball name, sha256, what was dropped,
that nothing is modified — mirror the tone of `micropython/patches/README.md`).

Build as a single translation unit. `extra/gc.c` `#include`s every core source,
which is the upstream-supported way to build the collector as one object; it
avoids adding bdwgc's CMake subproject and keeps "normal builds are pure
CMake". In top-level `CMakeLists.txt`, before `micropython_embed`:

```cmake
# --- bdwgc: the collector behind MicroPython's heap (see micropython/femm_gc.c) ---
set(BDWGC_DIR "${CMAKE_SOURCE_DIR}/third_party/bdwgc")
add_library(bdwgc STATIC "${BDWGC_DIR}/extra/gc.c")
target_include_directories(bdwgc PUBLIC "${BDWGC_DIR}/include")
target_compile_definitions(bdwgc PRIVATE
    GC_NOT_DLL              # static link (Windows); harmless elsewhere
    LARGE_CONFIG            # heap tables sized for GB-scale heaps, not MB
    ALL_INTERIOR_POINTERS   # runtime GC_set_all_interior_pointers(1) also set; keep both
    USE_MUNMAP              # return empty heap sections to the OS (mmap/VirtualFree)
    GC_DISABLE_INCREMENTAL  # no dirty-bit tracking, no SIGSEGV/vectored handler ever
    NO_EXECUTE_PERMISSION   # heap pages are data, not code
    GC_NO_THREADS_DISCOVERY
)
# No GC_THREADS: MICROPY_PY_THREAD is 0 and only the script thread touches the
# collector. Other process threads (Qt, PyBridgeHost's socket loop) never
# allocate from it and never hold the only reference to a GC object.
target_compile_options(bdwgc PRIVATE -w)   # vendored, unmodified: not our warnings
```

Check each macro name against `docs/README.macros` of the vendored version
(names have drifted between 7.x and 8.x; `GC_DISABLE_INCREMENTAL` and
`GC_NO_THREADS_DISCOVERY` in particular). On Windows/MinGW also confirm
`extra/gc.c` pulls `os_dep.c`'s Win32 root registration; it does in 8.2.

Licensing: bdwgc's `LICENSE` is a permissive X11-style notice (not GPL, not
LGPL). Add it to `packaging/license-texts/`, add an entry to
`THIRD-PARTY-NOTICES.txt` in the existing style, and check
`packaging/gen-third-party-notices.sh` does not need to know about it (it
enumerates dylibs in the macOS bundle; bdwgc is statically linked, so it needs a
hand-written entry, the same way MicroPython and ulab are handled today).

## 3. The shim: `micropython/femm_gc.c`, `femm_gc.h`

Lives next to `femm_port_hooks.c`, outside the regenerated tree, compiled into
`micropython_embed`. The companion file is the tested prototype; it needs no
functional change, only the `FEMM_GC_PROTOTYPE` escape in its config check
removed once `mpconfigport.h` is updated (§4).

What it implements, and the contract each piece keeps with `gc.c`:

- `gc_init(start, end)`: ignores its arguments. Calls, **before `GC_INIT()`**,
  `GC_set_all_interior_pointers(1)` (ulab views and `frombuffer(offset=)` hold
  interior pointers), `GC_set_finalize_on_demand(1)` (we choose when `__del__`
  runs), `GC_set_free_space_divisor(2)` (grow rather than collect). Then
  `GC_INIT()` and `GC_set_warn_proc(GC_ignore_warn_proc)`.
  **Tried and rejected:** `GC_set_no_dls(1)` to stop scanning Qt's library
  data segments. On a PIE Linux executable the main program's own
  `.data`/`.bss` are registered through the same `dl_iterate_phdr` path, so
  `mp_state_ctx` stopped being a root and the first collection freed the
  interpreter (immediate segfault). Leave dynamic-library roots on; if root
  scan time matters in the GUI (measure: §7), use `GC_exclude_static_roots`
  on specific ranges.
- `gc_alloc(n, flags)`: `GC_MALLOC` (zero-filled, which
  `MICROPY_GC_CONSERVATIVE_CLEAR` relies on). `GC_ALLOC_FLAG_HAS_FINALISER` →
  `GC_REGISTER_FINALIZER_NO_ORDER` with a callback that does what
  `gc_sweep_run_finalisers` does: `mp_load_method_maybe(__del__)` and
  `mp_call_function_1_protected`. Returns NULL when `gc_lock`ed or on OOM, so
  `m_malloc_fail` raises `MemoryError` as before. Drains the finaliser queue
  on the way out (§3.1).
- `gc_alloc_atomic(n)` (new, in `femm_gc.h`): `GC_MALLOC_ATOMIC` + `memset 0`.
  Pointer-free; never scanned. Used by ulab only (§5).
- `gc_realloc(p, n, allow_move)`: in place if `n <= GC_size(p)`; NULL if
  `!allow_move` (the vstr probe); else `GC_REALLOC`. Same observable semantics
  as `gc.c`, including that bytes between the old requested size and the size
  class are not re-cleared.
- `gc_nbytes(p)`: 0 unless `GC_base(p) == p`, else `GC_size(p)`. Only
  `persistentcode.c` and `malloc.c` (under mem-stats) call it.
- `gc_collect_start/root/end`: root scan is a no-op (bdwgc scans stack,
  registers and static data itself); `end` = `GC_gcollect()` + drain.
  `port/embed_util.c`'s `gc_collect()` still calls
  `gc_helper_collect_regs_and_stack`; harmless, leave the regenerated file alone.
- `gc_sweep_all()`: `GC_gcollect()`, then `GC_finalize_all()` (from
  `gc/javaxfc.h`), then drain. `gc.c` runs *every* finaliser at teardown,
  reachable or not, so open files flush at `close_py()`; `GC_finalize_all` is
  the same promise. Confirm the vendored build does not define
  `JAVA_FINALIZATION_NOT_NEEDED` (it doesn't by default).
- `gc_lock/unlock/is_locked`: depth counter. `runtime.c` and `profile.c`
  consult `gc_is_locked()`.
- `gc_info`: `total = GC_get_heap_size()`, `free = GC_get_free_bytes()`,
  `used = total - free`. `gc.mem_alloc()`/`mem_free()` therefore report the
  collector's heap, not "what the OS would grant". `gc.threshold()` keeps
  returning the `MP_STATE_MEM` counter but does nothing; document in §8.
- `gc_dump_alloc_table`: no-op (`micropython.mem_info(1)`).
- `femm_gc_set_max_heap(bytes)`: `GC_set_max_heap_size`. Optional cap;
  0 = unlimited. Past it `gc_alloc` returns NULL → `MemoryError`, a clean
  failure ahead of the OS OOM killer on a shared workstation. Wire to a
  setting later if wanted; not required for this handoff.

### 3.1 When finalisers run

Under `gc.c` a collection happens inside a failing `gc_alloc`, and finalisers
run inside that collection. Under bdwgc, collections happen inside
`GC_malloc` but (with `finalize_on_demand`) only *queue* finalisable objects;
nothing calls `__del__` until we drain. Three drain points, all in the shim:

1. `gc_collect_end` / `gc_sweep_all` (explicit `gc.collect()`, teardown).
2. `gc_alloc`, after a successful allocation — mirrors `gc.c`'s timing, and
   is what makes a dropped file object close within a few allocations.
3. `femm_gc_run_finalisers()` from the VM loop hook (§4), so a script that
   stops allocating still gets its finalisers.

A re-entrancy flag makes a `__del__` that allocates unable to recurse into the
drain. `GC_should_invoke_finalizers()` is a flag read, so an empty queue costs
nothing at each point.

Note: MicroPython never allocates *user* class instances with a finaliser
(`__del__` on a Python class is a documented no-op in both builds; verified).
Finalisers exist for C types only: `vfs_posix_file`, `ilistdir` iterators,
and anything femm adds with `mp_obj_malloc_with_finaliser`.

### 3.2 Rules the host keeps (unchanged from today, now stated)

- Only the thread that called `init_py()` touches MicroPython objects. bdwgc
  auto-detects the stack of the thread that ran `GC_INIT()`; that is the
  GUI/main thread in the app and the main thread in `femm_headless`. If
  `init_py()` ever moves to another thread, `GC_set_stackbottom` or thread
  registration becomes necessary — add `assert` that `init_py` runs once, on
  the thread that later runs `exec_python`.
- An `mp_obj_t` kept only inside C++-`malloc`'d memory (`std::vector`,
  `std::string`, Qt containers) is invisible to the collector. Today's code
  already respects this (`store_global`, static function objects,
  `femm_py::femm_module`); keep it that way. `grep -n "mp_obj_t" libfemm/*.h`
  for members of heap-allocated C++ classes when reviewing.
- Never call `GC_enable_incremental()`.

## 4. Configuration and lifecycle changes

### 4.1 `micropython/mpconfigport.h`

- Remove `MICROPY_GC_SPLIT_HEAP` and `MICROPY_GC_SPLIT_HEAP_AUTO` and the
  paragraph above them; remove the `#include <stdlib.h>` block whose only
  stated reason was `MP_PLAT_ALLOC_HEAP`. (Check nothing else in the header
  needed `<stdlib.h>`; the `errno.h` include below it stays.)
- Add a comment block where they were, pointing at `femm_gc.c` and this
  document, and keep `MICROPY_ENABLE_FINALISER (1)` and `MICROPY_PY_GC (1)`.
- Extend the VM loop hook so finalisers drain without allocation:

```c
extern void femm_gc_run_finalisers(void);
#define MICROPY_VM_HOOK_LOOP \
    if (--femm_vm_hook_div == 0) { \
        femm_vm_hook_div = 1024; \
        femm_gc_run_finalisers(); \
        if (femm_vm_hook_fn) femm_vm_hook_fn(); \
    }
```

- These are declaration/feature-flag edits with no qstr impact: refresh the
  stamp (`cmake -P micropython/stamp_mpconfig.cmake`), no regen. Note that
  turning `SPLIT_HEAP` off changes the layout of `mp_state_mem_t` in
  `mpstate.h`; that is compile-time only and every TU sees the same header.

### 4.2 `micropython/femm_port_hooks.c`

Delete the `gc_get_max_new_split()` block and its `#if`. Everything else
(VM hook pointer, time HAL, Windows path helpers) is unaffected.

### 4.3 `libfemm/py_state.cpp`

- Delete `static char s_heap[2 * 1024 * 1024];` and its comment.
- `init_py()`: `gc_init(nullptr, nullptr);` with a one-line comment
  ("bdwgc: no fixed heap, see micropython/femm_gc.c"). Keep
  `femm_init_stack_guard()` first: `MICROPY_STACK_CHECK` is independent of
  the collector.
- `close_py()`: delete the `#if MICROPY_GC_SPLIT_HEAP_AUTO` area-freeing
  loop. `gc_sweep_all()` then `mp_deinit()` stay.

### 4.4 `tests/test_py_bind.cpp`, `tests/test_py_state.cpp`

They call `gc_init(heap, heap + sizeof heap)` on a 128 KB static array; the
arguments are ignored now. Either leave them or replace with
`gc_init(nullptr, nullptr)` and drop the array — do the latter so nobody
reads the array as meaningful.

### 4.5 CMake wiring (top-level `CMakeLists.txt`)

Exclude `py/gc.c` from the glob and assert it was there, in the same style as
the `vfs_posix_file.c` guard (a silent no-op here would link two collectors
and fail at link with duplicate symbols — loud, but the assert gives a
readable message):

```cmake
# The block allocator/collector in py/gc.c is replaced by micropython/femm_gc.c
# on top of bdwgc (docs/bdwgc-handoff-plan.md). Exclude it, and refuse to
# configure if a version bump moves it so the exclusion silently stops matching.
set(MPY_GC_FILE "${MPY_DIR}/micropython_embed/py/gc.c")
list(FIND MPY_SOURCES "${MPY_GC_FILE}" _mpy_gc_idx)
if(_mpy_gc_idx EQUAL -1)
    message(FATAL_ERROR "[micropython] ${MPY_GC_FILE} not found; femm_gc.c would be linked alongside upstream gc.c.")
endif()
list(FILTER MPY_SOURCES EXCLUDE REGEX "py/gc\\.c$")
```

Then add `"${MPY_DIR}/femm_gc.c"` beside `femm_port_hooks.c` in
`add_library(micropython_embed ...)`, and
`target_link_libraries(micropython_embed PUBLIC bdwgc)`. `libfemm` links
`micropython_embed PUBLIC`, so `femm_gui`, `femm_headless` and the two test
executables pick bdwgc up transitively. `fkn` no longer links MicroPython
(CMakeLists comment at "fkn (magnetics solver)") so solvers are untouched.

Optional but recommended for one release: `option(FEMM_GC_BDWGC "Use bdwgc
instead of MicroPython's block allocator" ON)`. When OFF, keep `py/gc.c`,
skip `femm_gc.c`/bdwgc, and pass `-DFEMM_GC_MICROPYTHON` so `mpconfigport.h`
can re-enable `SPLIT_HEAP*` under `#ifdef`. It costs one `#if` in the config
header and one in `femm_port_hooks.c`, and it means a field problem on one
platform can be bisected to the collector in a minute. Remove after a release
without incident.

### 4.6 `micropython/build_test.sh`

Mirror the CMake change: exclude `py/gc.c` from the `find`, add `femm_gc.c`,
build `third_party/bdwgc/extra/gc.c` into `$BD/libbdwgc.a` with the same
defines, and append it to `COMMON_LINK`. The script hard-codes a MinGW path;
that is unchanged.

### 4.7 `micropython/regen.sh` and `patches/README.md`

No change to regen: the embed package will still contain `py/gc.c` after a
regen, and CMake excludes it. Add a short section to `patches/README.md`
("Not a patch: py/gc.c is not compiled at all; see femm_gc.c") so the next
person bumping MicroPython knows to re-check the `gc.h` API against the shim.
`gc.h` has been stable since the split-heap work (1.20); the likely future
delta is a new weakref or threading hook, which the shim's `#if` blocks
already stub.

## 5. ulab: atomic payloads

`micropython/ulab/code/ndarray.c`, `ndarray_new_ndarray()`, the single line
`array = m_new0(byte, len);` — every ndarray payload allocation in ulab funnels
through it (verified by grep: `ndarray_new_dense_ndarray`, `_linear_array`,
`copy_view`, `frombuffer` and all of `numpy/*.c` go through here; the other
`m_new` calls in `linalg.c`, `poly.c`, `optimize.c` are small scratch arrays
and can stay). Change to:

```c
        #include "femm_gc.h"   // at top of file, under the other includes
        ...
        array = gc_alloc_atomic(len);
        if (array == NULL) {
            m_malloc_fail(len);
        }
```

`ndarray_new_view()` and `frombuffer` need nothing: `origin` already points at
the payload base and, with interior pointers on, any `array` pointer into a
payload keeps it alive. `testing/GcTest.py` pins both (`view outlives base`,
`frombuffer(offset=...) outlives source`).

Record it in `micropython/ulab/FEMM-MODIFICATIONS.md` as a new dated section
in the style of the int32 one: what, why (scan cost + false retention with the
numbers from §0), and the re-verification rule: after any ulab update, grep
that `ndarray_new_ndarray` is still the only payload allocation site.

Do not add the `owner` field, `HostBuffer` type or a size threshold from the
earlier plan; they are superseded.

## 6. Tests

### 6.1 `testing/GcTest.py` (companion file)

Runs under `femm_headless -run` like the rest of `testing/`, uses the
`check()`/`fails` pattern of `ProxyDunderTest.py`, exits non-zero on failure.
Cases, each with a generous threshold so it passes on a loaded CI box:

- `np.zeros(9_000_000)` and `np.zeros(30_000_000)` succeed (was `MemoryError`).
- view outlives base; `frombuffer(offset=8)` result outlives its source bytes;
  after `gc.collect()` the data still reads back.
- int32 false-retention probe: 500 address pairs pin < 100 KB (was ~520 KB).
- tuples-only churn of 400k iterations under 500 ms (was 2.5 s); with an
  interleaved float allocation the ratio is < 3× either way.
- `bytearray += 16 B` × 200k under 1 s (was 9.4 s).
- `gc.collect()` with a 240 MB float64 array live under 50 ms.
- `gc.mem_alloc()` after building and dropping a 1M list returns to within
  1 MB of its starting value after two collections (heap shrink works).
- a dropped file object is closed without an explicit `gc.collect()`: open a
  temp file for write, write, drop the reference, allocate 10k small objects,
  then reopen for read and see the data (exercises drain point 2/3).
- `gc.mem_alloc()` / `mem_free()` still answer (`micropython.mem_info` is not
  in the CORE ROM level, so it is not exercised).

Add it to `testing/_verify_all.sh`.

Verified against the harness: cases 1–6 and 8 pass on the bdwgc build in
1.5 s; the same script on today's `gc.c` runs for 52 s before dying with
`MemoryError` at the 240 MB array (case 5). Case 7 needs the VFS mount and
was not run in the harness.

### 6.2 Existing suites

- Whole `testing/_verify_all.sh` on all four platforms. The suites that
  matter most: `UlabInt32Test.py` (the dtype sweep), `ProxyDunderTest.py`
  (memoryview over ndarray), `PyBridgeBenchTest.py` (its heap column will
  change — update the expectations and the table in
  `docs/pybridge-design.md` §8; the 3× transient figure should drop),
  `ConsolelessPrintTest.py` (unchanged, but it is the one that catches a
  link-order accident).
- `micropython/build_test.sh` (test_py_bind, test_py_state).
- `tests/gc_harness.c` is the standalone harness the numbers came from; keep
  it under `tests/` for the next allocator question, with a comment on how it
  was linked (embed archive minus `gc.c` and `ndarray.c`).

### 6.3 Platform checks beyond the suites

- **Windows GUI-subsystem run** (no console): bdwgc registers roots with
  `VirtualQuery` over loaded modules; confirm a long console session
  (`for i in range(10**7): t=(i,i)` then `gc.collect()`) does not crash and
  RSS returns after `del`. MinGW static link needs `GC_NOT_DLL`.
- **macOS**: hardened runtime + notarisation unaffected (no JIT, no
  `mach_vm` tricks in the non-incremental build); verify on Apple Silicon and
  x86-64 slices of the universal binary.
- **FreeBSD**: `USE_MUNMAP` path; nothing else special.
- **Linux AppImage**: nothing special; the GUI test above.
- **RSS soak**: 30-minute console session of mixed allocation on each
  platform, RSS graphed. bdwgc returns memory via `USE_MUNMAP` after a
  section has been empty for several collections (`GC_set_unmap_threshold`
  tunes it); the acceptance bar is "RSS after `del` + two collections is
  within 20% of RSS before the allocation", not "identical".

## 7. Measurements to take in the real build

Numbers in §0 came from the standalone harness. Re-measure in `femm_gui` and
`femm_headless`, because two things differ there:

1. **Root scan size.** bdwgc scans the writable data segments of every loaded
   module: Qt, libfemm, the C runtime. Measure `gc.collect()` on an idle
   interpreter in the GUI. Harness figure was 1.1 ms with MicroPython's tables
   only; a few ms in the GUI is expected and fine. If it is tens of ms,
   `GC_exclude_static_roots(begin, end)` on the Qt libraries' ranges (obtain
   via `dl_iterate_phdr` / `EnumProcessModules`) brings it back; do not use
   `GC_set_no_dls` (§3).
2. **Large-allocation latency.** `np.zeros(30_000_000)` took 595 ms in the
   harness (bdwgc collects before each heap expansion). If it matters for
   mesh-sized arrays, call `GC_expand_hp(n)` in `gc_alloc_atomic` for
   `n >= 64 MB` before `GC_MALLOC_ATOMIC`, or raise
   `GC_set_free_space_divisor`. Measure before tuning.

## 8. Documentation

- `docs/bdwgc-handoff-plan.md`: this file, updated with the real-build numbers
  from §7 and a "what was wrong" section if anything above turns out to be.
- `mpconfigport.h`, `femm_gc.c` and `py_state.cpp` comments as described.
- User-visible: a note in the scripting manual's memory section (wherever
  `gc.mem_alloc()` or "heap" is mentioned for scripts) that array sizes are
  bounded by machine RAM, that `gc.threshold()` is accepted and ignored, and
  that `gc.mem_free()` reports the collector's free space rather than
  available system memory.

## 9. Follow-ups (not in this handoff)

- **Atomic `bytes`/`bytearray`/`array` payloads.** PyBridge frames arrive as
  a `bytes` of up to 128 MiB, currently scanned. `objstr.c`/`objarray.c`
  allocate payloads with `m_new(byte, ...)`; routing those through
  `gc_alloc_atomic` is a MicroPython patch (08-...) worth doing after the
  bench shows how much it saves.
- **In-process ndarray mesh getters** (`mo_getnodes()` → `(nn,2)` f8,
  `mo_getelements()` → i4 connectivity + f8 centroid/area + i4 group,
  `mo_getb()` → c16). Copies built in C++ into atomic payloads; wire-format
  `mo_getall*` unchanged. This is the API half of the original question.
- **String-keyed dict construction.** `{str(i): ... for i in range(20000)}`
  took 3.2 s in both builds (160 µs per insert). Not allocator-related;
  suspect the interned-string probe in `mp_obj_new_str_from_vstr` or dict
  rehash. Profile separately.
- Remove `FEMM_GC_BDWGC` option after a release.

## 10. Acceptance

- All four platform builds link with `py/gc.c` absent from the object list
  (`nm` shows no `gc_alloc` from `gc.c`; `gc_dump_info` prints "GC (bdwgc)").
- `testing/_verify_all.sh` green, including `GcTest.py`.
- `PyBridgeBenchTest.py` heap column updated and documented.
- GUI soak on Windows and macOS: no crash, RSS returns.
- `THIRD-PARTY-NOTICES.txt` and `packaging/license-texts/` carry bdwgc's notice.
- `FEMM-MODIFICATIONS.md` and `patches/README.md` updated.

---

# 11. As built (2026-08-25)

Implemented as specified. This section records the real-build measurements §7
asked for, and the places where the tree turned out to differ from the plan.

## 11.1 Measurements in the real build

`femm_headless -run`, Windows/MinGW release build, same scripts as §0. The
harness figures are in brackets where they differ interestingly.

| | bdwgc, real build |
|---|---|
| idle heap after `init_py()` | 236 KB (was 2 MB static) |
| idle `gc.collect()`, headless | 0.38 ms |
| idle `gc.collect()`, **in femm_gui** | 1.5 ms mean, 1.38 ms best |
| `np.zeros(9_000_000)` | 15 ms (was `MemoryError`) |
| `np.zeros(30_000_000)` | 51 ms (was `MemoryError`) [harness: 595 ms] |
| `gc.collect()` with 240 MB float64 live | 1.8 ms |
| 2M-iteration 3-tuple churn | 137 ms (was 13,040 ms) |
| `bytearray += 16 B` × 200k | 126 ms (was 9,440 ms) |
| 1M list-of-tuples build | 937 ms |
| `gc.collect()` with that list live | 29 ms (was 119 ms) |

Two §7 questions are answered and need no action:

* **Root scan (§7.1).** The GUI costs ~1.1 ms more per collection than headless
  — that is Qt's, libfemm's and the CRT's writable data segments being scanned.
  Well inside the "a few ms is fine" bar, so no `GC_exclude_static_roots`.
* **Large-allocation latency (§7.2).** `np.zeros(30_000_000)` is 51 ms here
  against the harness's 595 ms, so the `GC_expand_hp` tuning is unnecessary.
  Not done.

**Heap shrink.** `gc.mem_alloc()` returns to ~127 KB after a 1M list *or* a
160 MB array is dropped and collected twice — below where it started. Note when
testing this by hand: a `del` at module scope after the object was referenced in
the same frame leaves conservative stack residue and the heap looks pinned.
Allocate inside a function and rebind to `None`, as `GcTest.py` does.

## 11.2 Where the tree differed from the plan

* **bdwgc 8.2.12, not 8.2.8**, and the repository is now `bdwgc/bdwgc`
  (`ivmai/bdwgc` redirects). All seven compile-time defines were checked
  against this release's `doc/README.macros` and all seven exist.
* **`doc/`, not `docs/`** in the tarball, and **there is no `LICENSE` file**:
  8.2.12 keeps the notice in `README.md` under "Copyright & Warranty".
  `third_party/bdwgc/LICENSE` is a new file holding that section, so the
  installers can ship it under the same `LICENSE-*.txt` convention as the
  others. Nothing vendored was modified.
* **Header layout.** 8.2.x installs headers flat (`include/gc.h`), not under
  `include/gc/` as the prototype's `#include "gc/gc.h"` assumed. Changed to
  `<gc.h>` / `<javaxfc.h>`; angle brackets so they can never be confused with
  MicroPython's own `py/gc.h`, which the same file includes.
* **`femm_port_hooks.c`: `gc_get_max_new_split()` was kept**, not deleted. Its
  existing `#if MICROPY_GC_SPLIT_HEAP_AUTO` already makes it vanish under
  bdwgc, and keeping it is what lets `-DFEMM_GC_BDWGC=OFF` work with no further
  edits. The file gained a fallback `gc_alloc_atomic()` for the same reason, so
  ulab has one code path rather than an `#ifdef`.
* **The `FEMM_GC_BDWGC` option was taken** (§4.5 called it optional). Its
  `FEMM_GC_MICROPYTHON` define is deliberately **PUBLIC** on
  `micropython_embed`: it switches `MICROPY_GC_SPLIT_HEAP`, which changes the
  layout of `mp_state_mem_t`, so libfemm and the test executables must see the
  same value. PRIVATE would give a silent ABI split.
* **`tests/test_stubs.cpp` gained `get_message_box_callback()`** rather than
  the harness linking `script_callbacks.cpp` — that file pulls in the document
  model and tangle, which is exactly what the stub file exists to avoid.

## 11.3 What the plan got wrong: when finalisers actually run

§3.1 lists three drain points and says the one in `gc_alloc` "is what makes a
dropped file object close within a few allocations". **It does not, and cannot.**

bdwgc only *queues* a finalisable object when a **collection** finds it
unreachable. With `GC_set_free_space_divisor(2)` — grow rather than collect —
20,000 small allocations trigger no collection at all, so the queue stays
empty, `GC_should_invoke_finalizers()` is false, and every drain point is a
no-op. Measured: a file dropped without `close()` is still open after 20k
allocations, and is closed by the first `gc.collect()`.

The drain points are not broken; there is simply nothing to drain until a
collection happens. Explicit `gc.collect()` (drain point 1) and `gc_sweep_all()`
at teardown both work, so nothing leaks across a session.

**The behaviour change to be aware of:** under `py/gc.c` a 2 MB fixed heap
filled often, so collections — and finalisers — were frequent. Under bdwgc they
are rare while memory is plentiful. A script that opens many files without
closing them can therefore hold far more descriptors open than it used to.
`with open(...)` and explicit `close()` were always the right answer and now
matter more. This is noted in the scripting manual.

**It also invalidated the test that was supposed to prove it.** The original
`GcTest.py` case 7 checked that the *content* was readable — but
`vfs_posix_file` writes straight to the fd, so the bytes reach disk whether or
not anything was finalised; the case would have passed with finalisers
completely broken. It now probes with `os.remove()`, which Windows refuses
while a handle is open, and asserts what actually holds: `gc.collect()` runs
the finaliser.

If eager finalisation is ever wanted, the lever is `GC_set_free_space_divisor`
(collect more often, at a throughput cost) or an explicit `GC_gcollect()` when
`GC_should_invoke_finalizers()` has been false for a long time — not more drain
points.

## 11.4 build_test.sh was already broken, and is now fixed

`micropython/build_test.sh` had not run in months, for reasons predating this
work. Mirroring the collector change meant repairing it:

* every path was `../../X`, one level too far (the script `cd`s to
  `micropython/`);
* it compiled `crypto-algorithms/sha256.c` and `lib/re1.5/*.c`, which their
  modules `#include` as `.c` — CMake excludes both, this did not;
* it never linked ulab, `femm_port_hooks.c`, `py_os.cpp`, `py_pybridge.cpp`,
  `py_clinalg.cpp`, `pybridge_host.cpp`, `common/utf8_io.cpp` or
  `common/fullmatrix.cpp`, all added since it was last touched;
* its ulab loop named objects by basename, so `numpy/linalg/linalg.c` and
  `scipy/linalg/linalg.c` wrote the same file;
* it never applied the `mp_sys_std*_obj` rename that CMake does on
  `vfs_posix_file.c`, so `print()` went to fd 1 and the harness's own
  output-sink test failed.

It now builds and runs clean: **test_py_bind 60 checks, 0 failures;
test_py_state 80 checks, 0 failures.**

Getting the last four to pass meant correcting the test, not the harness. They
asserted that `mi_makeABC` … `hi_makeABC` are defined, and that is false in the
**production build too**: a physics command is registered when a document of
that type is created, so `mi_makeABC` raises `NameError` until `newdocument(0)`
has run. Verified against the real binary rather than inferred — the same trap
`docs/pyfemm-inprocess-compat.md` records an audit falling into twice.

They were removed rather than made to work. Making them work needs a document,
which needs the document model: 44 undefined symbols spanning the four
`*Document` classes, the four results docs, the registry, per-physics
registration and tangle — effectively all 31 `libfemm` sources. That would turn
a harness whose own header calls it a "pre-CMake build harness to validate the
MicroPython binding layer" into a second copy of the CMake build. The commands
are covered where they can actually run: `_verify_all.sh` drives the real
binaries against real documents.

`mi_drawline`, checked on the next line, keeps passing and is the useful
control: it comes from the frozen init script, so it exists as soon as the
interpreter does.

## 11.4a The Windows GUI/RSS soak (§6.3), and what it corrected

Run on Windows against `femm_gui` — the GUI-subsystem case, no console, which
is the one with novel risk: bdwgc discovers static roots there by walking
loaded modules with `VirtualQuery`. `testing/gc_soak.py` is the script; memory is
sampled from outside the process at 250 ms, since nothing inside can see its
own RSS.

**No crash**, including the plan's specific case (10^7 tuples then
`gc.collect()` in a GUI-subsystem process). Steady-state cycles return exactly:

| cycle | before | peak | after |
|---|---|---|---|
| array240_1 | 321.4 MB | 550.9 | **321.9** (×1.00) |
| array80_1 | 321.9 | 398.2 | **321.9** (×1.00) |
| list500k_1 | 321.9 | 379.4 | **321.9** (×1.00) |
| array240_2 | 321.9 | 550.8 | **321.9** (×1.00) |

Two corrections came out of it.

**§6.3's bar of "two collections" is wrong; it takes four.** With two, a 240 MB
block comes back one cycle late and a run looks like it is holding ~340 MB
(measured: 628 MB peak, cycles reading ×1.38). With four, every steady-state
cycle is ×1.00 and the peak itself drops to 551 MB. Confirmed independently in
headless with the allocation confined to a returned frame: baseline 12.2 MB
working set, peak 241 MB, settled 12.5 MB — **×1.02 across five 240 MB
cycles**, no growth. So memory is genuinely returned; two collections just
aren't enough to see it.

**`GC_set_force_unmap_on_gcollect(1)` was added to `femm_gc.c`.** `USE_MUNMAP`
returns a section to the OS only after it has been empty for `MUNMAP_THRESHOLD`
collections, which is **6** in this build (`allchblk.c`) — so no realistic
number of `gc.collect()` calls would have returned memory promptly. The flag is
read only on bdwgc's `GC_gcollect()` path, which the shim reaches only from
`gc_collect_end()` and `gc_sweep_all()`, i.e. when a script or teardown asked
for it. Collections bdwgc triggers inside an allocation keep the lazy default,
so hot loops do not pay for unmap/remap. Effect at soak level: final working set
404 → 322 MB.

**Read the first cycle differently from the rest.** It is always ×3.5: the
process is reaching its working set from a cold start and cannot return below a
baseline it had not yet touched. Judge cycles 2 onward.

**And a trap for anyone writing one of these:** binding the allocation in the
caller and setting it to `None` is not enough. The dead value stays in that
frame's VM stack slots and registers, a conservative collector must treat them
as live, and the block is pinned for another cycle — which reads as retention.
Allocate inside a function that has returned. An earlier version of this soak
measured a 343 MB "floor" that was entirely this artifact.

## 11.4b Linux (Ubuntu), 2026-08-26

Collector confirmed active (`testing/gc_which.py`): idle heap 256 KB,
`np.zeros(30_000_000)` in 142 ms, and `gc.collect()` with it live in **0 ms**
against Windows' 1.8. The full suite is green.

RSS soak, headless, analysed with `testing/gc_soak_report.py`. **PASS — every
steady-state cycle x1.00**, and leaner than Windows throughout:

| | Linux | Windows |
|---|---|---|
| first sample | 5.9 MB | 28.7 MB |
| steady plateau | 237.5 MB | 321.9 MB |
| peak | 466.4 MB | 550.9 MB |

One observation that is NOT platform-specific and is worth understanding
before reading either trace: both runs end with the collector still reporting
**exactly one 240 MB array allocated** — `END heap=240132096` on Linux,
`240128000` on Windows. That is one `np.zeros(30000000)` from the last cycle
still reachable by conservative scanning, so most of the "steady plateau" is a
dead-but-pinned array rather than collector overhead. It does not accumulate:
every cycle returns to the same plateau exactly, which is the property the bar
is testing. §11.4a's headless floor test is the complement — with the residue
avoided, RSS returns to x1.02 of a 12 MB baseline across five 240 MB cycles.

Still to run on Linux: the same soak against `femm_gui`, which is where Qt's
data segments become GC roots (and, per §6.3, doubles as the AppImage case).
The 5.9 MB first sample above identifies this run as headless.

## 11.4c The GUI soak on Linux, and what the 20% bar cannot express

`femm_gui -run testing/gc_soak.py` on Ubuntu. The mechanical verdict is one
FAIL, and it is worth reading carefully because the bar is the thing at fault.

    array240_0   114.3 -> 343.7 peak -> 344.0    x3.01   (cold start, excluded)
    array80_0    344.0 -> 420.4      -> 344.1    x1.00
    list500k_0   344.1 -> 403.1      -> 345.6    x1.00
    array240_1   345.6 -> 575.0      -> 575.0    x1.66   <-- the one FAIL
    array80_1    575.0 -> 651.2      -> 575.0    x1.00
    list500k_1   575.0 -> 632.5      -> 575.0    x1.00
    array240_2   575.0 -> 804.4      -> 575.5    x1.00
    ... six more cycles, all x1.00 at 575.5

Not drift: a single step of ~230 MB — one `np.zeros(30000000)` — after which
the plateau is flat for six consecutive cycles. Confirmed by the collector's own
figure at teardown:

| run | `END heap` | arrays pinned |
|---|---|---|
| Linux, headless | 240,132,096 | 1 |
| Linux, **femm_gui** | **480,133,120** | **2** |
| Windows, femm_gui | 240,128,000 | 1 |

This is conservative false retention, and it is inherent to the collector
choice rather than a defect in the shim: a stale word in a register, a stack
slot or a scanned data segment that happens to hold what looks like a heap
address keeps that block alive. `femm_gui` has the larger root set — Qt's
writable data segments, a deeper stack, an event loop — so it catches more of
them. The count is not deterministic across platforms, as the table shows.

**The acceptance criterion should change.** "RSS after del + two collections is
within 20% of RSS before" cannot be met in general by a conservative collector:
whenever a cycle happens to pin a new block, that cycle steps and reads over
budget, however healthy the collector is. What actually distinguishes healthy
from broken is:

  * the plateau is **bounded** — it steps at most a few times and then stops;
  * it is **non-cumulative** — repeated identical cycles return to the same
    plateau exactly (x1.00), rather than each adding to it;
  * the collector's own `gc.mem_alloc()` agrees with the process figure, so a
    step is explainable as N pinned blocks rather than unaccounted growth.

All three hold here, in every run on both platforms. `gc_soak_report.py` still
prints the x1.20 verdict because a mechanical bar is useful, but a single FAIL
on a stepping cycle is not by itself a reason to act.

**What was deliberately not done.** `GC_exclude_static_roots` on Qt's ranges
would trim this, and §7 suggests it for scan *time*. It is the wrong tool here:
`py_state.cpp` keeps `mp_obj_t` in statics on purpose, those live in exactly the
kind of data segment such a range would cover, and getting the bounds wrong
frees a live object. The failure would be a rare silent crash, traded for
bounded, stable retention. Not worth it.

**For users:** a GUI session that allocates several hundred MB may keep one or
two such blocks resident after dropping them. The existing advice to reuse
arrays rather than reallocating (scripting manual, "Keep bulk numbers in arrays,
not lists") happens to avoid it.

## 11.4d macOS (M1), 2026-08-26 — and it returns everything

Builds clean, collector confirmed (`gc_which.py`: idle heap 256 KB,
`np.zeros(30_000_000)` in **27 ms**, the fastest of the three; collect with it
live 0 ms). Suite green apart from `FQReference`, which fails on macOS by
design and by prior decision — see §11.6 and `FQReferenceCompare.py`'s header.

The headless soak is the best result on any platform:

| | Windows | Linux | **macOS** |
|---|---|---|---|
| steady plateau | 244 MB | 237 MB | **5.1 MB** |
| peak | 473 MB | 466 MB | 234 MB |
| arrays pinned at teardown | 1 | 1 | **0** |

Windows and Linux each end holding one conservatively-pinned 240 MB array
(§11.4b). macOS pins nothing: RSS returns to 5.1 MB after every cycle, and the
peak is correspondingly one array lower. Whatever the mechanism — mach VM
returning pages more eagerly, fewer stale words surviving in registers — the
platform this change was most likely to disappoint is the one that behaves best.

**It also exposed a defect in `gc_soak_report.py`.** The report flagged
`list500k_0`, 3.6 → 5.1 MB, as x1.42 OVER. That is 1.5 MB of settling after half
a million small objects — noise. A ratio with no absolute floor cries wolf
precisely where the collector is doing best: the same 1.5 MB against Windows'
240 MB plateau reads as x1.01. The reporter now requires BOTH the ratio and
absolute growth over `FLOOR_MB = 16` before flagging, which is comfortably below
the 80-240 MB blocks the soak allocates and comfortably above settling noise.
Re-checked against the Windows trace (unchanged, PASS) and against a synthetic
240 MB retention (still caught, x2.01 OVER).

## 11.4e The GUI soak everywhere, and the criterion that finally works

macOS GUI, against its own headless run in §11.4d:

    array240_0   113.4 ->  374.1   +260.7   (cold start, excluded)
    array80_0    374.1 ->  450.5    +76.4   STEP
    list500k_0   450.4 ->  452.0     +1.6
    array240_1   452.0 ->  681.3   +229.3   STEP
    array80_1    681.3 ->  757.8    +76.5   STEP
    list500k_1 .. bytes3M_1         +0.0    (six cycles, flat at 758.3)

Bounded: three steps, then nothing for six consecutive cycles. But the size of
the effect is worth stating plainly, because it is the one number from this
whole exercise that might deserve action:

| plateau after the run | headless | GUI |
|---|---|---|
| Windows | 244 MB | 322 MB |
| Linux | 237 MB | 575 MB |
| macOS | **5 MB** | **758 MB** |

macOS headless pins nothing at all; macOS with Qt pins about 625 MB across four
blocks. The soak is adversarial (repeated 80-240 MB allocations are not what
real scripts do), and it is bounded, but a GUI session that touches large arrays
can hold several hundred MB that `gc.mem_alloc()` agrees is dead.

**An earlier draft of this section attributed the difference to the GUI's larger
root set. FreeBSD disproved that** — see §11.4f. There the GUI pins *nothing*
(74.7 MB plateau) while headless on the same machine pins one array (241 MB),
the opposite way round. Retention count is not a monotonic function of root-set
size; it is what happens to be lying in registers and stack slots when a
collection runs.

**DECIDED 2026-08-26: left as documented behaviour.** It is bounded,
`gc.mem_alloc()` accounts for every byte of it, and the soak that produces it is
adversarial in a way real scripts are not. The alternatives were considered and
not taken: a second `GC_gcollect()` from the post-script hook (which cleared the
analogous Windows case, but spends a collection on every script to fix a
worst-case), and `GC_exclude_static_roots` over Qt's ranges (rejected outright —
`py_state.cpp` keeps `mp_obj_t` in statics, those live in exactly such a
segment, and wrong bounds free a live object). Revisit only if a user reports it.

**The ratio test was wrong at BOTH ends, and both were measured.** It is now
absolute growth over `FLOOR_MB = 16`, with the ratio printed only as context:

  * **too strict where the collector does best.** macOS headless returns
    everything, so its plateau is ~5 MB; 1.5 MB of settling after half a
    million small objects reads x1.42 there and x1.01 against Windows' 240 MB
    plateau. Same noise, opposite verdicts.
  * **too lax once a plateau has grown.** `array80_1` above is a genuine 80 MB
    pin and reads x1.11 — under a 20% gate — while the identical pin earlier in
    the same run (`array80_0`, 374 -> 450) reads x1.20. The better the run had
    been going, the more likely the test was to miss a real pin.

And a step is no longer reported as a failure, because it is not one: a
conservative collector pins a block whenever a stale word points at it. The
verdict now encodes §11.4c directly — it looks at the LAST repetition of each
cycle shape, and fails only if retention is still growing there. All four runs
(Windows GUI, Linux GUI, macOS headless, macOS GUI) are PASS by that rule, and
a synthetic still-growing trace is still caught.

## 11.4f FreeBSD (GhostBSD), 2026-08-27

Builds and the suite is green. `GcTest`, `UlabInt32` and `ProxyDunders` all
pass, and `FQReference` reads 9.11e-10 against a 5e-6 budget — the same libm-tail
story as Linux's 2.41e-11, since the committed reference was generated on
Windows.

Headless soak **PASS**, and it lands exactly where Windows and Linux did: first
sample 7.1 MB, cold start to 238.9, steady plateau **241.0 MB**, peak 470.4, and
every steady-state cycle at ×1.00 with +0.0 MB growth. One 240 MB array pinned,
the same as Windows and Linux; macOS headless remains the only one that pins
none.

The corrected reporter also behaved as intended on real data for the first time
on a fresh platform: it excluded the ×25.95 cold start, and `list500k_0`'s
+1.5 MB at ×1.01 passed under the 16 MB absolute floor rather than being flagged
by a ratio.

GUI soak **PASS**, and it is the most interesting single result of the whole
pass: first sample 22.9 MB, steady plateau **74.7 MB**, peak 304.1 — and the
cold-start cycle itself only ×1.01, so *nothing was pinned at all*, including at
startup.

**This is the run that disproves the tidy explanation.** Every plateau, all
eight runs:

| | headless | GUI |
|---|---|---|
| Windows | 244 MB (1 array) | 322 MB (1) |
| Linux | 237 MB (1) | 575 MB (2) |
| macOS | 5 MB (**0**) | 758 MB (4) |
| FreeBSD | 241 MB (1) | **74.7 MB (0)** |

Windows, Linux and macOS all pinned more under the GUI than headless, which
made "the GUI has a larger root set, so it catches more stale pointers" look
like the mechanism. FreeBSD is the other way round: its GUI pins nothing while
its headless pins one array. macOS is the mirror image, 0 headless and 4 GUI.

So the count is **not** a function of root-set size. It is whatever happens to
be lying in registers and stack slots at the moment a collection runs — which
is exactly what conservative collection means, and is why the acceptance
criterion has to be bounded/non-cumulative/explainable (§11.4c) rather than any
threshold on the amount retained.

**Both failures it reported were defects in the tests, not the product**, and
both were the same species: an assertion that measured its environment rather
than the code.

`Fft` failed in the suite and passed standalone — the signature of a timing
assertion. Its fast-path check used `time.time()`, which is coarse enough on
FreeBSD that 200 power-of-two transforms measured as **zero**, so the
`if t_pow2 > 0` guard silently skipped the check; and it compared *means*, so
under the suite's load one scheduling hiccup pushed the ratio under threshold
and failed it. Now `ticks_us`, a batch grown until it clears 20 ms of clock,
best-of-5 rather than a mean, and a loud failure instead of a silent skip if the
transform still measures zero (38c9b95).

`ConsolelessPrint` reported the GUI dying with **SIGABRT**, about a second in —
but only when the test spawned it. `femm_gui -run /dev/null` from a shell was
fine. The test's POSIX spawn closed fds 0/1/2 outright, which frees those
descriptors for the first files the child opens; on FreeBSD the X11 socket
plausibly lands on one and a later write to "stderr" corrupts it. Linux and
macOS survived that by luck. It now uses `/dev/null`, which is what a desktop
launcher actually gives, and loses nothing: the teeth of that test are the
`stdout is not a file` probe, which pins which definition linked and does not
care what fd 1 is. Windows still gets NULL std handles, which is where the EBADF
failure it was built for lives (272ebff).

Worth generalising: **the collector produced no product failures on any
platform. Every failure in four platform passes was a test measuring its
environment** — a ratio against a plateau, a mean under load, an emulation
harsher than reality.

## 11.5 Acceptance status

Done: `py/gc.c` absent from the archive (`ar t` shows only `femm_gc.c.obj`, and
`nm` puts `gc_alloc` there); `_verify_all.sh` green on Windows including the new
`GcTest` row, with `FQReference` worst movement still 0; `THIRD-PARTY-NOTICES`
equivalent — the four attribution places (`third_party/bdwgc/LICENSE`,
`femm-qt.iss`, `macos_build.sh`, `packaging/license.txt`) all carry bdwgc;
`FEMM-VENDORED.md`, `ulab/FEMM-MODIFICATIONS.md` and `patches/README.md`
written; the `-DFEMM_GC_BDWGC=OFF` fallback verified to build and to reproduce
the old ceiling (`np.zeros(30_000_000)` → `MemoryError`).

**Linux: done** (2026-08-26) — build, full suite, headless and GUI soaks. See
§11.4b and §11.4c.

**macOS: done** (2026-08-26, M1) — builds clean, collector confirmed and the
fastest of the three (`np.zeros(30_000_000)` in 27 ms), suite green apart from
the by-design `FQReference` FMA failure, headless and GUI soaks both bounded.
See §11.4d and §11.4e.

**FreeBSD: done** (2026-08-27, GhostBSD) — build, suite, and both soaks. See
§11.4f.

**All four platforms are complete.** Remaining:  the `PyBridgeBenchTest.py` heap-column update in
`docs/pybridge-design.md` §8 — the bench passes and its transfer times improved
(Windows 65/52 → 53/39 ms, Linux 40/27 ms), but the documented heap figures
should be re-taken deliberately rather than inferred from one run.

## 11.6 Bringing up a new platform: what to run, and what "good" looks like

Everything below is in the source tree; nothing external is needed.

**1. Is bdwgc even in this binary?** Run this FIRST — it is the one failure that
is otherwise invisible, because every ordinary script, including a heavy one
like `examples/mySPMLossScriptAGB.py`, runs identically on either collector.

    build/bin/femm_headless -run testing/gc_which.py

Expect `VERDICT: bdwgc`, an idle heap near 250 KB, and `np.zeros(30_000_000)`
succeeding. If it reports the block allocator, CMake configured
`-DFEMM_GC_BDWGC=OFF` or the `py/gc.c` exclusion stopped matching after a
MicroPython bump — nothing else is worth running until that is fixed.

**2. The suite.** `bash testing/_verify_all.sh`. `GcTest` and `UlabInt32` are
the rows that bear most directly on this change.

`FQReference` needs its expectations set per platform, or it will look like this
change broke something when it did not:

  * **Linux/FreeBSD:** a tiny non-zero movement (Linux measured 2.41e-11 against
    a 5e-6 budget) because the committed reference was generated on Windows.
    That is libm's last bits, not the collector.
  * **macOS: this test is EXPECTED TO FAIL, and has been since long before
    bdwgc.** clang contracts on arm64 by default, FMA changes the MESH rather
    than just the last digits (16277 vs 16357 nodes on one model, ~64% of
    vertices existing in only one of the two meshes), and every budget in the
    comparator assumes the same mesh. `FQReferenceCompare.py`'s own header
    states this from line 60 and records the 2026-08-19 decision that FMA stays
    on. Do not attribute an `FQReference` failure on macOS to the collector, and
    do not turn FMA off to make it pass.

What would genuinely implicate the collector is `GcTest`, `UlabInt32` or
`ProxyDunders` failing, or an OF-comparison suite moving — those compare against
Original FEMM at a tolerance far looser than FMA noise.

**3. The RSS soak, headless and then GUI.** `testing/gc_soak.py`, sampled from
outside the process; its header has the PowerShell and POSIX samplers. Read it
with `python3 testing/gc_soak_report.py rss.csv gc_soak_phases.txt`.

Judge it by §11.4c's criterion, not by the raw verdict line: bounded,
non-cumulative, explainable. A single cycle stepping over 20% is expected
whenever that cycle happens to pin a block; what matters is that repeated
identical cycles then return to the new plateau exactly, and that `END heap` in
the phase log accounts for the step as a whole number of pinned arrays.
Observed so far: 1 array pinned headless on Linux and in the Windows GUI, 2 in
the Linux GUI. A plateau that keeps climbing, or an `END heap` that does not
correspond to whole arrays, would be the real thing.

**macOS specifics** (§6.3): hardened runtime and notarisation are unaffected —
no JIT, and `GC_DISABLE_INCREMENTAL` means no SIGSEGV or mach exception handler
is ever installed, which is the part that would have been a problem. Check both
slices of the universal binary. `USE_MUNMAP` is the platform code most likely
to differ, as it did on Windows (§11.4a).

**FreeBSD specifics:** `USE_MUNMAP` again; nothing else known.
