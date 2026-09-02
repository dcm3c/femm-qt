/*
 * This file is part of the micropython-ulab project,
 *
 * https://github.com/v923z/micropython-ulab
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019-2021 Zoltán Vörös
 *
*/

#ifndef _TRANSFORM_
#define _TRANSFORM_

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "py/obj.h"
#include "py/runtime.h"
#include "py/misc.h"

#include "../ulab.h"
#include "../ulab_tools.h"

MP_DECLARE_CONST_FUN_OBJ_KW(transform_compress_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(transform_delete_obj);
// femm-qt: the FUNCTION, not just the callable object. ndarray_binary_op()
// calls it directly to implement the @ operator, and without a prototype C
// assumes it returns int -- which truncates the returned mp_obj_t pointer and
// segfaults, after compiling with nothing worse than a warning.
mp_obj_t transform_dot(mp_obj_t , mp_obj_t );
MP_DECLARE_CONST_FUN_OBJ_2(transform_dot_obj);
MP_DECLARE_CONST_FUN_OBJ_KW(transform_size_obj);

#endif
