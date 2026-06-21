// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// test_py_bind.cpp - exercises every femm_py caster specialization with
// round-trip (C++ -> Python -> C++) and boundary conditions, plus the
// FemmFn/Module call path (arity checking and optional-arg padding).
//
// BUILD/RUN STATUS: this is an integration test against a live MicroPython
// runtime. It compiles and runs only once Phase 2 (MicroPython embedding +
// mpconfigport.h) and Phase 10 (CMake/CTest wiring) are in place; it links
// against the embedded micropython library. It is authored now (Phase 1) so
// it can gate the subsequent phases, exactly as the plan requires.
//
// It expects the standard embed-port runtime symbols (mp_init, gc_init,
// mp_stack_ctrl_init) and a stdout hook (mp_hal_stdout_tx_strn) to be provided
// by the link, as they will be once py_state.cpp exists.
extern "C" {
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
}

#include "py_bind.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("FAIL: %s  (%s:%d)\n", (what), __FILE__, __LINE__); \
        }                                                                   \
    } while (0)

using namespace femm_py;

static bool complex_eq(mp_obj_t o, double re, double im) {
    mp_float_t r = 0, i = 0;
    mp_obj_get_complex(o, &r, &i);
    return (double)r == re && (double)i == im;
}

static void test_scalars() {
    // double round-trip, incl. boundaries
    for (double v : {0.0, -0.0, 3.141592653589793, 1e308, 2.2250738585072014e-308,
                     -1.5e-300, 1234567.0}) {
        CHECK(caster<double>::load(caster<double>::cast(v)) == v, "double round-trip");
    }
    // int round-trip
    for (int v : {0, 1, -1, 42, -32768, 2147483647, -2147483647}) {
        CHECK(caster<int>::load(caster<int>::cast(v)) == v, "int round-trip");
    }
    // int caster accepts exact-integer floats (1.0 -> 1) but rejects
    // non-integer floats (3.5 -> TypeError) to avoid silent truncation.
    CHECK(caster<int>::load(mp_obj_new_float((mp_float_t)3.0)) == 3, "int from float 3.0 -> 3");
    CHECK(caster<int>::load(mp_obj_new_float((mp_float_t)-2.0)) == -2, "int from float -2.0 -> -2");
    {
        bool caught = false;
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            caster<int>::load(mp_obj_new_float((mp_float_t)3.5));
            nlr_pop();
        } else { caught = true; }
        CHECK(caught, "int from float 3.5 -> TypeError");
    }
    // bool
    CHECK(caster<bool>::load(caster<bool>::cast(true)) == true, "bool true");
    CHECK(caster<bool>::load(caster<bool>::cast(false)) == false, "bool false");
    // string, incl. empty and embedded tab/control
    for (const std::string &s : {std::string(""), std::string("hello"),
                                 std::string("a\tb\nc"), std::string("unicode-free")}) {
        CHECK(caster<std::string>::load(caster<std::string>::cast(s)) == s,
              "string round-trip");
    }
}

static void test_complex() {
    struct { double re, im; } cases[] = {
        {0, 0}, {3, -4}, {2.5, 0}, {0, -7.25}, {1e200, -1e-200}, {-3.3, 4.4}};
    for (auto &c : cases) {
        CComplex z(c.re, c.im);
        mp_obj_t o = caster<CComplex>::cast(z);
        CHECK(complex_eq(o, c.re, c.im), "CComplex cast bits");
        CComplex back = caster<CComplex>::load(o);
        CHECK(back.re == c.re && back.im == c.im, "CComplex round-trip");
    }
    // real promotion: int and float load as (re, 0)
    CComplex fromInt = caster<CComplex>::load(mp_obj_new_int(5));
    CHECK(fromInt.re == 5.0 && fromInt.im == 0.0, "int -> CComplex(5,0)");
    CComplex fromFloat = caster<CComplex>::load(mp_obj_new_float((mp_float_t)2.5));
    CHECK(fromFloat.re == 2.5 && fromFloat.im == 0.0, "float -> CComplex(2.5,0)");
}

static void test_optional() {
    // cast
    CHECK(caster<std::optional<int>>::cast(std::nullopt) == mp_const_none,
          "nullopt -> None");
    mp_obj_t some = caster<std::optional<int>>::cast(std::optional<int>(9));
    CHECK(caster<int>::load(some) == 9, "optional value -> 9");
    // load
    CHECK(!caster<std::optional<int>>::load(mp_const_none).has_value(),
          "None -> nullopt");
    auto v = caster<std::optional<double>>::load(mp_obj_new_float((mp_float_t)1.25));
    CHECK(v.has_value() && *v == 1.25, "value -> optional 1.25");
}

static void test_tuple_and_vector() {
    auto t = std::make_tuple(1.5, std::string("x"), CComplex(2, 3));
    mp_obj_t o = caster<std::tuple<double, std::string, CComplex>>::cast(t);
    size_t n = 0;
    mp_obj_t *items = nullptr;
    mp_obj_get_array(o, &n, &items);
    CHECK(n == 3, "tuple len 3");
    if (n == 3) {
        CHECK(caster<double>::load(items[0]) == 1.5, "tuple[0]");
        CHECK(caster<std::string>::load(items[1]) == "x", "tuple[1]");
        CHECK(complex_eq(items[2], 2, 3), "tuple[2]");
    }

    std::vector<double> vd{1, 2, 3, 4};
    std::vector<double> rt = caster<std::vector<double>>::load(
        caster<std::vector<double>>::cast(vd));
    CHECK(rt == vd, "vector<double> round-trip");

    std::vector<std::string> vs{"a", "bb", ""};
    auto rs = caster<std::vector<std::string>>::load(
        caster<std::vector<std::string>>::cast(vs));
    CHECK(rs == vs, "vector<string> round-trip");
}

static void test_module_calls() {
    mp_obj_t d = mp_obj_new_dict(0);
    femm_module.globals = (mp_obj_dict_t *)MP_OBJ_TO_PTR(d);

    femm_module.def("add2", [](double a, double b) { return a + b; });
    femm_module.def("opt", [](int a, std::optional<int> b) {
        return a + b.value_or(100);
    });
    femm_module.def("noret", [](double) { /* void */ });

    mp_obj_t add2 = mp_obj_dict_get(d, MP_OBJ_NEW_QSTR(qstr_from_strn("add2", 4)));
    mp_obj_t opt  = mp_obj_dict_get(d, MP_OBJ_NEW_QSTR(qstr_from_strn("opt", 3)));

    {
        mp_obj_t a[2] = {mp_obj_new_float((mp_float_t)2), mp_obj_new_float((mp_float_t)3)};
        mp_obj_t r = mp_call_function_n_kw(add2, 2, 0, a);
        CHECK(caster<double>::load(r) == 5.0, "add2(2,3)==5");
    }
    {
        mp_obj_t a[1] = {mp_obj_new_int(1)};
        mp_obj_t r = mp_call_function_n_kw(opt, 1, 0, a);  // omit optional
        CHECK(caster<int>::load(r) == 101, "opt(1)==101 (optional padded)");
    }
    {
        mp_obj_t a[2] = {mp_obj_new_int(1), mp_obj_new_int(2)};
        mp_obj_t r = mp_call_function_n_kw(opt, 2, 0, a);
        CHECK(caster<int>::load(r) == 3, "opt(1,2)==3");
    }
    // arity error: too many args must raise TypeError
    {
        nlr_buf_t nlr;
        mp_obj_t a[3] = {mp_obj_new_float((mp_float_t)1), mp_obj_new_float((mp_float_t)2),
                         mp_obj_new_float((mp_float_t)3)};
        bool raised = false;
        if (nlr_push(&nlr) == 0) {
            mp_call_function_n_kw(add2, 3, 0, a);
            nlr_pop();
        } else {
            raised = true;
        }
        CHECK(raised, "add2 with 3 args raises");
    }

    // C++ exception thrown in a bound function must surface as a Python
    // exception (RuntimeError), not crash through MicroPython's C frames.
    femm_module.def("boom", []() -> double { throw std::runtime_error("kaboom"); });
    {
        mp_obj_t boom = mp_obj_dict_get(d, MP_OBJ_NEW_QSTR(qstr_from_strn("boom", 4)));
        nlr_buf_t nlr;
        bool raised = false;
        if (nlr_push(&nlr) == 0) {
            mp_call_function_n_kw(boom, 0, 0, nullptr);
            nlr_pop();
        } else {
            raised = true;
            // the raised object should be exactly a RuntimeError
            mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
            CHECK(mp_obj_get_type(exc) == &mp_type_RuntimeError, "boom -> RuntimeError type");
        }
        CHECK(raised, "C++ throw surfaces as Python exception");
    }
}

// Validates the AC-magnetics argument pattern: a bound function whose
// parameter is CComplex / optional<CComplex> / raw mp_obj_t receives a Python
// complex (e.g. 1+2j) through the real call path, with real->complex promotion.
static void test_complex_args() {
    mp_obj_t d = mp_obj_new_dict(0);
    femm_module.globals = (mp_obj_dict_t *)MP_OBJ_TO_PTR(d);

    femm_module.def("imagof", [](CComplex z) { return z.im; });
    femm_module.def("reof", [](std::optional<CComplex> z) { return z ? z->re : -1.0; });
    femm_module.def("conjraw", [](mp_obj_t o) {
        CComplex z = caster<CComplex>::load(o);
        return CComplex(z.re, -z.im);
    });

    auto get = [&](const char *n) {
        return mp_obj_dict_get(d, MP_OBJ_NEW_QSTR(qstr_from_strn(n, std::strlen(n))));
    };
    mp_obj_t imagof = get("imagof"), reof = get("reof"), conjraw = get("conjraw");

    // CComplex arg: imag of 3+4j is 4; a real promotes to (re,0) so imag is 0.
    {
        mp_obj_t a[1] = {mp_obj_new_complex(3, 4)};
        CHECK(caster<double>::load(mp_call_function_n_kw(imagof, 1, 0, a)) == 4.0,
              "CComplex arg: imag(3+4j)==4");
        mp_obj_t b[1] = {mp_obj_new_float((mp_float_t)5)};
        CHECK(caster<double>::load(mp_call_function_n_kw(imagof, 1, 0, b)) == 0.0,
              "CComplex arg: real 5 promoted -> imag 0");
    }
    // optional<CComplex>: present complex, present real, and omitted.
    {
        mp_obj_t a[1] = {mp_obj_new_complex(2, 3)};
        CHECK(caster<double>::load(mp_call_function_n_kw(reof, 1, 0, a)) == 2.0,
              "optional<CComplex>: re(2+3j)==2");
        mp_obj_t b[1] = {mp_obj_new_int(7)};
        CHECK(caster<double>::load(mp_call_function_n_kw(reof, 1, 0, b)) == 7.0,
              "optional<CComplex>: int 7 promoted -> re 7");
        CHECK(caster<double>::load(mp_call_function_n_kw(reof, 0, 0, nullptr)) == -1.0,
              "optional<CComplex>: omitted -> nullopt");
    }
    // raw mp_obj_t -> caster<CComplex>::load (the modify* value-arg pattern).
    {
        mp_obj_t a[1] = {mp_obj_new_complex(1, 2)};
        CHECK(complex_eq(mp_call_function_n_kw(conjraw, 1, 0, a), 1, -2),
              "mp_obj_t -> CComplex: conj(1+2j)==1-2j");
    }
    // tuple<CComplex,CComplex> return (mo_getcircuitproperties / mo_getgapb pattern).
    femm_module.def("pair", []() { return std::make_tuple(CComplex(1, 2), CComplex(3, -4)); });
    {
        mp_obj_t r = mp_call_function_n_kw(get("pair"), 0, 0, nullptr);
        size_t n = 0;
        mp_obj_t *it = nullptr;
        mp_obj_get_array(r, &n, &it);
        CHECK(n == 2 && complex_eq(it[0], 1, 2) && complex_eq(it[1], 3, -4),
              "tuple<CComplex,CComplex> return");
    }
}

static char heap[128 * 1024];

int main() {
    mp_stack_ctrl_init();
    // Required with MICROPY_STACK_CHECK: an unset (zero) limit fails the
    // first check.
    mp_stack_set_limit(256 * 1024);
    gc_init(heap, heap + sizeof(heap));
    mp_init();

    test_scalars();
    test_complex();
    test_optional();
    test_tuple_and_vector();
    test_module_calls();
    test_complex_args();

    mp_deinit();

    std::printf("py_bind tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
