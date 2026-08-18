// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_pybridge.cpp - see py_pybridge.h.
//
// Failures surface as OSError, matching the rest of the scripting layer (a
// dead worker and a missing file are both "the outside world let us down", and
// `except OSError` should catch either). pybridge.py wraps them in BridgeError
// for callers who want to be specific.
#include "py_pybridge.h"

#include "py_bind.h"
#include "pybridge_host.h"

extern "C" {
#include "py/objmodule.h"
#include "py/runtime.h"
}

#include <string>
#include <vector>

namespace {

// Every entry point funnels through this: pybridge_error carries a message
// meant for the user, and os_error is what py_bind turns into OSError.
template <typename F>
auto guarded(F &&f) -> decltype(f()) {
    try {
        return f();
    } catch (const pybridge_error &e) {
        throw femm_py::os_error(e.what());
    }
}

}  // namespace

void register_pybridge_py() {
    using femm_py::Module;

    mp_obj_t mod = mp_obj_new_module(qstr_from_str("_pybridge"));
    Module m;
    m.globals = mp_obj_module_get_globals(mod);

    m.def("ensure_started", []() {
        guarded([] { femm_pybridge_ensure_started(); return 0; });
    });

    // The one hot path. Raw because the typed def() layer speaks str, and a
    // frame's binary region is arbitrary bytes -- routing it through str would
    // corrupt anything that is not valid UTF-8, which is every array.
    m.def_raw("rpc", 1, 1, [](size_t n, const mp_obj_t *args) -> mp_obj_t {
        mp_buffer_info_t bi;
        if (!mp_get_buffer(args[0], &bi, MP_BUFFER_READ))
            mp_raise_TypeError(MP_ERROR_TEXT("_pybridge.rpc needs bytes"));
        std::string frame((const char *)bi.buf, bi.len);
        std::string reply = guarded([&] { return femm_pybridge_rpc(frame); });
        return mp_obj_new_bytes((const byte *)reply.data(), reply.size());
    });

    m.def("session",    []() { return (long)femm_pybridge_session(); });
    m.def("running",    []() { return femm_pybridge_running(); });
    m.def("info",       []() { return femm_pybridge_info(); });
    m.def("shutdown",   []() { femm_pybridge_shutdown(); });
    m.def("restart",    []() { femm_pybridge_restart(); });

    m.def("set_interpreter", [](const std::string &p) {
        femm_pybridge_set_interpreter(p);
    });
    m.def("interpreter", []() { return femm_pybridge_interpreter(); });

    m.def("set_backend", [](const std::string &b) {
        femm_pybridge_set_backend(b);
    });
    m.def("backend", []() { return femm_pybridge_backend(); });

    // What was tried and why each candidate was rejected. This is the support
    // surface when someone reports "plotting does not work".
    m.def("attempts", []() { return femm_pybridge_attempts(); });
}
