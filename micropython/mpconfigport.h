// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// mpconfigport.h - MicroPython embed configuration for femm-qt.
// Pinned to MicroPython v1.28.0 (see MICROPYTHON_VERSION).
//
// Rationale for the key knobs (see port/PLAN.md Appendix B):
//   - COMPLEX is the whole point of the swap (native `j` literal so `1+2*I`
//     parses, and `re+I*im` can be produced on the wire).
//   - DOUBLE float + MPZ longint: FEMM needs full IEEE-754 doubles and
//     arbitrary-precision integers for user scripts.
//   - DETAILED errors so the console shows useful tracebacks.

// Common embed definitions (alloca, mp_off_t, MICROPY_MPHALPORT_H).
#include <port/mpconfigport_common.h>

// MICROPY_GC_SPLIT_HEAP_AUTO uses MP_PLAT_ALLOC_HEAP/FREE_HEAP (malloc/free) in
// gc.c, which doesn't include <stdlib.h> itself. This header is included by
// every embed TU, so pull it in here to declare malloc/free (otherwise GCC/
// Clang warn -Wimplicit-function-declaration / -Wbuiltin-declaration-mismatch).
#include <stdlib.h>

// CORE_FEATURES is the baseline for a general-purpose desktop scripting
// engine (upstream's own default). Over MINIMUM it turns on CPYTHON_COMPAT
// (type().__name__, __class__, function attrs — which user scripts and
// `except` handlers expect), collections (namedtuple/OrderedDict/deque),
// attrtuple, the array module, and more str methods. It does NOT pull in any
// port-dependent module (time/os/select/socket are gated higher), so it needs
// no extra HAL hooks. Specific features below are still set explicitly.
#define MICROPY_CONFIG_ROM_LEVEL            (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// ---- Core interpreter ----
#define MICROPY_ENABLE_COMPILER             (1)
#define MICROPY_ENABLE_GC                   (1)
// Runaway script recursion raises RecursionError instead of overflowing the
// C stack and killing the process. NB: with this on, an embedder MUST call
// mp_stack_set_limit() after mp_stack_ctrl_init() — the limit lives in
// zero-initialised state, and a zero limit fails the very first check.
#define MICROPY_STACK_CHECK                 (1)
// Auto-growing GC heap: gc_init() gets a small initial segment and
// gc_try_add_heap() mallocs further segments on demand (capped per split by
// gc_get_max_new_split() in femm_port_hooks.c). Segments that empty out are
// returned to the system during sweep. Big postprocessing scripts get memory
// instead of MemoryError; idle sessions stay small.
#define MICROPY_GC_SPLIT_HEAP               (1)
#define MICROPY_GC_SPLIT_HEAP_AUTO          (1)
#define MICROPY_PY_GC                       (1)
#define MICROPY_ENABLE_FINALISER            (1)
#define MICROPY_HELPER_REPL                 (1)   // GUI console uses mp_repl_continue_with_input()
#define MICROPY_KBD_EXCEPTION               (1)
// KBD_EXCEPTION pulls in shared/runtime/interrupt_char.c, whose
// mp_hal_set_interrupt_char() is called by modmicropython.c (micropython.kbd_intr).
// This embed port's mphalport.h doesn't declare it and py/mphal.h leaves it to
// the port, so declare it here -- a persistent, always-included header (survives
// regen) -- to avoid an -Wimplicit-function-declaration warning. Bare prototype:
// the C++ build reaches this header inside runtime.h's extern "C", so it gets C
// linkage matching the C definition.
#if MICROPY_KBD_EXCEPTION
void mp_hal_set_interrupt_char(int c);
#endif
#define MICROPY_ENABLE_SOURCE_LINE          (1)
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_WARNINGS                    (1)

// ---- VM speed (no qstr impact, safe without regen) ----
// Computed-goto dispatch: ~10-15% faster bytecode loop (GCC/Clang only,
// which covers every femm-qt toolchain; MSVC is not used).
#define MICROPY_OPT_COMPUTED_GOTO           (1)
// Per-map lookup cache: speeds up the repeated global-name lookups that
// dominate FEMM scripts (mi_addnode(...) etc. in hot loops).
#define MICROPY_OPT_MAP_LOOKUP_CACHE        (1)

// ---- VM loop hook: GUI event pumping + script interruption ----
// Every 1024 VM branches, call femm_vm_hook_fn if set (femm_port_hooks.c,
// default NULL). The GUI console points it at a rate-limited Qt event pump so
// the UI stays live during scripts and a Stop control can interrupt via
// mp_sched_keyboard_interrupt(). Headless embedders leave it NULL (the cost
// is one decrement + branch per VM jump).
#ifdef __cplusplus
extern "C" {
#endif
extern void (*femm_vm_hook_fn)(void);
#ifdef __cplusplus
}
#endif
#define MICROPY_VM_HOOK_INIT static unsigned int femm_vm_hook_div = 1024;
#define MICROPY_VM_HOOK_LOOP \
    if (--femm_vm_hook_div == 0) { \
        femm_vm_hook_div = 1024; \
        if (femm_vm_hook_fn) femm_vm_hook_fn(); \
    }

// ---- Numerics ----
#define MICROPY_FLOAT_IMPL                  (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL                (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_PY_BUILTINS_FLOAT           (1)
#define MICROPY_PY_BUILTINS_COMPLEX         (1)
#define MICROPY_PY_MATH                     (1)
#define MICROPY_PY_CMATH                    (1)

// ---- Builtins relied on by user scripts / response formatting ----
#define MICROPY_PY_BUILTINS_STR_OP_MODULO   (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE     (1)
#define MICROPY_PY_BUILTINS_SET             (1)
#define MICROPY_PY_BUILTINS_FROZENSET       (1)
#define MICROPY_PY_BUILTINS_SLICE           (1)
#define MICROPY_PY_BUILTINS_ENUMERATE       (1)
#define MICROPY_PY_BUILTINS_REVERSED        (1)
#define MICROPY_PY_BUILTINS_RANGE_ATTRS     (1)
#define MICROPY_PY_BUILTINS_ROUND_INT       (1)

// f-strings. EXTRA-gated upstream for code size, which is not a concern here,
// and their absence is the kind of gap that reads as a broken product: every
// modern Python example uses them, and a pasted snippet fails with a bare
// "invalid syntax" that names no line and suggests no cause. The lexer
// implements them by injecting `"...".format(...)` into the token stream, so
// this needs no new source and no new qstrs -- the embed package is unchanged,
// which is why the stamp was refreshed rather than the package regenerated.
// Format specs therefore go through str.format, and complex values come out
// right via the same patch that fixes "%g" (01-objstr-complex-format.patch):
// f"{3+4j:g}" prints 3+4j.
#define MICROPY_PY_FSTRINGS                 (1)

// Full special-method dispatch for user-defined classes. MicroPython's default
// set is small -- __add__, __eq__, __lt__, __getitem__, __len__, __call__ and a
// few more dispatch, while __mul__, __truediv__, __pow__, __neg__, __abs__ and
// every reverse operation (__radd__, __rmul__, ...) silently do not, and
// __setattr__/__delattr__ are never called at all, so an assignment to an
// attribute quietly creates a local one instead.
//
// A class that models an object living somewhere else -- the PyBridge proxy
// (docs: pybridge-implementation-plan.md), but equally any wrapper a user
// writes -- needs all of it, and the failure mode without it is the bad kind:
// `2 * wrapped` raises where `wrapped * 2` works, and `obj.attr = v` succeeds
// while doing nothing. testing/ProxyDunderTest.py pins the whole matrix so a
// later ROM-level change cannot quietly take it away again.
//
// memoryview joins them because it is what makes bulk data cheap: ulab
// implements the buffer protocol, so memoryview(ndarray) is a view onto the
// array's own memory rather than a copy of it.
#define MICROPY_PY_ALL_SPECIAL_METHODS      (1)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS  (1)
#define MICROPY_PY_DELATTR_SETATTR          (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW      (1)

// ---- Modules ----
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_EXIT                 (0)   // quit() is FEMM's, not sys.exit()
// sys.stdout / sys.stderr / sys.stdin. They were off on the reasoning that
// output already routes through mp_hal -- true, but it left the NAMES absent,
// and `print(msg, file=sys.stderr)` or `sys.stdout.write(...)` is in a great
// deal of ordinary Python. It also blocked the `logging` module outright.
// shared/runtime/sys_stdio_mphal.c (copied by regen.sh) defines the three
// objects on top of mp_hal_stdout_tx_strn, so they write exactly where print()
// already writes -- the console in the GUI, the terminal headless. This also
// switches on print()'s own `file=` keyword, which modbuiltins.c gates on the
// same pair of flags.
//
// Turning this on also MOVES print(): modbuiltins.c:387 makes mp_sys_stdout_obj
// its default `file`, and mpprint.h makes it MP_PYTHON_PRINTER as well, so the
// flag does not merely add names -- it redirects everything that was already
// working. extmod/vfs_posix_file.c defines the same three symbols on raw fds
// and only one set can link, so the build has to say which; see the
// set_source_files_properties() rename in the top-level CMakeLists.txt. Get
// that wrong and script output goes to fd 1, which a GUI-subsystem process does
// not have.
#define MICROPY_PY_SYS_STDFILES             (1)

// collections: namedtuple comes with CORE; OrderedDict + deque are otherwise
// EXTRA-gated, but they are pure object features with no port dependency, so
// cherry-pick them onto CORE for a more complete general-purpose collections.
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_COLLECTIONS_DEQUE        (1)

// Standard-library modules a scripting user reaches for out of CPython habit.
// All are EXTRA_FEATURES upstream, cherry-picked onto CORE rather than raising
// the whole ROM level (which would also drag in things this host has no use
// for). Each needs its extmod/mod*.c copied into the package by regen.sh --
// the embed packager ships only py/, so the flag alone is not enough.
#define MICROPY_PY_JSON                     (1)
// re, with the pieces a CPython user assumes are simply part of it: sub() is
// EXTRA-gated upstream and the match accessors are gated higher still, so
// without these `re` would offer match/search/split and then fail on
// m.groups() or re.sub -- worse than not having it.
#define MICROPY_PY_RE                       (1)
#define MICROPY_PY_RE_SUB                   (1)
#define MICROPY_PY_RE_MATCH_GROUPS          (1)
#define MICROPY_PY_RE_MATCH_SPAN_START_END  (1)
// binascii's hexlify/unhexlify are bytes.hex()/fromhex() under another name, so
// they appear only with BYTES_HEX -- which is also what makes b"..".hex() work.
#define MICROPY_PY_BINASCII                 (1)
#define MICROPY_PY_BUILTINS_BYTES_HEX       (1)
#define MICROPY_PY_HASHLIB                  (1)   // sha256
#define MICROPY_PY_ERRNO                    (1)   // py/moderrno.c: already packaged

// random seeds itself on import, so random.random() differs run to run as it
// does in CPython. Seeding on import needs MODULE_BUILTIN_INIT, which is
// EXTRA-gated for code size -- irrelevant on a desktop.
#define MICROPY_PY_RANDOM                   (1)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS       (1)   // random, uniform, randrange, choice
#define MICROPY_MODULE_BUILTIN_INIT         (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC    (femm_random_seed())
unsigned long femm_random_seed(void);

// time: ticks and sleep come from the port HAL (femm_port_hooks.c),
// localtime/mktime from shared/timeutils, and time()/localtime() from
// femm_modtime.c -- which modtime.c pulls in through the INCLUDEFILE hook, the
// same way every upstream port supplies them. sleep() pumps the GUI event loop
// rather than blocking, so a settling delay in a script does not look like a
// hung window. Epoch 1970 and a float time(), i.e. desktop Python's, not a
// microcontroller's 2000 epoch and integer seconds.
#define MICROPY_PY_TIME                     (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS        (1)
#define MICROPY_PY_TIME_INCLUDEFILE         "femm_modtime.c"
#define MICROPY_EPOCH_IS_1970               (1)
// This host HAS a timezone, which upstream's time module assumes away: it
// aliases gmtime to localtime and converts a passed-in timestamp with pure UTC
// arithmetic. The two then return each other's answer -- localtime(t) gives
// UTC, gmtime() gives local -- and everything downstream inherits it, most
// visibly datetime.now(), which was off by the whole UTC offset. Switching this
// on asks modtime.c for a separate gmtime and routes localtime()/mktime()
// through the CRT's timezone-aware conversions in femm_modtime.c.
// See patches/06-modtime-host-timezone.patch.
#define MICROPY_PY_TIME_HOST_TIMEZONE       (1)
// Dates outside 1970..2099. Upstream auto-enables these only when MP_SSIZE_MAX
// is 64-bit, which this build is not, so timeutils was using an UNSIGNED
// timestamp: time.gmtime(-1) came back as year 53052 (a huge day count with
// the year truncated into a uint16) rather than 1969-12-31. Silent nonsense
// either side of the range, and datetime.fromtimestamp(0) tripped over it
// because it probes one day earlier to detect DST.
#define MICROPY_TIME_SUPPORT_Y1969_AND_BEFORE (1)
#define MICROPY_TIME_SUPPORT_Y2100_AND_BEYOND (1)

// Platform string for sys.platform (required by modsys.c).
// MICROPY_PLATFORM_COMPILER is provided by extmod/modplatform.h.
#ifdef _WIN32
#define MICROPY_PY_SYS_PLATFORM             "win32"
#elif defined(__APPLE__)
#define MICROPY_PY_SYS_PLATFORM             "darwin"
#else
#define MICROPY_PY_SYS_PLATFORM             "linux"
#endif

// ---- File I/O via host filesystem ----
// Enables Python open()/read()/write() backed by the POSIX VFS, which we mount
// at "/" in init_py(). Needed for user scripts that write analysis results.
#define MICROPY_PY_IO                       (1)
#define MICROPY_VFS                         (1)
#define MICROPY_VFS_POSIX                   (1)
#define MICROPY_READER_VFS                  (1)

// extmod/vfs_posix.c is written for the unix port and expects this EINTR-retry
// macro from the unix mphal. The embed port doesn't provide it, so define a
// thread-free version here (MICROPY_PY_THREAD is 0). Expanded in vfs_posix.c
// where errno and mp_handle_pending are in scope.
#include <errno.h>
#ifndef MP_HAL_RETRY_SYSCALL
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise_block)        \
    {                                                          \
        for (;;) {                                             \
            ret = syscall;                                     \
            if (ret == -1) {                                   \
                int err = errno;                               \
                if (err == EINTR) {                            \
                    mp_handle_pending(true);                   \
                    continue;                                  \
                }                                              \
                raise_block;                                   \
            }                                                  \
            break;                                             \
        }                                                      \
    }
#endif

// ---- Deliberately disabled ----
#define MICROPY_PY_THREAD                   (0)
