// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_port_hooks.c - port-level symbols referenced from inside the
// micropython_embed core (vm.c, gc.c). This file lives OUTSIDE the generated
// micropython_embed/ tree (regen-micropython must not wipe it) and is
// compiled into the micropython_embed library so every embedder — libfemm,
// fkn, the test harnesses — links it without extra wiring.
#include <stddef.h>

#include "py/mpconfig.h"

// VM-loop callback (see MICROPY_VM_HOOK_LOOP in mpconfigport.h). The GUI
// console sets this to a rate-limited Qt event pump (py_state.cpp
// set_py_vm_hook); NULL when headless, so the hook is just a null check.
void (*femm_vm_hook_fn)(void) = NULL;

#if MICROPY_GC_SPLIT_HEAP_AUTO
// Cap on each additional heap segment gc_try_add_heap() may malloc. Growth
// doubles the total heap per split, so this bounds the step size, not the
// total: the total is bounded only by what the system malloc will grant,
// matching desktop interpreter semantics (CPython behaves the same way).
size_t gc_get_max_new_split(void) {
    return 64u * 1024 * 1024;
}
#endif
