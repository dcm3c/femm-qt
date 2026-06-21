// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// mag_dir_eval.h — Evaluate a MagDirFctn formula string at one element
// centroid.
//
// Available identifiers in the expression:
//   x, y        — 2D planar coords (== r, z in axisymmetric problems)
//   r, z        — axisymmetric coords (== x, y in planar problems)
//   theta       — arg(X)*180/PI, in degrees
//   R           — |X|
//   pi, Pi, PI  — 3.14159...
//   I, i, j     — imaginary unit
//   sin/cos/tan/asin/acos/atan/atan2/sinh/cosh/tanh/exp/log/sqrt/
//   abs/absq/arg/conj/re/im/pow/Complex/floor/ceil/round/min/max/
//   deg/rad/fmod   — same as FemmCalc's symbol table.

#ifndef FKN_MAG_DIR_EVAL_H
#define FKN_MAG_DIR_EVAL_H

#include "complex.h"

// Evaluate the formula `expr` with x, y, r, z, theta, R set from `X`.
// Returns the real part of the result (the magnetization-direction
// angle in degrees). On parse/eval failure, prints a MsgBox-equivalent
// error and exits the solver with code 7.
double mag_dir_eval(const char* expr, CComplex X);

#endif // FKN_MAG_DIR_EVAL_H
