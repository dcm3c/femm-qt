// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_stack_guard.h - one-call C-stack limit setup for MICROPY_STACK_CHECK.
//
// With MICROPY_STACK_CHECK enabled, mp_cstack_check() raises RecursionError
// when mp_cstack_usage() (= stack_top - current_sp) reaches stack_limit. The
// limit is only correct if stack_top is at (or above) the SHALLOWEST stack
// pointer ever seen during script execution. Capturing it in a helper frame
// that later unwinds (e.g. inside init_py) is WRONG: a subsequent exec at a
// shallower depth makes the unsigned subtraction underflow to a huge value and
// every check trips — which manifests as a spurious RecursionError on the very
// first print(), then a hang in nlr_jump_fail.
//
// We sidestep frame fragility entirely by anchoring stack_top at the OS stack
// BASE (highest address of the thread's stack) and setting the budget from the
// real thread stack size minus a safety reserve. This is valid from any entry
// point (GUI console, headless -run, TCP exec, fkn solver) and any call depth.
//
// Single-threaded use only (MICROPY_PY_THREAD == 0): the interpreter always
// runs on the thread that called femm_init_stack_guard().
#ifndef FEMM_STACK_GUARD_H
#define FEMM_STACK_GUARD_H

#include <cstddef>

extern "C" {
#include "py/cstack.h"
}

#if defined(_WIN32)
// Keep windows.h from polluting TUs that include this (min/max/ERROR macros
// clash with std:: and MicroPython); we only need the thread-stack query.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
// BSDs declare the thread-stack query (pthread_attr_get_np) here, not pthread.h.
#include <pthread_np.h>
#endif
#endif

// Reserve below the OS stack limit: must exceed the deepest C-stack growth
// between two mp_cstack_check() calls (one interpreter recursion plus the
// deepest bound C function, e.g. a solver callback or printf). 256 KB is
// generous on every desktop target.
static inline void femm_init_stack_guard() {
    const size_t reserve = 256 * 1024;

#if defined(_WIN32)
    // Win8+: exact bounds of the current thread's stack. high == base.
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    size_t usable = (size_t)(high - low);
    size_t budget = usable > reserve ? usable - reserve : usable / 2;
    mp_cstack_init_with_top((void *)high, budget);

#elif defined(__APPLE__)
    // macOS: addr is the stack BASE (highest address); grows down from there.
    void *base = pthread_get_stackaddr_np(pthread_self());
    size_t size = pthread_get_stacksize_np(pthread_self());
    size_t budget = size > reserve ? size - reserve : size / 2;
    mp_cstack_init_with_top(base, budget);

#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || \
    defined(__DragonFly__)
    // BSD: like Linux, attr gives the LOW address + size (base = low + size),
    // but the query is spelled pthread_attr_get_np and -- unlike glibc's
    // pthread_getattr_np -- requires the attr to be initialized beforehand. We
    // own the init here, so we must destroy it on both paths.
    pthread_attr_t attr;
    void *low = nullptr;
    size_t size = 0;
    pthread_attr_init(&attr);
    if (pthread_attr_get_np(pthread_self(), &attr) == 0 &&
        pthread_attr_getstack(&attr, &low, &size) == 0) {
        pthread_attr_destroy(&attr);
        size_t budget = size > reserve ? size - reserve : size / 2;
        mp_cstack_init_with_top((char *)low + size, budget);
    } else {
        pthread_attr_destroy(&attr);
        // Fall back to capturing here with a budget that fits an 8 MB stack.
        mp_cstack_init_with_sp_here(8u * 1024 * 1024 - reserve);
    }

#else
    // Linux/glibc: attr gives the LOW address + size; base = low + size.
    pthread_attr_t attr;
    void *low = nullptr;
    size_t size = 0;
    if (pthread_getattr_np(pthread_self(), &attr) == 0 &&
        pthread_attr_getstack(&attr, &low, &size) == 0) {
        pthread_attr_destroy(&attr);
        size_t budget = size > reserve ? size - reserve : size / 2;
        mp_cstack_init_with_top((char *)low + size, budget);
    } else {
        // Fall back to capturing here with a budget that fits an 8 MB stack.
        mp_cstack_init_with_sp_here(8u * 1024 * 1024 - reserve);
    }
#endif
}

#endif  // FEMM_STACK_GUARD_H
