// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// expr_eval.cpp — Recursive-descent expression evaluator implementation.

#include "expr_eval.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>

ExprEvaluator::ExprEvaluator() = default;

void ExprEvaluator::setConstant(const std::string& name, CComplex value) {
    m_consts[name] = value;
}

void ExprEvaluator::setFunction(const std::string& name, Func fn) {
    m_funcs[name] = std::move(fn);
}

bool ExprEvaluator::hasConstant(const std::string& name) const {
    return m_consts.find(name) != m_consts.end();
}

bool ExprEvaluator::hasFunction(const std::string& name) const {
    return m_funcs.find(name) != m_funcs.end();
}

void ExprEvaluator::setError(const std::string& msg) {
    if (m_err.empty()) {
        size_t offset = (size_t)(m_pos - m_src);
        char buf[64];
        snprintf(buf, sizeof(buf), " at offset %zu", offset);
        m_err = msg + buf;
    }
}

void ExprEvaluator::skipWs() {
    while (*m_pos && std::isspace((unsigned char)*m_pos)) m_pos++;
}

bool ExprEvaluator::peek(char c) {
    skipWs();
    return *m_pos == c;
}

bool ExprEvaluator::consume(char c) {
    skipWs();
    if (*m_pos == c) { m_pos++; return true; }
    return false;
}

bool ExprEvaluator::atEnd() {
    skipWs();
    return *m_pos == 0;
}

bool ExprEvaluator::eval(const std::string& expr, CComplex* out, std::string* err) {
    m_src = expr.c_str();
    m_pos = m_src;
    m_err.clear();

    CComplex v;
    if (!parseExpr(&v)) {
        if (err) *err = m_err.empty() ? "parse error" : m_err;
        return false;
    }
    if (!atEnd()) {
        setError("unexpected trailing input");
        if (err) *err = m_err;
        return false;
    }
    *out = v;
    return true;
}

CComplex ExprEvaluator::evalOr(const std::string& expr, CComplex fallback) {
    CComplex v;
    if (eval(expr, &v, nullptr)) return v;
    return fallback;
}

// expr = term (('+'|'-') term)*
bool ExprEvaluator::parseExpr(CComplex* out) {
    if (!parseTerm(out)) return false;
    while (true) {
        skipWs();
        char op = *m_pos;
        if (op != '+' && op != '-') return true;
        m_pos++;
        CComplex rhs;
        if (!parseTerm(&rhs)) return false;
        if (op == '+') *out = *out + rhs;
        else           *out = *out - rhs;
    }
}

// term = factor (('*'|'/') factor)*
bool ExprEvaluator::parseTerm(CComplex* out) {
    if (!parseFactor(out)) return false;
    while (true) {
        skipWs();
        char op = *m_pos;
        if (op != '*' && op != '/') return true;
        m_pos++;
        CComplex rhs;
        if (!parseFactor(&rhs)) return false;
        if (op == '*') *out = *out * rhs;
        else           *out = *out / rhs;
    }
}

// factor = unary ('^' unary)*  (right-associative power)
bool ExprEvaluator::parseFactor(CComplex* out) {
    if (!parseUnary(out)) return false;
    skipWs();
    if (*m_pos == '^') {
        m_pos++;
        CComplex rhs;
        if (!parseFactor(&rhs)) return false;
        *out = pow(*out, rhs);
    }
    return true;
}

// unary = ('-'|'+')? atom
bool ExprEvaluator::parseUnary(CComplex* out) {
    skipWs();
    bool neg = false;
    if (*m_pos == '-') { neg = true;  m_pos++; }
    else if (*m_pos == '+') {        m_pos++; }
    if (!parseAtom(out)) return false;
    if (neg) *out = CComplex(-out->re, -out->im);
    return true;
}

// atom = NUMBER | IDENT ['(' args ')'] | '(' expr ')'
bool ExprEvaluator::parseAtom(CComplex* out) {
    skipWs();
    if (*m_pos == 0) {
        setError("unexpected end of input");
        return false;
    }
    if (*m_pos == '(') {
        m_pos++;
        if (!parseExpr(out)) return false;
        if (!consume(')')) {
            setError("expected ')'");
            return false;
        }
        return true;
    }
    if (std::isdigit((unsigned char)*m_pos) || *m_pos == '.') {
        return parseNumber(out);
    }
    if (std::isalpha((unsigned char)*m_pos) || *m_pos == '_') {
        std::string name;
        if (!parseIdent(&name)) return false;
        skipWs();
        // Function call?
        if (*m_pos == '(') {
            auto fit = m_funcs.find(name);
            if (fit == m_funcs.end()) {
                setError("unknown function '" + name + "'");
                return false;
            }
            m_pos++;  // consume '('
            std::vector<CComplex> args;
            skipWs();
            if (*m_pos != ')') {
                while (true) {
                    CComplex a;
                    if (!parseExpr(&a)) return false;
                    args.push_back(a);
                    skipWs();
                    if (*m_pos == ',') { m_pos++; continue; }
                    break;
                }
            }
            if (!consume(')')) {
                setError("expected ')' in call to '" + name + "'");
                return false;
            }
            try {
                *out = fit->second(args);
            } catch (const std::exception& e) {
                setError(std::string("in '") + name + "': " + e.what());
                return false;
            }
            return true;
        }
        // Constant lookup
        auto cit = m_consts.find(name);
        if (cit == m_consts.end()) {
            setError("unknown identifier '" + name + "'");
            return false;
        }
        *out = cit->second;
        return true;
    }
    setError(std::string("unexpected character '") + *m_pos + "'");
    return false;
}

// NUMBER = digits ('.' digits)? ([eE][-+]?digits)?  ['i' | 'I']
// The trailing imaginary marker only counts when it is NOT followed by
// another identifier character — so 'inch' stays an identifier.
bool ExprEvaluator::parseNumber(CComplex* out) {
    const char* start = m_pos;
    while (std::isdigit((unsigned char)*m_pos)) m_pos++;
    if (*m_pos == '.') {
        m_pos++;
        while (std::isdigit((unsigned char)*m_pos)) m_pos++;
    }
    if (*m_pos == 'e' || *m_pos == 'E') {
        m_pos++;
        if (*m_pos == '+' || *m_pos == '-') m_pos++;
        if (!std::isdigit((unsigned char)*m_pos)) {
            setError("malformed exponent");
            return false;
        }
        while (std::isdigit((unsigned char)*m_pos)) m_pos++;
    }
    if (m_pos == start) {
        setError("expected number");
        return false;
    }
    double val = std::strtod(start, nullptr);

    // Imaginary suffix? Only when 'i'/'I' is directly adjacent and not
    // part of a longer identifier (e.g. 'in', 'Inch').
    if ((*m_pos == 'i' || *m_pos == 'I')) {
        char nx = m_pos[1];
        if (!std::isalnum((unsigned char)nx) && nx != '_') {
            m_pos++;
            *out = CComplex(0, val);
            return true;
        }
    }
    *out = CComplex(val, 0);
    return true;
}

bool ExprEvaluator::parseIdent(std::string* out) {
    skipWs();
    const char* start = m_pos;
    if (!std::isalpha((unsigned char)*m_pos) && *m_pos != '_') {
        setError("expected identifier");
        return false;
    }
    while (std::isalnum((unsigned char)*m_pos) || *m_pos == '_') m_pos++;
    out->assign(start, m_pos);
    return true;
}
