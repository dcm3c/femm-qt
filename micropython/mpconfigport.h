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
#define MICROPY_HELPER_REPL                 (0)   // femm provides its own console
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

// ---- Modules ----
#define MICROPY_PY_SYS                      (1)
#define MICROPY_PY_SYS_EXIT                 (0)   // quit() is FEMM's, not sys.exit()
#define MICROPY_PY_SYS_STDFILES             (0)   // stdout routes via mp_hal, not sys.stdout

// collections: namedtuple comes with CORE; OrderedDict + deque are otherwise
// EXTRA-gated, but they are pure object features with no port dependency, so
// cherry-pick them onto CORE for a more complete general-purpose collections.
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_COLLECTIONS_DEQUE        (1)

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
