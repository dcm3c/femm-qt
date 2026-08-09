// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_bind_type.c - the const type table for the femm callable object.
//
// Compiled as C (NOT C++) on purpose: MP_DEFINE_CONST_OBJ_TYPE expands to
// designated initializers, which are standard in C99 but only conditionally
// supported under C++17. Keeping just this one definition in a C TU lets the
// rest of the binding layer be ordinary C++ that includes <functional>.
//
// The call entry point femm_fn_call is implemented in py_bind.cpp with C
// linkage. femm_fn_type is referenced from py_bind.h.
#include "py/obj.h"
#include "py/runtime.h"

mp_obj_t femm_fn_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args);

// GC finaliser: deletes the C++ std::function the object owns (see femm_fn_del
// in py_bind.cpp). MicroPython has no "del" type slot -- gc_sweep finalises a
// block by looking up __del__ on the object (py/gc.c, mp_load_method_maybe with
// MP_QSTR___del__), so it has to be published through locals_dict. Objects are
// allocated with m_malloc_with_finaliser so they carry the finaliser flag.
mp_obj_t femm_fn_del(mp_obj_t self_in);
static MP_DEFINE_CONST_FUN_OBJ_1(femm_fn_del_obj, femm_fn_del);

static const mp_rom_map_elem_t femm_fn_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&femm_fn_del_obj) },
};
static MP_DEFINE_CONST_DICT(femm_fn_locals_dict, femm_fn_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    femm_fn_type,
    MP_QSTR_function,
    MP_TYPE_FLAG_NONE,
    call, femm_fn_call,
    locals_dict, &femm_fn_locals_dict
    );
