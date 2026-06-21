// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// mag_dir_eval.cpp — MagDirFctn formula evaluation.

#include "StdAfx.h"
#include "mag_dir_eval.h"
#include "expr_eval.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

constexpr double PI_VAL = 3.141592653589793238462643383279;

template<class F>
auto unary(F&& f) {
    return [f](const std::vector<CComplex>& a) -> CComplex {
        if (a.size() != 1) throw std::runtime_error("expected 1 argument");
        return f(a[0]);
    };
}
template<class F>
auto binary(F&& f) {
    return [f](const std::vector<CComplex>& a) -> CComplex {
        if (a.size() != 2) throw std::runtime_error("expected 2 arguments");
        return f(a[0], a[1]);
    };
}

ExprEvaluator& evaluator()
{
    static ExprEvaluator e;
    static bool initialized = false;
    if (!initialized) {
        // Constants
        e.setConstant("I",  CComplex(0, 1));
        e.setConstant("i",  CComplex(0, 1));
        e.setConstant("j",  CComplex(0, 1));
        e.setConstant("pi", CComplex(PI_VAL));
        e.setConstant("Pi", CComplex(PI_VAL));
        e.setConstant("PI", CComplex(PI_VAL));

        // Math functions — same set as the FemmCalc symbol table.
        e.setFunction("sin",   unary([](CComplex z) { return sin(z); }));
        e.setFunction("cos",   unary([](CComplex z) { return cos(z); }));
        e.setFunction("tan",   unary([](CComplex z) { return tan(z); }));
        e.setFunction("asin",  unary([](CComplex z) { return asin(z); }));
        e.setFunction("acos",  unary([](CComplex z) { return acos(z); }));
        e.setFunction("atan",  unary([](CComplex z) { return atan(z); }));
        e.setFunction("sinh",  unary([](CComplex z) { return sinh(z); }));
        e.setFunction("cosh",  unary([](CComplex z) { return cosh(z); }));
        e.setFunction("tanh",  unary([](CComplex z) { return tanh(z); }));
        e.setFunction("exp",   unary([](CComplex z) { return exp(z); }));
        e.setFunction("log",   unary([](CComplex z) { return log(z); }));
        e.setFunction("ln",    unary([](CComplex z) { return log(z); }));
        e.setFunction("sqrt",  unary([](CComplex z) { return sqrt(z); }));
        e.setFunction("abs",   unary([](CComplex z) { return CComplex(abs(z)); }));
        e.setFunction("absq",  unary([](CComplex z) {
            return CComplex(z.re * z.re + z.im * z.im);
        }));
        e.setFunction("arg",   unary([](CComplex z) { return CComplex(arg(z)); }));
        e.setFunction("conj",  unary([](CComplex z) { return conj(z); }));
        e.setFunction("re",    unary([](CComplex z) { return CComplex(z.re); }));
        e.setFunction("im",    unary([](CComplex z) { return CComplex(z.im); }));
        e.setFunction("Re",    unary([](CComplex z) { return CComplex(z.re); }));
        e.setFunction("Im",    unary([](CComplex z) { return CComplex(z.im); }));
        e.setFunction("atan2", binary([](CComplex y, CComplex x) {
            return CComplex(std::atan2(y.re, x.re));
        }));
        e.setFunction("pow",   binary([](CComplex a, CComplex b) {
            return pow(a, b);
        }));
        e.setFunction("min",   binary([](CComplex a, CComplex b) {
            return a.re < b.re ? a : b;
        }));
        e.setFunction("max",   binary([](CComplex a, CComplex b) {
            return a.re > b.re ? a : b;
        }));
        e.setFunction("floor", unary([](CComplex z) {
            return CComplex(std::floor(z.re), std::floor(z.im));
        }));
        e.setFunction("ceil",  unary([](CComplex z) {
            return CComplex(std::ceil(z.re), std::ceil(z.im));
        }));
        e.setFunction("round", unary([](CComplex z) {
            return CComplex(std::round(z.re), std::round(z.im));
        }));
        e.setFunction("deg",   unary([](CComplex z) {
            return CComplex(z.re * 180.0 / PI_VAL, z.im * 180.0 / PI_VAL);
        }));
        e.setFunction("rad",   unary([](CComplex z) {
            return CComplex(z.re * PI_VAL / 180.0, z.im * PI_VAL / 180.0);
        }));
        e.setFunction("fmod",  binary([](CComplex a, CComplex b) {
            return CComplex(std::fmod(a.re, b.re));
        }));
        e.setFunction("Complex", [](const std::vector<CComplex>& a) -> CComplex {
            if (a.size() == 1) return CComplex(a[0].re);
            if (a.size() == 2) return CComplex(a[0].re, a[1].re);
            throw std::runtime_error("Complex expects 1 or 2 args");
        });
        initialized = true;
    }
    return e;
}

} // namespace

double mag_dir_eval(const char* expr, CComplex X)
{
    ExprEvaluator& e = evaluator();
    // Per-call point variables. In planar problems x/y are canonical and
    // r/z alias to them; in axisymmetric, r/z are canonical and x/y alias.
    // Either way the variable values are the same: r == x == X.re,
    // z == y == X.im.
    e.setConstant("x",     CComplex(X.re));
    e.setConstant("y",     CComplex(X.im));
    e.setConstant("r",     CComplex(X.re));
    e.setConstant("z",     CComplex(X.im));
    e.setConstant("theta", CComplex(arg(X) * 180.0 / PI_VAL));
    e.setConstant("R",     CComplex(abs(X)));

    CComplex result;
    std::string err;
    if (!e.eval(expr, &result, &err)) {
        MsgBox("Error evaluating MagDirFctn \"%s\":\n%s", expr, err.c_str());
        std::exit(7);
    }
    return result.re;
}
