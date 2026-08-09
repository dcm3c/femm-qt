// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_bind.cpp - the C++ half of the MicroPython callable wrapper.
//   femm_fn_call : the type's call slot. Does arity checking, pads omitted
//                  trailing optional args with None, then runs the std::function.
//   femm_module  : the single global module-wrapper instance.
// The type table itself (femm_fn_type) lives in py_bind_type.c.
#include "py_bind.h"

#include <cstdio>
#include <exception>

namespace femm_py {
Module femm_module;
}  // namespace femm_py

// GC finaliser. FemmFn's impl/raw_impl are plain `new`-ed std::functions on the
// C++ heap; the collector reclaims the FemmFn itself (and its m_malloc'd name)
// but knows nothing about those, so without this they leak. Re-registering a
// binding -- which every open()/newdocument()/loadsolution() does, ~89 names per
// editor and ~50 per postprocessor -- supersedes the old object and would
// otherwise strand one allocation per name.
//
// Nulled after deletion so a double finalise is harmless.
extern "C" mp_obj_t femm_fn_del(mp_obj_t self_in) {
    femm_py::FemmFn *self = (femm_py::FemmFn *)MP_OBJ_TO_PTR(self_in);
    delete self->impl;
    self->impl = nullptr;
    delete self->raw_impl;
    self->raw_impl = nullptr;
    return mp_const_none;
}

extern "C" mp_obj_t femm_fn_call(mp_obj_t self_in, size_t n_args, size_t n_kw,
                                 const mp_obj_t *args) {
    femm_py::FemmFn *self = (femm_py::FemmFn *)MP_OBJ_TO_PTR(self_in);

    if (n_kw != 0) {
        mp_raise_TypeError(MP_ERROR_TEXT("femm functions take no keyword arguments"));
    }

    if (n_args < self->min_n || n_args > self->max_n) {
        if (self->min_n == self->max_n) {
            mp_raise_msg_varg(
                &mp_type_TypeError,
                MP_ERROR_TEXT("%s() takes %d positional arguments but %d were given"),
                self->name, (int)self->max_n, (int)n_args);
        } else {
            mp_raise_msg_varg(
                &mp_type_TypeError,
                MP_ERROR_TEXT("%s() takes from %d to %d positional arguments but %d were given"),
                self->name, (int)self->min_n, (int)self->max_n, (int)n_args);
        }
    }

    // Bridge C++ exceptions to Python exceptions. The bound lambdas (and the
    // document model they call) throw std::runtime_error etc.; MicroPython
    // unwinds via nlr/longjmp and does NOT understand C++ exceptions, so a
    // throw must never propagate into MicroPython's C frames. Snapshot the
    // message and let the catch block END before raising: mp_raise_* longjmps,
    // and jumping out of a live catch would skip destroying the in-flight
    // exception object (and is UB on some unwinders).
    char what[512];
    try {
        // Varargs functions take (n_args, args) directly, no padding.
        if (self->raw_impl)
            return (*self->raw_impl)(n_args, args);

        // The std::function always reads max_n entries; pad omitted trailing
        // optionals with None (caster<std::optional<T>>::load maps None -> nullopt).
        const mp_obj_t *call_args = args;
        mp_obj_t buf[FEMM_MAX_ARGS];
        if (n_args != self->max_n) {
            for (size_t i = 0; i < self->max_n; i++)
                buf[i] = (i < n_args) ? args[i] : mp_const_none;
            call_args = buf;
        }
        return (*self->impl)(call_args);
    } catch (const std::exception &e) {
        std::snprintf(what, sizeof what, "%s", e.what());
    } catch (...) {
        std::snprintf(what, sizeof what, "unknown C++ exception in femm function");
    }
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), what);
}
