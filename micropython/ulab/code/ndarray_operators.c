/*
 * This file is part of the micropython-ulab project,
 *
 * https://github.com/v923z/micropython-ulab
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Zoltán Vörös
*/


#include <math.h>

#include "py/runtime.h"
#include "py/objtuple.h"
#include "ndarray.h"
#include "ndarray_operators.h"
#include "ulab.h"
#include "ulab_tools.h"
#include "numpy/carray/carray.h"

/*
    This file contains the actual implementations of the various
    ndarray operators.

    These are the upcasting rules of the binary operators

    - if complex is supported, and if one of the operarands is a complex, the result is always complex
    - if both operarands are real one of them is a float, then the result is also a float
    - operation on identical types preserves type

    uint8 + int8 => int16
    uint8 + int16 => int16
    uint8 + uint16 => uint16
    int8 + int16 => int16
    int8 + uint16 => uint16
    uint16 + int16 => float

    femm-qt: int32 does NOT appear in the chains below. Any binary operation in
    which an int32 meets another integer type is handled by
    ndarray_binary_int32() at the head of this file, before those chains are
    reached; int32 mixed with float or complex is converted to float first, in
    ndarray_binary_op(). See the NDARRAY_INT32 note in ndarray.h for why it is
    done that way rather than by adding a row and a column to each chain.
*/

// ---------------------------------------------------------------------------
// femm-qt: int32 binary operations
// ---------------------------------------------------------------------------
// One kernel for every binary operation where an int32 meets another integer
// type. The alternative -- adding an int32 row and column to each of the ten
// macro-expanded dtype chains below -- is about a hundred leaves that would
// have to be re-applied on every upstream update, and getting one of them
// wrong is silent: a missed combination falls into a chain's trailing "assume
// float" branch and reinterprets the bytes.
//
// Values are read through a per-dtype accessor and computed in int32, which is
// what NumPy's promotion gives once int32 is the widest integer available.
// Semantics follow NumPy deliberately:
//   * +, -, * wrap on overflow (NumPy wraps too; the intermediate is int64 so
//     the wrap is a defined conversion rather than signed overflow)
//   * // and % floor toward negative infinity, Python-style, not C-style
//   * division or modulo by zero yields 0, as NumPy does
//   * a negative exponent raises, as NumPy does
//   * comparisons yield a boolean array
//
// The loop mirrors BINARY_LOOP's walk. ULAB_MAX_DIMS is 2 here; the #error
// below fires rather than silently mis-walking if that is ever raised.
#if ULAB_MAX_DIMS > 2
#error "femm-qt: extend ndarray_binary_int32() before raising ULAB_MAX_DIMS"
#endif

static int32_t ndarray_int32_apply(mp_binary_op_t op, int32_t a, int32_t b, bool *ok) {
    int64_t r;
    switch(op) {
        case MP_BINARY_OP_ADD:      r = (int64_t)a + (int64_t)b; break;
        case MP_BINARY_OP_SUBTRACT: r = (int64_t)a - (int64_t)b; break;
        case MP_BINARY_OP_MULTIPLY: r = (int64_t)a * (int64_t)b; break;
        case MP_BINARY_OP_FLOOR_DIVIDE:
        case MP_BINARY_OP_MODULO: {
            if(b == 0) return 0;                    // NumPy yields 0 here
            int32_t q = a / b, m = a % b;           // C truncates toward zero
            if((m != 0) && ((m < 0) != (b < 0))) {  // ...Python floors
                q -= 1;
                m += b;
            }
            return (op == MP_BINARY_OP_MODULO) ? m : q;
        }
        case MP_BINARY_OP_POWER: {
            if(b < 0) { *ok = false; return 0; }    // NumPy raises for this
            r = 1;
            int64_t base = a;
            for(int32_t e = b; e > 0; e >>= 1) {
                if(e & 1) r *= base;
                base *= base;
                r = (int32_t)r;                     // keep the wrap at 32 bits
                base = (int32_t)base;
            }
            break;
        }
        case MP_BINARY_OP_AND: return a & b;
        case MP_BINARY_OP_OR:  return a | b;
        case MP_BINARY_OP_XOR: return a ^ b;
        // Shifts are reachable only through numpy.left_shift/right_shift --
        // ulab does not wire << and >> to the ndarray operators.
        case MP_BINARY_OP_LSHIFT:
            if((b < 0) || (b > 31)) return 0;
            return (int32_t)((uint32_t)a << b);
        case MP_BINARY_OP_RSHIFT:
            if(b < 0) return 0;
            return (b > 31) ? ((a < 0) ? -1 : 0) : (a >> b);
        default: *ok = false; return 0;
    }
    return (int32_t)r;
}

static bool ndarray_int32_is_comparison(mp_binary_op_t op) {
    return (op == MP_BINARY_OP_EQUAL) || (op == MP_BINARY_OP_NOT_EQUAL) ||
           (op == MP_BINARY_OP_LESS) || (op == MP_BINARY_OP_LESS_EQUAL) ||
           (op == MP_BINARY_OP_MORE) || (op == MP_BINARY_OP_MORE_EQUAL);
}

static uint8_t ndarray_int32_compare(mp_binary_op_t op, int32_t a, int32_t b) {
    switch(op) {
        case MP_BINARY_OP_EQUAL:      return a == b;
        case MP_BINARY_OP_NOT_EQUAL:  return a != b;
        case MP_BINARY_OP_LESS:       return a <  b;
        case MP_BINARY_OP_LESS_EQUAL: return a <= b;
        case MP_BINARY_OP_MORE:       return a >  b;
        default:                      return a >= b;   // MORE_EQUAL
    }
}

mp_obj_t ndarray_binary_int32(mp_binary_op_t op, ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                              uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    int32_t (*get_lhs)(void *) = (int32_t (*)(void *))ndarray_get_int32_function(lhs->dtype);
    int32_t (*get_rhs)(void *) = (int32_t (*)(void *))ndarray_get_int32_function(rhs->dtype);
    if((get_lhs == NULL) || (get_rhs == NULL)) {
        mp_raise_TypeError(MP_ERROR_TEXT("operation not supported for the input types"));
    }

    bool compare = ndarray_int32_is_comparison(op);
    // An operator ulab does not implement for ANY dtype (<<, >> are the current
    // examples) must fail the way it does for every other dtype -- MP_OBJ_NULL,
    // which lets MicroPython raise its own "unsupported types" message. Checked
    // up front so int32 does not get a different error from int16.
    if(!compare) {
        switch(op) {
            case MP_BINARY_OP_ADD:
            case MP_BINARY_OP_SUBTRACT:
            case MP_BINARY_OP_MULTIPLY:
            case MP_BINARY_OP_FLOOR_DIVIDE:
            case MP_BINARY_OP_MODULO:
            case MP_BINARY_OP_POWER:
            case MP_BINARY_OP_AND:
            case MP_BINARY_OP_OR:
            case MP_BINARY_OP_XOR:
            case MP_BINARY_OP_LSHIFT:
            case MP_BINARY_OP_RSHIFT:
                break;
            default:
                return MP_OBJ_NULL;
        }
    }
    ndarray_obj_t *results = ndarray_new_dense_ndarray(ndim, shape,
                                 compare ? NDARRAY_UINT8 : NDARRAY_INT32);
    if(compare) {
        results->boolean = 1;
    }
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;
    uint8_t *out = (uint8_t *)results->array;
    bool ok = true;

    // Loop bounds come from results->shape, NOT from the `shape` argument --
    // exactly as every BINARY_LOOP macro does, and for a reason that is not
    // obvious: ndarray_can_broadcast() fills only the last `ndim` entries of
    // `shape`, and numpy/bitwise.c allocates it with m_new rather than m_new0,
    // so for a 1-D operand the leading entry is uninitialised garbage. Looping
    // on it writes far past the end of the result and corrupts the heap, which
    // then surfaces as a crash somewhere else entirely.
    // ndarray_new_dense_ndarray() fills results->shape completely, padding
    // unused dimensions with 1.
    size_t k = 0;
    do {
        size_t l = 0;
        do {
            int32_t a = get_lhs(larray), b = get_rhs(rarray);
            if(compare) {
                *(uint8_t *)out = ndarray_int32_compare(op, a, b);
                out += 1;
            } else {
                *(int32_t *)out = ndarray_int32_apply(op, a, b, &ok);
                out += 4;
                if(!ok) break;
            }
            larray += lstrides[ULAB_MAX_DIMS - 1];
            rarray += rstrides[ULAB_MAX_DIMS - 1];
            l++;
        } while(l < results->shape[ULAB_MAX_DIMS - 1]);
        if(!ok) break;
        larray -= lstrides[ULAB_MAX_DIMS - 1] * results->shape[ULAB_MAX_DIMS - 1];
        larray += lstrides[ULAB_MAX_DIMS - 2];
        rarray -= rstrides[ULAB_MAX_DIMS - 1] * results->shape[ULAB_MAX_DIMS - 1];
        rarray += rstrides[ULAB_MAX_DIMS - 2];
        k++;
    } while(k < results->shape[ULAB_MAX_DIMS - 2]);

    if(!ok) {   // only reachable from a negative integer power
        mp_raise_ValueError(MP_ERROR_TEXT("integers to negative integer powers are not allowed"));
    }
    return MP_OBJ_FROM_PTR(results);
}

// The in-place counterpart. ulab's UNWRAP_INPLACE_OPERATOR has the same closed-
// world shape as the binary chains -- its innermost `else` assumes int16 -- so
// an int32 operand has to be intercepted here too rather than fall into it.
// The result is written back in the left operand's own dtype, which is what
// in-place means, and matches NumPy.
mp_obj_t ndarray_inplace_int32(mp_binary_op_t op, ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                               int32_t *rstrides) {

    if((lhs->dtype != NDARRAY_INT32) && (rhs->dtype == NDARRAY_INT32)) {
        // widening the left operand in place is not possible, and NumPy refuses
        // the same way ("cannot cast output with casting rule")
        mp_raise_TypeError(MP_ERROR_TEXT("cannot cast output with casting rule"));
    }
    int32_t (*get_rhs)(void *) = (int32_t (*)(void *))ndarray_get_int32_function(rhs->dtype);
    if(get_rhs == NULL) {
        mp_raise_TypeError(MP_ERROR_TEXT("cannot cast output with casting rule"));
    }

    // Matches what ulab already does for its own integer dtypes: /= and **= are
    // refused on an integer array (measured on int16 and uint8 -- both raise
    // "results cannot be cast to specified type"), while +=, -=, *= and %=
    // work and preserve the dtype. int32 behaves the same way rather than
    // inventing a third rule.
    mp_binary_op_t base;
    switch(op) {
        case MP_BINARY_OP_INPLACE_ADD:      base = MP_BINARY_OP_ADD; break;
        case MP_BINARY_OP_INPLACE_SUBTRACT: base = MP_BINARY_OP_SUBTRACT; break;
        case MP_BINARY_OP_INPLACE_MULTIPLY: base = MP_BINARY_OP_MULTIPLY; break;
        case MP_BINARY_OP_INPLACE_MODULO:   base = MP_BINARY_OP_MODULO; break;
        default:
            mp_raise_TypeError(MP_ERROR_TEXT("results cannot be cast to specified type"));
            return mp_const_none;
    }

    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;
    bool ok = true;

    size_t k = 0;
    do {
        size_t l = 0;
        do {
            int32_t a = *(int32_t *)larray, b = get_rhs(rarray);
            *(int32_t *)larray = ndarray_int32_apply(base, a, b, &ok);
            if(!ok) break;
            larray += lhs->strides[ULAB_MAX_DIMS - 1];
            rarray += rstrides[ULAB_MAX_DIMS - 1];
            l++;
        } while(l < lhs->shape[ULAB_MAX_DIMS - 1]);
        if(!ok) break;
        larray -= lhs->strides[ULAB_MAX_DIMS - 1] * lhs->shape[ULAB_MAX_DIMS - 1];
        larray += lhs->strides[ULAB_MAX_DIMS - 2];
        rarray -= rstrides[ULAB_MAX_DIMS - 1] * lhs->shape[ULAB_MAX_DIMS - 1];
        rarray += rstrides[ULAB_MAX_DIMS - 2];
        k++;
    } while(k < lhs->shape[ULAB_MAX_DIMS - 2]);

    if(!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("integers to negative integer powers are not allowed"));
    }
    return MP_OBJ_FROM_PTR(lhs);
}

#if NDARRAY_HAS_BINARY_OP_EQUAL | NDARRAY_HAS_BINARY_OP_NOT_EQUAL
mp_obj_t ndarray_binary_equality(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape,  int32_t *lstrides, int32_t *rstrides, mp_binary_op_t op) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX))  {
        return carray_binary_equal_not_equal(lhs, rhs, ndim, shape, lstrides, rstrides, op);
    }
    #endif

    ndarray_obj_t *results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
    results->boolean = 1;
    uint8_t *array = (uint8_t *)results->array;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    #if NDARRAY_HAS_BINARY_OP_EQUAL
    if(op == MP_BINARY_OP_EQUAL) {
        if(lhs->dtype == NDARRAY_UINT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint8_t, int8_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint8_t, int16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, ==);
            }
        } else if(lhs->dtype == NDARRAY_INT8) {
            if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int8_t, int8_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int8_t, uint16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int8_t, int16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, ==);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_UINT16) {
            if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint16_t, int16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, ==);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_INT16) {
            if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int16_t, int16_t, larray, lstrides, rarray, rstrides, ==);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int16_t, mp_float_t, larray, lstrides, rarray, rstrides, ==);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_FLOAT) {
            if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, ==);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        }
    }
    #endif /* NDARRAY_HAS_BINARY_OP_EQUAL */

    #if NDARRAY_HAS_BINARY_OP_NOT_EQUAL
    if(op == MP_BINARY_OP_NOT_EQUAL) {
        if(lhs->dtype == NDARRAY_UINT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint8_t, int8_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint8_t, int16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, !=);
            }
        } else if(lhs->dtype == NDARRAY_INT8) {
            if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int8_t, int8_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int8_t, uint16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int8_t, int16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, !=);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_UINT16) {
            if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint16_t, int16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, !=);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_INT16) {
            if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int16_t, int16_t, larray, lstrides, rarray, rstrides, !=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int16_t, mp_float_t, larray, lstrides, rarray, rstrides, !=);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        } else if(lhs->dtype == NDARRAY_FLOAT) {
            if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, !=);
            } else {
                return ndarray_binary_op(op, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
            }
        }
    }
    #endif /* NDARRAY_HAS_BINARY_OP_NOT_EQUAL */

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_EQUAL | NDARRAY_HAS_BINARY_OP_NOT_EQUAL */

#if NDARRAY_HAS_BINARY_OP_ADD
mp_obj_t ndarray_binary_add(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                        uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX))  {
        return carray_binary_add(lhs, rhs, ndim, shape, lstrides, rstrides);
    }
    #endif

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
            BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, +);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
            BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, +);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_ADD, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, +);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_ADD, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, +);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int16_t, mp_float_t, larray, lstrides, rarray, rstrides, +);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_ADD, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, +);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_ADD, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    }

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_ADD */

#if NDARRAY_HAS_BINARY_OP_MODULO
mp_obj_t ndarray_binary_modulo(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                        uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
            BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, uint8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
            BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, int8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, uint8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int8_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int16_t, uint16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, %);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, int16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, mp_float_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, mp_float_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            MODULO_FLOAT_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    }

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_MODULO */

#if NDARRAY_HAS_BINARY_OP_MULTIPLY
mp_obj_t ndarray_binary_multiply(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX))  {
        return carray_binary_multiply(lhs, rhs, ndim, shape, lstrides, rstrides);
    }
    #endif

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
            BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, *);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
            BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, *);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_MULTIPLY, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, *);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_MULTIPLY, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, *);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int16_t, mp_float_t, larray, lstrides, rarray, rstrides, *);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_MULTIPLY, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, *);
        } else {
            return ndarray_binary_op(MP_BINARY_OP_MULTIPLY, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
        }
    }

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_MULTIPLY */

#if NDARRAY_HAS_BINARY_OP_MORE | NDARRAY_HAS_BINARY_OP_MORE_EQUAL | NDARRAY_HAS_BINARY_OP_LESS | NDARRAY_HAS_BINARY_OP_LESS_EQUAL
mp_obj_t ndarray_binary_more(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides, mp_binary_op_t op) {

    ndarray_obj_t *results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
    results->boolean = 1;
    uint8_t *array = (uint8_t *)results->array;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    #if NDARRAY_HAS_BINARY_OP_MORE | NDARRAY_HAS_BINARY_OP_LESS
    if(op == MP_BINARY_OP_MORE) {
        if(lhs->dtype == NDARRAY_UINT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint8_t, int8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint8_t, int16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, >);
            }
        } else if(lhs->dtype == NDARRAY_INT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, int8_t, uint8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int8_t, int8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int8_t, uint16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int8_t, int16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, >);
            }
        } else if(lhs->dtype == NDARRAY_UINT16) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint16_t, uint8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint16_t, int8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint16_t, int16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, >);
            }
        } else if(lhs->dtype == NDARRAY_INT16) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, int16_t, uint8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int16_t, int8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int16_t, uint16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int16_t, int16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, >);
            }
        } else if(lhs->dtype == NDARRAY_FLOAT) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, mp_float_t, int8_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, mp_float_t, int16_t, larray, lstrides, rarray, rstrides, >);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, >);
            }
        }
    }
    #endif /* NDARRAY_HAS_BINARY_OP_MORE | NDARRAY_HAS_BINARY_OP_LESS*/
    #if NDARRAY_HAS_BINARY_OP_MORE_EQUAL | NDARRAY_HAS_BINARY_OP_LESS_EQUAL
    if(op == MP_BINARY_OP_MORE_EQUAL) {
        if(lhs->dtype == NDARRAY_UINT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint8_t, int8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint8_t, int16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, >=);
            }
        } else if(lhs->dtype == NDARRAY_INT8) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, int8_t, uint8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int8_t, int8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int8_t, uint16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int8_t, int16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, >=);
            }
        } else if(lhs->dtype == NDARRAY_UINT16) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, uint16_t, uint8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, uint16_t, int8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, uint16_t, int16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, >=);
            }
        } else if(lhs->dtype == NDARRAY_INT16) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, int16_t, uint8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, int16_t, int8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, int16_t, uint16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, int16_t, int16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, >=);
            }
        } else if(lhs->dtype == NDARRAY_FLOAT) {
            if(rhs->dtype == NDARRAY_UINT8) {
                EQUALITY_LOOP(results, array, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT8) {
                EQUALITY_LOOP(results, array, mp_float_t, int8_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_UINT16) {
                EQUALITY_LOOP(results, array, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_INT16) {
                EQUALITY_LOOP(results, array, mp_float_t, int16_t, larray, lstrides, rarray, rstrides, >=);
            } else if(rhs->dtype == NDARRAY_FLOAT) {
                EQUALITY_LOOP(results, array, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, >=);
            }
        }
    }
    #endif /* NDARRAY_HAS_BINARY_OP_MORE_EQUAL | NDARRAY_HAS_BINARY_OP_LESS_EQUAL */

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_MORE | NDARRAY_HAS_BINARY_OP_MORE_EQUAL | NDARRAY_HAS_BINARY_OP_LESS | NDARRAY_HAS_BINARY_OP_LESS_EQUAL */

#if NDARRAY_HAS_BINARY_OP_SUBTRACT
mp_obj_t ndarray_binary_subtract(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX))  {
        return carray_binary_subtract(lhs, rhs, ndim, shape, lstrides, rstrides);
    }
    #endif

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
            BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, -);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, uint8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
            BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, -);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, int8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, -);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, uint8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, int16_t, uint16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, -);
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, int8_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, int16_t, larray, lstrides, rarray, rstrides, -);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            BINARY_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, -);
        }
    }

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_SUBTRACT */

#if NDARRAY_HAS_BINARY_OP_TRUE_DIVIDE
mp_obj_t ndarray_binary_true_divide(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX))  {
        return carray_binary_divide(lhs, rhs, ndim, shape, lstrides, rstrides);
    }
    #endif

    ndarray_obj_t *results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    #if NDARRAY_BINARY_USES_FUN_POINTER
    mp_float_t (*get_lhs)(void *) = ndarray_get_float_function(lhs->dtype);
    mp_float_t (*get_rhs)(void *) = ndarray_get_float_function(rhs->dtype);

    uint8_t *array = (uint8_t *)results->array;
    void (*set_result)(void *, mp_float_t ) = ndarray_set_float_function(NDARRAY_FLOAT);

    // Note that lvalue and rvalue are local variables in the macro itself
    FUNC_POINTER_LOOP(results, array, get_lhs, get_rhs, larray, lstrides, rarray, rstrides, lvalue/rvalue);

    #else
    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            BINARY_LOOP(results, mp_float_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT8) {
            BINARY_LOOP(results, mp_float_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            BINARY_LOOP(results, mp_float_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT16) {
            BINARY_LOOP(results, mp_float_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            BINARY_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides, /);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            BINARY_LOOP(results, mp_float_t, int8_t, uint8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT8) {
            BINARY_LOOP(results, mp_float_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            BINARY_LOOP(results, mp_float_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT16) {
            BINARY_LOOP(results, mp_float_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            BINARY_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides, /);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            BINARY_LOOP(results, mp_float_t, uint16_t, uint8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT8) {
            BINARY_LOOP(results, mp_float_t, uint16_t, int8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            BINARY_LOOP(results, mp_float_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT16) {
            BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            BINARY_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides, /);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            BINARY_LOOP(results, mp_float_t, int16_t, uint8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT8) {
            BINARY_LOOP(results, mp_float_t, int16_t, int8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            BINARY_LOOP(results, mp_float_t, int16_t, uint16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT16) {
            BINARY_LOOP(results, mp_float_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            BINARY_LOOP(results, mp_float_t, int16_t, mp_float_t, larray, lstrides, rarray, rstrides, /);
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_UINT8) {
            BINARY_LOOP(results, mp_float_t, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT8) {
            BINARY_LOOP(results, mp_float_t, mp_float_t, int8_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            BINARY_LOOP(results, mp_float_t, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_INT16) {
            BINARY_LOOP(results, mp_float_t, mp_float_t, int16_t, larray, lstrides, rarray, rstrides, /);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            BINARY_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides, /);
        }
    }
    #endif /* NDARRAY_BINARY_USES_FUN_POINTER */

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_TRUE_DIVIDE */

#if NDARRAY_HAS_BINARY_OP_FLOOR_DIVIDE
mp_obj_t ndarray_binary_floor_divide(ndarray_obj_t *lhs, ndarray_obj_t *rhs, 
                                                    uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
            FLOOR_DIVIDE_LOOP_UINT(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            FLOOR_DIVIDE_LOOP_UINT(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, int8_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
            FLOOR_DIVIDE_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            FLOOR_DIVIDE_LOOP(results, uint16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            FLOOR_DIVIDE_LOOP_UINT(results, uint16_t, uint16_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            FLOOR_DIVIDE_LOOP(results, uint16_t, uint16_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
            FLOOR_DIVIDE_LOOP_UINT(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, int16_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, int16_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, int16_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
            FLOOR_DIVIDE_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
        if(rhs->dtype == NDARRAY_UINT8) {
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, mp_float_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, mp_float_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            FLOOR_DIVIDE_LOOP_FLOAT(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    }

    return MP_OBJ_FROM_PTR(results);

}
#endif /* NDARRAY_HAS_BINARY_OP_FLOOR_DIVIDE */

#if NDARRAY_HAS_BINARY_OP_POWER
mp_obj_t ndarray_binary_power(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape, int32_t *lstrides, int32_t *rstrides) {

    // Note that numpy upcasts the results to int64, if the inputs are of integer type,
    // while we always return a float array.
    ndarray_obj_t *results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    #if NDARRAY_BINARY_USES_FUN_POINTER
    mp_float_t (*get_lhs)(void *) = ndarray_get_float_function(lhs->dtype);
    mp_float_t (*get_rhs)(void *) = ndarray_get_float_function(rhs->dtype);

    uint8_t *array = (uint8_t *)results->array;
    void (*set_result)(void *, mp_float_t ) = ndarray_set_float_function(NDARRAY_FLOAT);

    // Note that lvalue and rvalue are local variables in the macro itself
    FUNC_POINTER_LOOP(results, array, get_lhs, get_rhs, larray, lstrides, rarray, rstrides, MICROPY_FLOAT_C_FUN(pow)(lvalue, rvalue));

    #else
    if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            POWER_LOOP(results, mp_float_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            POWER_LOOP(results, mp_float_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            POWER_LOOP(results, mp_float_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            POWER_LOOP(results, mp_float_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            POWER_LOOP(results, mp_float_t, uint8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            POWER_LOOP(results, mp_float_t, int8_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            POWER_LOOP(results, mp_float_t, int8_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            POWER_LOOP(results, mp_float_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            POWER_LOOP(results, mp_float_t, int8_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            POWER_LOOP(results, mp_float_t, int8_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            POWER_LOOP(results, mp_float_t, uint16_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            POWER_LOOP(results, mp_float_t, uint16_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            POWER_LOOP(results, mp_float_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            POWER_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            POWER_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            POWER_LOOP(results, mp_float_t, int16_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            POWER_LOOP(results, mp_float_t, int16_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            POWER_LOOP(results, mp_float_t, int16_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            POWER_LOOP(results, mp_float_t, int16_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            POWER_LOOP(results, mp_float_t, uint16_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        if(rhs->dtype == NDARRAY_UINT8) {
            POWER_LOOP(results, mp_float_t, mp_float_t, uint8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            POWER_LOOP(results, mp_float_t, mp_float_t, int8_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            POWER_LOOP(results, mp_float_t, mp_float_t, uint16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            POWER_LOOP(results, mp_float_t, mp_float_t, int16_t, larray, lstrides, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_FLOAT) {
            POWER_LOOP(results, mp_float_t, mp_float_t, mp_float_t, larray, lstrides, rarray, rstrides);
        }
    }
    #endif /* NDARRAY_BINARY_USES_FUN_POINTER */

    return MP_OBJ_FROM_PTR(results);
}
#endif /* NDARRAY_HAS_BINARY_OP_POWER */

#if NDARRAY_HAS_BINARY_OP_OR | NDARRAY_HAS_BINARY_OP_XOR | NDARRAY_HAS_BINARY_OP_AND
mp_obj_t ndarray_binary_logical(ndarray_obj_t *lhs, ndarray_obj_t *rhs,
                                            uint8_t ndim, size_t *shape,  int32_t *lstrides, int32_t *rstrides, mp_binary_op_t op) {

    #if ULAB_SUPPORTS_COMPLEX
    if((lhs->dtype == NDARRAY_COMPLEX) || (rhs->dtype == NDARRAY_COMPLEX) || (lhs->dtype == NDARRAY_FLOAT) || (rhs->dtype == NDARRAY_FLOAT))  {
        mp_raise_TypeError(MP_ERROR_TEXT("operation not supported for the input types"));
    }
    #else    
    if((lhs->dtype == NDARRAY_FLOAT) || (rhs->dtype == NDARRAY_FLOAT)) {
        mp_raise_TypeError(MP_ERROR_TEXT("operation not supported for the input types"));
    }
    #endif

    // bail out, if both inputs are of 16-bit types, but differ in sign;
    // numpy promotes the result to int32
    if(((lhs->dtype == NDARRAY_INT16) && (rhs->dtype == NDARRAY_UINT16)) || 
        ((lhs->dtype == NDARRAY_UINT16) && (rhs->dtype == NDARRAY_INT16))) {
        mp_raise_TypeError(MP_ERROR_TEXT("dtype of int32 is not supported"));
    }

    ndarray_obj_t *results = NULL;
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;


    switch(op) {
        case MP_BINARY_OP_XOR:
            if(lhs->dtype == NDARRAY_UINT8) {
                if(rhs->dtype == NDARRAY_UINT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
                    if(lhs->boolean & rhs->boolean) {
                        results->boolean = 1;
                    }
                    BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, ^);
                }
            } else if(lhs->dtype == NDARRAY_INT8) {
                if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
                    BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, ^);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_XOR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_UINT16) {
                if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, ^);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
                    BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, ^);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_XOR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_INT16) {
                if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, ^);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_XOR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            }
            break;

        case MP_BINARY_OP_OR:
            if(lhs->dtype == NDARRAY_UINT8) {
                if(rhs->dtype == NDARRAY_UINT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
                    if(lhs->boolean & rhs->boolean) {
                        results->boolean = 1;
                    }
                    BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, |);
                }
            } else if(lhs->dtype == NDARRAY_INT8) {
                if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
                    BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, |);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_OR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_UINT16) {
                if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, |);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
                    BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, |);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_OR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_INT16) {
                if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, |);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_OR, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            }
            break;

            case MP_BINARY_OP_AND:
            if(lhs->dtype == NDARRAY_UINT8) {
                if(rhs->dtype == NDARRAY_UINT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT8);
                    if(lhs->boolean & rhs->boolean) {
                        results->boolean = 1;
                    }
                    BINARY_LOOP(results, uint8_t, uint8_t, uint8_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int8_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint8_t, uint16_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, uint8_t, int16_t, larray, lstrides, rarray, rstrides, &);
                }
            } else if(lhs->dtype == NDARRAY_INT8) {
                if(rhs->dtype == NDARRAY_INT8) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT8);
                    BINARY_LOOP(results, int8_t, int8_t, int8_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, uint16_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int8_t, int16_t, larray, lstrides, rarray, rstrides, &);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_AND, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_UINT16) {
                if(rhs->dtype == NDARRAY_UINT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_UINT16);
                    BINARY_LOOP(results, uint16_t, uint16_t, uint16_t, larray, lstrides, rarray, rstrides, &);
                } else if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_FLOAT);
                    BINARY_LOOP(results, mp_float_t, uint16_t, int16_t, larray, lstrides, rarray, rstrides, &);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_AND, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            } else if(lhs->dtype == NDARRAY_INT16) {
                if(rhs->dtype == NDARRAY_INT16) {
                    results = ndarray_new_dense_ndarray(ndim, shape, NDARRAY_INT16);
                    BINARY_LOOP(results, int16_t, int16_t, int16_t, larray, lstrides, rarray, rstrides, &);
                } else {
                    return ndarray_binary_op(MP_BINARY_OP_AND, MP_OBJ_FROM_PTR(rhs), MP_OBJ_FROM_PTR(lhs));
                }
            }
            break;
        default:
            return MP_OBJ_NULL; // op not supported
            break;
    }
    return MP_OBJ_FROM_PTR(results);
}

#endif /* NDARRAY_HAS_BINARY_OP_OR | NDARRAY_HAS_BINARY_OP_XOR | NDARRAY_HAS_BINARY_OP_AND */

#if NDARRAY_HAS_INPLACE_ADD || NDARRAY_HAS_INPLACE_MULTIPLY || NDARRAY_HAS_INPLACE_SUBTRACT
mp_obj_t ndarray_inplace_ams(ndarray_obj_t *lhs, ndarray_obj_t *rhs, int32_t *rstrides, uint8_t optype) {

    if((lhs->dtype != NDARRAY_FLOAT) && (rhs->dtype == NDARRAY_FLOAT)) {
        mp_raise_TypeError(MP_ERROR_TEXT("cannot cast output with casting rule"));
    }
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    #if NDARRAY_HAS_INPLACE_ADD
    if(optype == MP_BINARY_OP_INPLACE_ADD) {
        UNWRAP_INPLACE_OPERATOR(lhs, larray, rarray, rstrides, +=);
    }
    #endif
    #if NDARRAY_HAS_INPLACE_MULTIPLY
    if(optype == MP_BINARY_OP_INPLACE_MULTIPLY) {
        UNWRAP_INPLACE_OPERATOR(lhs, larray, rarray, rstrides, *=);
    }
    #endif
    #if NDARRAY_HAS_INPLACE_SUBTRACT
    if(optype == MP_BINARY_OP_INPLACE_SUBTRACT) {
        UNWRAP_INPLACE_OPERATOR(lhs, larray, rarray, rstrides, -=);
    }
    #endif

    return MP_OBJ_FROM_PTR(lhs);
}
#endif /* NDARRAY_HAS_INPLACE_ADD || NDARRAY_HAS_INPLACE_MULTIPLY || NDARRAY_HAS_INPLACE_SUBTRACT */


#if NDARRAY_HAS_INPLACE_MODULO
mp_obj_t ndarray_inplace_modulo(ndarray_obj_t *lhs, ndarray_obj_t *rhs, int32_t *rstrides) {
    if((lhs->dtype != NDARRAY_FLOAT) && (rhs->dtype == NDARRAY_FLOAT)) {
        mp_raise_TypeError(MP_ERROR_TEXT("cannot cast output with casting rule"));
    }
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;
    if(lhs->dtype == NDARRAY_FLOAT) {
        // Float lhs: use fmod since C does not support %= for floating-point types.
        if(rhs->dtype == NDARRAY_UINT8) {
            INLINE_MODULO_FLOAT_LOOP(lhs, uint8_t, larray, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT8) {
            INLINE_MODULO_FLOAT_LOOP(lhs, int8_t, larray, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            INLINE_MODULO_FLOAT_LOOP(lhs, uint16_t, larray, rarray, rstrides);
        } else if(rhs->dtype == NDARRAY_INT16) {
            INLINE_MODULO_FLOAT_LOOP(lhs, int16_t, larray, rarray, rstrides);
        } else {
            INLINE_MODULO_FLOAT_LOOP(lhs, mp_float_t, larray, rarray, rstrides);
        }
    } else if(lhs->dtype == NDARRAY_UINT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            INPLACE_LOOP(lhs, uint8_t, uint8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_INT8) {
            INPLACE_LOOP(lhs, uint8_t, int8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            INPLACE_LOOP(lhs, uint8_t, uint16_t, larray, rarray, rstrides, %=);
        } else {
            INPLACE_LOOP(lhs, uint8_t, int16_t, larray, rarray, rstrides, %=);
        }
    } else if(lhs->dtype == NDARRAY_INT8) {
        if(rhs->dtype == NDARRAY_UINT8) {
            INPLACE_LOOP(lhs, int8_t, uint8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_INT8) {
            INPLACE_LOOP(lhs, int8_t, int8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            INPLACE_LOOP(lhs, int8_t, uint16_t, larray, rarray, rstrides, %=);
        } else {
            INPLACE_LOOP(lhs, int8_t, int16_t, larray, rarray, rstrides, %=);
        }
    } else if(lhs->dtype == NDARRAY_UINT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            INPLACE_LOOP(lhs, uint16_t, uint8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_INT8) {
            INPLACE_LOOP(lhs, uint16_t, int8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            INPLACE_LOOP(lhs, uint16_t, uint16_t, larray, rarray, rstrides, %=);
        } else {
            INPLACE_LOOP(lhs, uint16_t, int16_t, larray, rarray, rstrides, %=);
        }
    } else if(lhs->dtype == NDARRAY_INT16) {
        if(rhs->dtype == NDARRAY_UINT8) {
            INPLACE_LOOP(lhs, int16_t, uint8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_INT8) {
            INPLACE_LOOP(lhs, int16_t, int8_t, larray, rarray, rstrides, %=);
        } else if(rhs->dtype == NDARRAY_UINT16) {
            INPLACE_LOOP(lhs, int16_t, uint16_t, larray, rarray, rstrides, %=);
        } else {
            INPLACE_LOOP(lhs, int16_t, int16_t, larray, rarray, rstrides, %=);
        }
    }
    return MP_OBJ_FROM_PTR(lhs);
}
#endif /* NDARRAY_HAS_INPLACE_MODULO */

#if NDARRAY_HAS_INPLACE_TRUE_DIVIDE
mp_obj_t ndarray_inplace_divide(ndarray_obj_t *lhs, ndarray_obj_t *rhs, int32_t *rstrides) {

    if((lhs->dtype != NDARRAY_FLOAT)) {
        mp_raise_TypeError(MP_ERROR_TEXT("results cannot be cast to specified type"));
    }
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(rhs->dtype == NDARRAY_UINT8) {
        INPLACE_LOOP(lhs, mp_float_t, uint8_t, larray, rarray, rstrides, /=);
    } else if(rhs->dtype == NDARRAY_INT8) {
        INPLACE_LOOP(lhs, mp_float_t, int8_t, larray, rarray, rstrides, /=);
    } else if(rhs->dtype == NDARRAY_UINT16) {
        INPLACE_LOOP(lhs, mp_float_t, uint16_t, larray, rarray, rstrides, /=);
    } else if(rhs->dtype == NDARRAY_INT16) {
        INPLACE_LOOP(lhs, mp_float_t, int16_t, larray, rarray, rstrides, /=);
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        INPLACE_LOOP(lhs, mp_float_t, mp_float_t, larray, rarray, rstrides, /=);
    }
    return MP_OBJ_FROM_PTR(lhs);
}
#endif /* NDARRAY_HAS_INPLACE_TRUE_DIVIDE */

#if NDARRAY_HAS_INPLACE_POWER
mp_obj_t ndarray_inplace_power(ndarray_obj_t *lhs, ndarray_obj_t *rhs, int32_t *rstrides) {

    if((lhs->dtype != NDARRAY_FLOAT)) {
        mp_raise_TypeError(MP_ERROR_TEXT("results cannot be cast to specified type"));
    }
    uint8_t *larray = (uint8_t *)lhs->array;
    uint8_t *rarray = (uint8_t *)rhs->array;

    if(rhs->dtype == NDARRAY_UINT8) {
        INPLACE_POWER(lhs, mp_float_t, uint8_t, larray, rarray, rstrides);
    } else if(rhs->dtype == NDARRAY_INT8) {
        INPLACE_POWER(lhs, mp_float_t, int8_t, larray, rarray, rstrides);
    } else if(rhs->dtype == NDARRAY_UINT16) {
        INPLACE_POWER(lhs, mp_float_t, uint16_t, larray, rarray, rstrides);
    } else if(rhs->dtype == NDARRAY_INT16) {
        INPLACE_POWER(lhs, mp_float_t, int16_t, larray, rarray, rstrides);
    } else if(lhs->dtype == NDARRAY_FLOAT) {
        INPLACE_POWER(lhs, mp_float_t, mp_float_t, larray, rarray, rstrides);
    }
    return MP_OBJ_FROM_PTR(lhs);
}
#endif /* NDARRAY_HAS_INPLACE_POWER */
