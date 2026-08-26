// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_gc.h - host-facing extras of the bdwgc-backed collector (femm_gc.c).
#ifndef FEMM_GC_H
#define FEMM_GC_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Pointer-free allocation for bulk numeric payloads. Zero-filled. NULL on failure.
void *gc_alloc_atomic(size_t n_bytes);
// Drain queued __del__ calls. Cheap when the queue is empty; called from the
// VM loop hook (mpconfigport.h) and after collections.
void femm_gc_run_finalisers(void);
// Cap the collector's heap (0 = unlimited). Past the cap, allocations fail
// and MicroPython raises MemoryError.
void femm_gc_set_max_heap(size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
