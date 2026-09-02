// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_clinalg.cpp — complex linear algebra for the embedded Python (ulab).
//
// ulab's numpy.linalg rejects complex dtype (COMPLEX_DTYPE_NOT_IMPLEMENTED) and
// has no general (non-symmetric) eig, no qr-for-complex, and no svd. FEMM is
// complex-valued throughout (AC magnetics, eddy currents, AC E/J fields), so
// this fills the gap by exposing the dense complex kernels in
// common/fullmatrix.cpp (eig = Francis double-shift QR; qr = complex Givens) to
// Python. Registered as private globals _clinalg_eig / _clinalg_qr; the init
// script grafts them onto numpy.linalg (best effort) and a `clinalg` namespace.
//
// Data marshalling is nearly free: CComplex is two contiguous doubles (re, im),
// bit-identical to ulab's complex ndarray element (2 * mp_float_t), so a dense
// complex ndarray buffer is reinterpreted directly as a CComplex array.
//
// TODO (follow-ups): svd (lift svdJacobiTall/computeSVD from the .m mtx.cpp to a
// flat CComplex* form), then the cheap derived ops solve / inv / det / pinv /
// cond / rank.

#include "py_bind.h"
#include "py_clinalg.h"
#include "fullmatrix.h"

extern "C" {
#include "py/obj.h"
#include "py/runtime.h"
#include "ndarray.h"   // ulab — header is not extern-"C" guarded, so wrap it
}

namespace {

// Require a dense, 2-D, complex ndarray; optionally square. Raises on misuse.
ndarray_obj_t *check_matrix(mp_obj_t o, bool require_square, const char *who) {
    if (!mp_obj_is_type(o, &ulab_ndarray_type))
        mp_raise_TypeError(MP_ERROR_TEXT("clinalg: argument must be an ndarray"));
    ndarray_obj_t *a = (ndarray_obj_t *)MP_OBJ_TO_PTR(o);
    if (a->dtype != NDARRAY_COMPLEX)
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: argument must be complex"));
    if (a->ndim != 2)
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: argument must be 2-D"));
    if (!ndarray_is_dense(a))
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: argument must be C-contiguous"));
    if (require_square &&
        a->shape[ULAB_MAX_DIMS - 1] != a->shape[ULAB_MAX_DIMS - 2])
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: matrix must be square"));
    (void)who;
    return a;
}

// eig(a) -> (w, v) : w = eigenvalues (n,), v = eigenvectors as columns (n,n)
mp_obj_t clinalg_eig(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], /*square=*/true, "eig");
    int n = (int)a->shape[ULAB_MAX_DIMS - 1];
    const CComplex *A = (const CComplex *)a->array;

    ndarray_obj_t *w = ndarray_new_linear_array((size_t)n, NDARRAY_COMPLEX);
    size_t vsh[ULAB_MAX_DIMS] = { (size_t)n, (size_t)n };
    ndarray_obj_t *v = ndarray_new_dense_ndarray(2, vsh, NDARRAY_COMPLEX);

    if (!femm_linalg::eig(A, n, (CComplex *)w->array, (CComplex *)v->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("eig: computation failed"));

    mp_obj_t tup[2] = { MP_OBJ_FROM_PTR(w), MP_OBJ_FROM_PTR(v) };
    return mp_obj_new_tuple(2, tup);
}

// qr(a) -> (q, r) : complete QR.  q is (m,m), r is (m,n).  A = q @ r.
mp_obj_t clinalg_qr(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], /*square=*/false, "qr");
    int m = (int)a->shape[ULAB_MAX_DIMS - 2];
    int n = (int)a->shape[ULAB_MAX_DIMS - 1];
    const CComplex *A = (const CComplex *)a->array;

    size_t qsh[ULAB_MAX_DIMS] = { (size_t)m, (size_t)m };
    size_t rsh[ULAB_MAX_DIMS] = { (size_t)m, (size_t)n };
    ndarray_obj_t *q = ndarray_new_dense_ndarray(2, qsh, NDARRAY_COMPLEX);
    ndarray_obj_t *r = ndarray_new_dense_ndarray(2, rsh, NDARRAY_COMPLEX);

    if (!femm_linalg::qr(A, m, n, (CComplex *)q->array, (CComplex *)r->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("qr: computation failed"));

    mp_obj_t tup[2] = { MP_OBJ_FROM_PTR(q), MP_OBJ_FROM_PTR(r) };
    return mp_obj_new_tuple(2, tup);
}

// rows/cols of a validated matrix (1-D treated as 1 x len)
void mat_dims(ndarray_obj_t *a, int *m, int *n) {
    if (a->ndim == 2) { *m = (int)a->shape[ULAB_MAX_DIMS - 2]; *n = (int)a->shape[ULAB_MAX_DIMS - 1]; }
    else { *m = 1; *n = (int)a->shape[ULAB_MAX_DIMS - 1]; }
}

ndarray_obj_t *check_complex_dense(mp_obj_t o, const char *who) {
    if (!mp_obj_is_type(o, &ulab_ndarray_type))
        mp_raise_TypeError(MP_ERROR_TEXT("clinalg: argument must be an ndarray"));
    ndarray_obj_t *a = (ndarray_obj_t *)MP_OBJ_TO_PTR(o);
    if (a->dtype != NDARRAY_COMPLEX)
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: argument must be complex"));
    if (!ndarray_is_dense(a))
        mp_raise_ValueError(MP_ERROR_TEXT("clinalg: argument must be C-contiguous"));
    (void)who;
    return a;
}

// svd(a) -> (u, s, vh)  economy: u(m,k), s(k) real, vh(k,n)
mp_obj_t clinalg_svd(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], false, "svd");
    int m, n; mat_dims(a, &m, &n);
    int k = (m < n) ? m : n;
    size_t ush[ULAB_MAX_DIMS] = { (size_t)m, (size_t)k };
    size_t vsh[ULAB_MAX_DIMS] = { (size_t)k, (size_t)n };
    ndarray_obj_t *u  = ndarray_new_dense_ndarray(2, ush, NDARRAY_COMPLEX);
    ndarray_obj_t *s  = ndarray_new_linear_array((size_t)k, NDARRAY_FLOAT);
    ndarray_obj_t *vh = ndarray_new_dense_ndarray(2, vsh, NDARRAY_COMPLEX);
    if (!femm_linalg::svd((const CComplex *)a->array, m, n,
                          (CComplex *)u->array, (double *)s->array, (CComplex *)vh->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("svd: failed"));
    mp_obj_t tup[3] = { MP_OBJ_FROM_PTR(u), MP_OBJ_FROM_PTR(s), MP_OBJ_FROM_PTR(vh) };
    return mp_obj_new_tuple(3, tup);
}

// solve(a, b) -> x;  b is n (vector) or n x nrhs
mp_obj_t clinalg_solve(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], true, "solve");
    int n = (int)a->shape[ULAB_MAX_DIMS - 1];
    ndarray_obj_t *b = check_complex_dense(args[1], "solve");
    bool vec = (b->ndim == 1);
    int brows = vec ? (int)b->shape[ULAB_MAX_DIMS - 1] : (int)b->shape[ULAB_MAX_DIMS - 2];
    int nrhs  = vec ? 1 : (int)b->shape[ULAB_MAX_DIMS - 1];
    if (brows != n) mp_raise_ValueError(MP_ERROR_TEXT("solve: A and b shapes disagree"));
    ndarray_obj_t *x;
    if (vec) x = ndarray_new_linear_array((size_t)n, NDARRAY_COMPLEX);
    else { size_t xsh[ULAB_MAX_DIMS] = { (size_t)n, (size_t)nrhs };
           x = ndarray_new_dense_ndarray(2, xsh, NDARRAY_COMPLEX); }
    if (!femm_linalg::solve((const CComplex *)a->array, n,
                            (const CComplex *)b->array, nrhs, (CComplex *)x->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("solve: singular matrix"));
    return MP_OBJ_FROM_PTR(x);
}

mp_obj_t clinalg_inv(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], true, "inv");
    int n = (int)a->shape[ULAB_MAX_DIMS - 1];
    size_t sh[ULAB_MAX_DIMS] = { (size_t)n, (size_t)n };
    ndarray_obj_t *x = ndarray_new_dense_ndarray(2, sh, NDARRAY_COMPLEX);
    if (!femm_linalg::inv((const CComplex *)a->array, n, (CComplex *)x->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("inv: singular matrix"));
    return MP_OBJ_FROM_PTR(x);
}

mp_obj_t clinalg_det(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], true, "det");
    int n = (int)a->shape[ULAB_MAX_DIMS - 1];
    CComplex d;
    femm_linalg::det((const CComplex *)a->array, n, &d);
    return mp_obj_new_complex(d.re, d.im);
}

// dot(a, b) -> matmul / mat-vec / vec-mat / inner product
mp_obj_t clinalg_dot(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_complex_dense(args[0], "dot");
    ndarray_obj_t *b = check_complex_dense(args[1], "dot");
    const CComplex *A = (const CComplex *)a->array, *B = (const CComplex *)b->array;
    bool a2 = (a->ndim == 2), b2 = (b->ndim == 2);
    int ar = a2 ? (int)a->shape[ULAB_MAX_DIMS - 2] : 1;
    int ac = (int)a->shape[ULAB_MAX_DIMS - 1];
    int br = b2 ? (int)b->shape[ULAB_MAX_DIMS - 2] : (int)b->shape[ULAB_MAX_DIMS - 1];
    int bc = b2 ? (int)b->shape[ULAB_MAX_DIMS - 1] : 1;
    if (ac != br) mp_raise_ValueError(MP_ERROR_TEXT("dot: shapes not aligned"));
    int m = ar, k = ac, n = bc;
    if (a2 && b2) {
        size_t sh[ULAB_MAX_DIMS] = { (size_t)m, (size_t)n };
        ndarray_obj_t *c = ndarray_new_dense_ndarray(2, sh, NDARRAY_COMPLEX);
        femm_linalg::dot(A, m, k, B, n, (CComplex *)c->array);
        return MP_OBJ_FROM_PTR(c);
    } else if (a2 && !b2) {                          // matrix . vector
        ndarray_obj_t *c = ndarray_new_linear_array((size_t)m, NDARRAY_COMPLEX);
        femm_linalg::dot(A, m, k, B, 1, (CComplex *)c->array);
        return MP_OBJ_FROM_PTR(c);
    } else if (!a2 && b2) {                          // vector . matrix
        ndarray_obj_t *c = ndarray_new_linear_array((size_t)n, NDARRAY_COMPLEX);
        femm_linalg::dot(A, 1, k, B, n, (CComplex *)c->array);
        return MP_OBJ_FROM_PTR(c);
    }
    CComplex r;                                      // vector . vector -> scalar
    femm_linalg::dot(A, 1, k, B, 1, &r);
    return mp_obj_new_complex(r.re, r.im);
}

mp_obj_t clinalg_pinv(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], false, "pinv");
    int m, n; mat_dims(a, &m, &n);
    size_t sh[ULAB_MAX_DIMS] = { (size_t)n, (size_t)m };     // pinv is n x m
    ndarray_obj_t *x = ndarray_new_dense_ndarray(2, sh, NDARRAY_COMPLEX);
    if (!femm_linalg::pinv((const CComplex *)a->array, m, n, (CComplex *)x->array))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pinv: failed"));
    return MP_OBJ_FROM_PTR(x);
}

mp_obj_t clinalg_cond(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], false, "cond");
    int m, n; mat_dims(a, &m, &n);
    double c;
    if (!femm_linalg::cond((const CComplex *)a->array, m, n, &c))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("cond: failed"));
    return mp_obj_new_float((mp_float_t)c);
}

mp_obj_t clinalg_rank(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_matrix(args[0], false, "rank");
    int m, n; mat_dims(a, &m, &n);
    int r;
    if (!femm_linalg::rank((const CComplex *)a->array, m, n, &r))
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("rank: failed"));
    return mp_obj_new_int(r);
}

mp_obj_t clinalg_norm(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    ndarray_obj_t *a = check_complex_dense(args[0], "norm");
    double nrm;
    femm_linalg::norm((const CComplex *)a->array, (int)a->len, &nrm);
    return mp_obj_new_float((mp_float_t)nrm);
}

} // namespace

// The @ operator's complex path. ulab's ndarray type handles `A @ B` in C, so
// it cannot reach the Python-level numpy proxy where complex support lives --
// without this, `A @ B` would raise "not implemented for complex dtype" while
// np.dot(A, B) on the same arrays worked, which is a worse state than not
// having @ at all.
//
// Coercion is `x * (1+0j)`, i.e. ulab's own multiply, which is exactly what the
// proxy's _asc() does in Python and yields the dense complex array the clinalg
// kernel requires. Calling ndarray_binary_op for it cannot recurse: multiply is
// not matmul.
static mp_obj_t clinalg_as_complex(mp_obj_t o) {
    return ndarray_binary_op(MP_BINARY_OP_MULTIPLY, o,
                             mp_obj_new_complex(1, 0));
}

static mp_obj_t clinalg_matmul(mp_obj_t a, mp_obj_t b) {
    mp_obj_t args[2] = { clinalg_as_complex(a), clinalg_as_complex(b) };
    return clinalg_dot(2, args);
}

void register_clinalg() {
    // ulab calls this for a matmul with a complex operand; see ndarray.c.
    ndarray_matmul_complex_hook = clinalg_matmul;
    femm_py::femm_module.def_raw("_clinalg_eig",   1, 1, clinalg_eig);
    femm_py::femm_module.def_raw("_clinalg_qr",    1, 1, clinalg_qr);
    femm_py::femm_module.def_raw("_clinalg_svd",   1, 1, clinalg_svd);
    femm_py::femm_module.def_raw("_clinalg_solve", 2, 2, clinalg_solve);
    femm_py::femm_module.def_raw("_clinalg_inv",   1, 1, clinalg_inv);
    femm_py::femm_module.def_raw("_clinalg_det",   1, 1, clinalg_det);
    femm_py::femm_module.def_raw("_clinalg_dot",   2, 2, clinalg_dot);
    femm_py::femm_module.def_raw("_clinalg_pinv",  1, 1, clinalg_pinv);
    femm_py::femm_module.def_raw("_clinalg_cond",  1, 1, clinalg_cond);
    femm_py::femm_module.def_raw("_clinalg_rank",  1, 1, clinalg_rank);
    femm_py::femm_module.def_raw("_clinalg_norm",  1, 1, clinalg_norm);
}
