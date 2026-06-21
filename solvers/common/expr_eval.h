// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// expr_eval.h — Recursive-descent expression evaluator.
//
// Single grammar shared by the IPC command parser (small symbol table:
// just I) and the GUI line-edit calculator (large symbol table: units,
// math functions, etc.). Internal value type is CComplex throughout —
// real numbers are CComplex with im=0.
//
// Grammar:
//   expr   = term   (('+'|'-') term)*
//   term   = factor (('*'|'/') factor)*
//   factor = unary  ('^' unary)*
//   unary  = ('-'|'+')? atom
//   atom   = NUMBER | IDENT [ '(' expr (',' expr)* ')' ] | '(' expr ')'
//
// NUMBER tokens accept an optional trailing 'i' or 'I' (no whitespace,
// not part of an identifier) for Octave-style imaginary literals like
// 0.7i or 1.5e-3i. Bare I (or i) as an identifier yields the imaginary
// unit if registered in the symbol table.

#ifndef EXPR_EVAL_H
#define EXPR_EVAL_H

#include "complex.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

class ExprEvaluator {
public:
    // Function callback receives evaluated argument list, returns result.
    using Func = std::function<CComplex(const std::vector<CComplex>&)>;

    ExprEvaluator();

    // Symbol table mutators. Constants and functions share a namespace;
    // a name registered as both is treated as function when followed by
    // an open paren, otherwise as constant.
    void setConstant(const std::string& name, CComplex value);
    void setFunction(const std::string& name, Func fn);
    bool hasConstant(const std::string& name) const;
    bool hasFunction(const std::string& name) const;

    // Evaluate one expression string. Returns true on success and writes
    // the result to *out; on failure, returns false and writes a short
    // diagnostic to *err (if non-null). Trailing whitespace ignored; any
    // unconsumed non-whitespace input is an error.
    bool eval(const std::string& expr, CComplex* out, std::string* err = nullptr);

    // Convenience: evaluate and discard error info, returning 0 on failure.
    CComplex evalOr(const std::string& expr, CComplex fallback = CComplex());

private:
    std::unordered_map<std::string, CComplex> m_consts;
    std::unordered_map<std::string, Func> m_funcs;

    // Per-eval parser state
    const char* m_src = nullptr;
    const char* m_pos = nullptr;
    std::string m_err;

    // Lexer / parser internals
    void skipWs();
    bool peek(char c);
    bool consume(char c);
    bool atEnd();
    bool parseExpr(CComplex* out);
    bool parseTerm(CComplex* out);
    bool parseFactor(CComplex* out);
    bool parseUnary(CComplex* out);
    bool parseAtom(CComplex* out);
    bool parseNumber(CComplex* out);
    bool parseIdent(std::string* out);
    void setError(const std::string& msg);
};

#endif // EXPR_EVAL_H
