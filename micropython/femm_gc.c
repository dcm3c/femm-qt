// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_gc.c - MicroPython's py/gc.h implemented on the Boehm-Demers-Weiser
// collector (third_party/bdwgc). Replaces py/gc.c in the link; see
// docs/bdwgc-handoff-plan.md for why and for the measurements.
//
// What changes for the rest of femm-qt: nothing allocates from a fixed heap
// any more. gc_init() ignores its arguments, there are no heap areas and no
// gc_get_max_new_split(); an allocation fails only when the process is out
// of memory or the optional cap (femm_gc_set_max_heap) is hit, and in both
// cases MicroPython raises MemoryError exactly as before.
//
// Rules the host must keep (identical to the rules under py/gc.c):
//   - Only the thread that called init_py() may touch MicroPython objects.
//   - An mp_obj_t stored in C++-malloc'd memory (std::vector etc.) is
//     invisible to the collector. Keep such references in static storage or
//     in the Python globals dict (store_global), as py_state.cpp already does.
//   - Never call GC_enable_incremental(): it installs a SIGSEGV/vectored
//     handler that Qt must not share the process with.
#include <string.h>
// bdwgc 8.2.x installs its headers at include/*.h; the gc/ prefix used by the
// development branch does not exist here. Angle brackets, so these resolve
// against third_party/bdwgc/include and can never be confused with py/gc.h --
// MicroPython's own collector header, which this file also includes and whose
// interface it implements.
#include <gc.h>
#include <javaxfc.h>             // GC_finalize_all
#include "py/gc.h"
#include "py/runtime.h"
#include "py/mpstate.h"
#include "femm_gc.h"

#if !MICROPY_ENABLE_GC || MICROPY_PY_THREAD || MICROPY_GC_SPLIT_HEAP
#error "femm_gc.c: expects MICROPY_ENABLE_GC=1, MICROPY_GC_SPLIT_HEAP=0, MICROPY_PY_THREAD=0"
#endif

static size_t s_lock_depth = 0;
static size_t s_collections = 0;
static bool s_in_finalisers = false;

// ---------------------------------------------------------------------------
// Finalisers. MicroPython runs __del__ during sweep, protected (the exception
// is printed and dropped) and with the scheduler locked. Boehm queues
// finalisable objects instead (finalize_on_demand), and we drain the queue at
// three points: after an explicit collection, from the VM loop hook, and
// before returning from an allocation that found the queue non-empty. The
// last one mirrors py/gc.c, where finalisers run inside the collection that
// an allocation triggered.
// ---------------------------------------------------------------------------
static void finaliser_cb(void *obj, void *cd) {
    (void)cd;
    mp_obj_base_t *base = (mp_obj_base_t *)obj;
    if (base->type == NULL) return;           // freed by gc_free(), or never initialised
    mp_obj_t dest[2];
    mp_load_method_maybe(MP_OBJ_FROM_PTR(obj), MP_QSTR___del__, dest);
    if (dest[0] != MP_OBJ_NULL) {
        #if MICROPY_ENABLE_SCHEDULER
        mp_sched_lock();
        #endif
        mp_call_function_1_protected(dest[0], dest[1]);
        #if MICROPY_ENABLE_SCHEDULER
        mp_sched_unlock();
        #endif
    }
}

void femm_gc_run_finalisers(void) {
    if (s_in_finalisers || !GC_should_invoke_finalizers()) return;
    s_in_finalisers = true;
    GC_invoke_finalizers();
    s_in_finalisers = false;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void gc_init(void *start, void *end) {
    (void)start; (void)end;
    // All of these must precede GC_INIT().
    GC_set_all_interior_pointers(1);   // ulab views and frombuffer(offset=) hold interior pointers
    GC_set_finalize_on_demand(1);      // we drain the queue; Boehm never calls __del__ on its own
    GC_set_free_space_divisor(2);      // grow rather than collect: desktop, not MCU
    // Do NOT call GC_set_no_dls(1) to skip Qt's library data segments: on a PIE
    // executable Linux registers the main program's own .data/.bss through the
    // same dl_iterate_phdr path, so mp_state_ctx stops being a root and the
    // first collection frees the interpreter (verified: instant segfault). If
    // root-scan time ever matters, use GC_exclude_static_roots on measured
    // library ranges instead.
    GC_INIT();
    GC_set_warn_proc(GC_ignore_warn_proc);
    // An explicit gc.collect() means "give the memory back now", so make it
    // unmap as much as it can. AFTER GC_INIT(), which reads the
    // GC_FORCE_UNMAP_ON_GCOLLECT environment variable and would override it.
    //
    // Without this, USE_MUNMAP returns a heap section to the OS only once it
    // has been empty for MUNMAP_THRESHOLD (6) collections, so RSS does not come
    // back within the two collections the acceptance test allows -- measured:
    // a soak allocating and dropping 240 MB three times sat at 632 MB RSS while
    // gc.mem_alloc() correctly reported 143 KB. The memory was cached, not
    // leaked (the later cycles reused it and RSS stopped growing), but a
    // workstation holding 632 MB of nothing is not a good look.
    //
    // The flag is read only on the GC_gcollect() path (bdwgc alloc.c), which is
    // reached only from gc_collect_end() and gc_sweep_all() -- i.e. when a
    // script or teardown asked. Collections that bdwgc triggers inside an
    // allocation keep the lazy default, so hot allocation loops do not pay for
    // unmapping and remapping.
    GC_set_force_unmap_on_gcollect(1);
    MP_STATE_MEM(gc_auto_collect_enabled) = 1;
    MP_STATE_MEM(gc_alloc_threshold) = (size_t)-1;
    s_lock_depth = 0;
    s_collections = 0;
}

void femm_gc_set_max_heap(size_t bytes) {
    GC_set_max_heap_size(bytes);       // 0 = unlimited; exceeding it makes gc_alloc return NULL
}

void gc_lock(void) { s_lock_depth++; }
void gc_unlock(void) { s_lock_depth--; }
bool gc_is_locked(void) { return s_lock_depth > 0; }

void gc_collect_start(void) {}
void gc_collect_root(void **ptrs, size_t len) { (void)ptrs; (void)len; }   // Boehm scans stack + registers itself
void gc_collect_end(void) {
    GC_gcollect();
    s_collections++;
    femm_gc_run_finalisers();
}

// At interpreter teardown py/gc.c runs EVERY finaliser, reachable or not, so
// open files are flushed. GC_finalize_all is the same promise.
void gc_sweep_all(void) {
    GC_gcollect();
    GC_finalize_all();
    femm_gc_run_finalisers();
}

// ---------------------------------------------------------------------------
// Allocation
// ---------------------------------------------------------------------------
void *gc_alloc(size_t n_bytes, unsigned int alloc_flags) {
    if (n_bytes == 0 || gc_is_locked()) return NULL;
    void *p = GC_MALLOC(n_bytes);            // zero-filled, as MICROPY_GC_CONSERVATIVE_CLEAR assumes
    if (p == NULL) return NULL;
    if (alloc_flags & GC_ALLOC_FLAG_HAS_FINALISER) {
        GC_REGISTER_FINALIZER_NO_ORDER(p, finaliser_cb, NULL, NULL, NULL);
    }
    femm_gc_run_finalisers();
    return p;
}

// Pointer-free storage for bulk numeric data (ulab payloads): never scanned,
// so a 240 MB float64 array costs the collector nothing and an int32 pair that
// happens to spell a heap address cannot pin garbage.
void *gc_alloc_atomic(size_t n_bytes) {
    if (n_bytes == 0 || gc_is_locked()) return NULL;
    void *p = GC_MALLOC_ATOMIC(n_bytes);
    if (p) memset(p, 0, n_bytes);            // atomic memory is not cleared by Boehm
    return p;
}

void gc_free(void *ptr) {
    if (ptr) GC_FREE(ptr);                   // does not run the finaliser, per gc.h
}

size_t gc_nbytes(const void *ptr) {
    if (ptr == NULL || GC_base((void *)ptr) != ptr) return 0;   // 0 for "not a heap object", as gc.c
    return GC_size((void *)ptr);
}

void *gc_realloc(void *ptr, size_t n_bytes, bool allow_move) {
    if (ptr == NULL) return gc_alloc(n_bytes, 0);
    if (n_bytes == 0) { GC_FREE(ptr); return NULL; }
    if (gc_is_locked()) return NULL;
    if (n_bytes <= GC_size(ptr)) return ptr;  // fits the size class: in place, like gc.c's same-block case
    if (!allow_move) return NULL;             // vstr's "extend only if free" probe
    return GC_REALLOC(ptr, n_bytes);          // copies, tail is zero-filled for non-atomic objects
}

// ---------------------------------------------------------------------------
// Introspection (gc.mem_alloc/mem_free, micropython.mem_info)
// ---------------------------------------------------------------------------
void gc_info(gc_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->total = GC_get_heap_size();
    info->free = GC_get_free_bytes();
    info->used = info->total > info->free ? info->total - info->free : 0;
    info->max_free = info->free;
}

void gc_dump_info(const mp_print_t *print) {
    gc_info_t info;
    gc_info(&info);
    mp_printf(print, "GC (bdwgc): heap %u, used %u, free %u, unmapped %u, collections %u\n",
        (unsigned)info.total, (unsigned)info.used, (unsigned)info.free,
        (unsigned)GC_get_unmapped_bytes(), (unsigned)s_collections);
}
void gc_dump_alloc_table(const mp_print_t *print) { (void)print; }   // no allocation table to dump

#if MICROPY_PY_WEAKREF
void gc_weakref_mark(void *ptr) { (void)ptr; }
void gc_weakref_about_to_be_freed(void *ptr) { (void)ptr; }
void gc_weakref_sweep(void) {}
#endif
