// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// test_py_state.cpp - smoke test for the embedded interpreter lifecycle.
// Exercises exec_python's expression/statement paths, the shared global
// namespace, the core numeric globals (I, pi, Complex/Re/Im/conj), and error
// capture. Same build/run gating as test_py_bind (needs the embed library).
#include "py_state.h"

extern "C" {
#include "py/runtime.h"
#include "py/obj.h"
#include "py/qstr.h"
}

#include "script_callbacks.h"

#include <cstdio>
#include <cstring>
#include <string>

// Stubs for script_callbacks / py_state_globals are in test_stubs.cpp.

static int g_fail = 0, g_checks = 0;
#define CHECK(c, w)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(c)) { ++g_fail; std::printf("FAIL: %s (%s:%d)\n", (w), __FILE__, __LINE__); } \
    } while (0)

static std::string g_sink;
static void test_sink(const char *s, size_t n) { g_sink.append(s, n); }

static mp_obj_t getg(const char *n) {
    return mp_obj_dict_get(MP_OBJ_FROM_PTR(mp_globals_get()),
                           MP_OBJ_NEW_QSTR(qstr_from_strn(n, std::strlen(n))));
}
static bool ceq(const char *n, double re, double im) {
    mp_float_t r = 0, i = 0;
    mp_obj_complex_get(getg(n), &r, &i);
    return (double)r == re && (double)i == im;
}

int main() {
    init_py();
    char err[512];

    // statement path + namespace readback
    CHECK(exec_python("x = 2 + 3", err, sizeof err) == 0, "assign ok");
    CHECK(mp_obj_get_int(getg("x")) == 5, "x == 5");
    // expression path (prints 10 to stdout; not asserted here)
    CHECK(exec_python("x * 2", err, sizeof err) == 0, "expr ok");

    // I is the imaginary unit: I*I == -1
    CHECK(exec_python("z = I*I", err, sizeof err) == 0, "z = I*I ok");
    CHECK(ceq("z", -1.0, 0.0), "I*I == -1");

    // core complex helpers
    CHECK(exec_python("a = Re(3+4*I)", err, sizeof err) == 0, "Re ok");
    CHECK(mp_obj_get_float(getg("a")) == 3.0, "Re == 3");
    CHECK(exec_python("b = Im(3+4*I)", err, sizeof err) == 0, "Im ok");
    CHECK(mp_obj_get_float(getg("b")) == 4.0, "Im == 4");
    CHECK(exec_python("c = conj(3+4*I)", err, sizeof err) == 0, "conj ok");
    CHECK(ceq("c", 3.0, -4.0), "conj(3+4I) == 3-4I");
    CHECK(exec_python("d = Complex(5)", err, sizeof err) == 0, "Complex(5) ok");
    CHECK(ceq("d", 5.0, 0.0), "Complex(5) == 5+0I");
    CHECK(exec_python("e = Complex(5,6)", err, sizeof err) == 0, "Complex(5,6) ok");
    CHECK(ceq("e", 5.0, 6.0), "Complex(5,6) == 5+6I");

    // pi
    CHECK(exec_python("p = pi", err, sizeof err) == 0, "pi ok");
    double pv = mp_obj_get_float(getg("p"));
    CHECK(pv > 3.1415 && pv < 3.1416, "pi value");

    // multi-line statement (forces the FILE_INPUT path)
    CHECK(exec_python("tot = 0\nfor i in range(5):\n    tot += i", err, sizeof err) == 0,
          "loop ok");
    CHECK(mp_obj_get_int(getg("tot")) == 10, "loop sum == 10");

    // error path: runtime exception is captured, returns nonzero
    // errmsg is the wire-faithful message only (str(exc), no type prefix),
    // matching Lua which sent e.what(). Check the message text, not the type.
    err[0] = 0;
    CHECK(exec_python("1/0", err, sizeof err) != 0, "div0 nonzero");
    CHECK(std::strstr(err, "divide") != nullptr, "div0 message captured");
    err[0] = 0;
    CHECK(exec_python("undefined_name_xyz", err, sizeof err) != 0, "nameerror nonzero");
    CHECK(std::strstr(err, "undefined_name_xyz") != nullptr, "nameerror message captured");

    // ---- Phase 8 output sink: print()/stdout routed to the sink ----
    set_py_output_sink(test_sink);
    g_sink.clear();
    CHECK(exec_python("print('hi', 42)", err, sizeof err) == 0, "print ok");
    CHECK(g_sink == "hi\t42\n", "print() routed to sink");
    // a pure expression's result goes to py_last_result(), NOT to stdout/sink
    g_sink.clear();
    CHECK(exec_python("3+4", err, sizeof err) == 0 && py_last_result() == "7", "expr result");
    CHECK(g_sink.empty(), "expr result not sent to stdout sink");
    set_py_output_sink(nullptr);

    // console-control globals registered (headless no-ops here; GUI overrides)
    CHECK(exec_python("showconsole()", err, sizeof err) == 0, "showconsole callable");
    CHECK(exec_python("clearconsole()", err, sizeof err) == 0, "clearconsole callable");
    CHECK(exec_python("hc = hide_console", err, sizeof err) == 0, "hide_console alias defined");

    // shared namespace persists across exec_python calls (invariant #6)
    CHECK(exec_python("persisted = 99", err, sizeof err) == 0, "set persisted");
    CHECK(exec_python("persisted + 1", err, sizeof err) == 0, "read persisted later");

    // ---- init script (py_init_script): complex-aware math, units, helpers ----
    CHECK(exec_python("rr = sqrt(4.0)", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("rr")) == 2.0, "sqrt(4)==2 (real path)");
    CHECK(exec_python("zz = sqrt(-1+0j)", err, sizeof err) == 0, "sqrt(complex) ok");
    {
        mp_float_t r = 0, i = 0;
        mp_obj_complex_get(getg("zz"), &r, &i);
        CHECK(i > 0.9999 && i < 1.0001 && (double)r < 1e-9 && (double)r > -1e-9,
              "sqrt(-1+0j) ~= 1j");
    }
    CHECK(exec_python("ss = sin(0.0)", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("ss")) == 0.0, "sin(0)==0");
    CHECK(exec_python("ee = exp(0.0)", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("ee")) == 1.0, "exp(0)==1");
    CHECK(exec_python("a2 = atan2(1.0, 1.0)", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("a2")) > 0.785 && mp_obj_get_float(getg("a2")) < 0.786,
          "atan2(1,1)~=pi/4");
    // units
    CHECK(exec_python("ui = inch", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("ui")) == 0.0254, "inch==0.0254");
    CHECK(exec_python("um1 = mm", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("um1")) == 0.001, "mm==0.001");
    CHECK(exec_python("Pv = Pi", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("Pv")) > 3.1415 && mp_obj_get_float(getg("Pv")) < 3.1416,
          "Pi defined");
    // AWG and the helper functions are defined and callable
    CHECK(exec_python("aw = AWG(0)", err, sizeof err) == 0 &&
              mp_obj_get_float(getg("aw")) > 8.25 && mp_obj_get_float(getg("aw")) < 8.26,
          "AWG(0)~=8.2514694");
    CHECK(exec_python("mab = mi_makeABC", err, sizeof err) == 0, "mi_makeABC defined");
    CHECK(exec_python("eab = ei_makeABC", err, sizeof err) == 0, "ei_makeABC defined");
    CHECK(exec_python("cab = ci_makeABC", err, sizeof err) == 0, "ci_makeABC defined");
    CHECK(exec_python("hab = hi_makeABC", err, sizeof err) == 0, "hi_makeABC defined");
    CHECK(exec_python("dl = mi_drawline", err, sizeof err) == 0, "mi_drawline defined");

    // ---- POSIX VFS: native Python file write/read round-trip ----
    CHECK(exec_python("f = open('vfs_rt.txt', 'w'); f.write('hello 42'); f.close()",
                      err, sizeof err) == 0, "open/write ok");
    CHECK(exec_python("g = open('vfs_rt.txt', 'r'); data = g.read(); g.close()",
                      err, sizeof err) == 0, "open/read ok");
    {
        size_t n = 0;
        const char *s = mp_obj_str_get_data(getg("data"), &n);
        CHECK(std::string(s, n) == "hello 42", "file round-trip content");
    }
    std::remove("vfs_rt.txt");

    // ---- Phase 7 wire formatter: exec_python result -> py_last_result() ----
    auto wire = [&](const char *expr, const char *expected, const char *what) {
        int rc = exec_python(expr, err, sizeof err);
        CHECK(rc == 0 && py_last_result() == expected, what);
        if (rc != 0 || py_last_result() != expected)
            std::printf("    expr=%s got=[%s] want=[%s]\n", expr, py_last_result().c_str(), expected);
    };
    // complex: CComplex::ToString special cases
    wire("3+4*I",   "3+I*4",   "complex re+I*im");
    wire("3-4*I",   "3-I*4",   "complex re-I*im");
    wire("I",       "I",       "complex pure +I");
    wire("-I",      "-I",      "complex pure -I");
    wire("2+1*I",   "2+I",     "complex re+I");
    wire("2-1*I",   "2-I",     "complex re-I");
    wire("2*I",     "I*2",     "complex I*im (re==0)");
    wire("-2*I",    "-I*2",    "complex -I*im (re==0)");
    wire("5+0*I",   "5",       "complex im==0 -> real");
    // float / int / bool / str
    wire("5.0",     "5",       "float 5.0 -> 5");
    wire("0.5",     "0.5",     "float 0.5");
    wire("42",      "42",      "int 42");
    wire("True",    "true",    "bool true (Lua-style)");
    wire("False",   "false",   "bool false");
    wire("'hello'", "hello",   "str unquoted");
    // tuple -> tab-separated (multiple return values)
    wire("(1.5, 3+4*I, 'z')", "1.5\t3+I*4\tz", "tuple tab-joined");
    // statement / None -> empty result (action commands, assignments)
    CHECK(exec_python("qq = 7", err, sizeof err) == 0 && py_last_result().empty(),
          "statement -> empty wire result");
    // error -> single-line message (str(exc)), nonzero return
    err[0] = 0;
    CHECK(exec_python("1/0", err, sizeof err) != 0 && py_last_result().empty(),
          "error -> nonzero, empty result");

    // ---- Phase 9 py_eval_* helpers (expression-only, for line-edit widgets) ----
    {
        double d = -999;
        CHECK(py_eval_double("25.4/2", &d) && d == 12.7, "eval_double arith");
        d = -999;
        CHECK(py_eval_double("sqrt(4.0)", &d) && d == 2.0, "eval_double sqrt");
        d = -999;
        CHECK(py_eval_double("3+4*I", &d) && d == 3.0, "eval_double complex->re");
        d = -999;
        CHECK(py_eval_double("42", &d) && d == 42.0, "eval_double int");
        CHECK(!py_eval_double("x = 5", &d), "eval_double rejects statement");
        CHECK(!py_eval_double("bad_var_xyz", &d), "eval_double rejects NameError");
    }
    {
        double re = -1, im = -1;
        CHECK(py_eval_complex("3+4*I", &re, &im) && re == 3.0 && im == 4.0,
              "eval_complex full");
        re = -1; im = -1;
        CHECK(py_eval_complex("7.5", &re, &im) && re == 7.5 && im == 0.0,
              "eval_complex real");
        re = -1; im = -1;
        CHECK(py_eval_complex("10", &re, &im) && re == 10.0 && im == 0.0,
              "eval_complex int");
        CHECK(!py_eval_complex("y = 1", &re, &im), "eval_complex rejects stmt");
    }
    {
        std::string s;
        CHECK(py_eval_string("25.4/2", &s) && s == "12.699999999999999", "eval_string arith");
        s.clear();
        CHECK(py_eval_string("3+4*I", &s) && s == "3+4j", "eval_string complex (j display)");
        s.clear();
        CHECK(py_eval_string("42", &s) && s == "42", "eval_string int");
        s.clear();
        CHECK(py_eval_string("'hello'", &s) && s == "hello", "eval_string str");
        CHECK(!py_eval_string("z = 1", &s), "eval_string rejects stmt");
        CHECK(!py_eval_string("1/0", &s), "eval_string rejects error");
    }

    close_py();
    std::printf("py_state tests: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
