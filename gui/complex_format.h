// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// complex_format.h - shared human-display formatting for complex values shown
// in the GUI postprocessor dialogs and point-value readouts (Circuit
// Properties, line/block integrals, point values, etc.).
//
// Uses native-Python "j" notation to match the script-side console/print
// display: e.g. "1.5+2j", a real-valued complex (im==0) collapsed to a bare
// real, and a pure imaginary as "2j". `prec` is the number of significant
// digits (Qt 'g' format).
//
// NB: this is display only. The TCP IPC wire format (CComplex::ToString /
// format_complex in py_state.cpp, "1.5+I*2") is a separate, unchanged contract.
#ifndef GUI_COMPLEX_FORMAT_H
#define GUI_COMPLEX_FORMAT_H

#include <QString>
#include "complex.h"

inline QString femmFmtComplex(CComplex z, int prec = 8) {
    if (z.im == 0)  return QString("%1").arg(z.re, 0, 'g', prec);
    if (z.re == 0)  return QString("%1j").arg(z.im, 0, 'g', prec);
    return z.im > 0 ? QString("%1+%2j").arg(z.re, 0, 'g', prec).arg(z.im, 0, 'g', prec)
                    : QString("%1-%2j").arg(z.re, 0, 'g', prec).arg(-z.im, 0, 'g', prec);
}

#endif // GUI_COMPLEX_FORMAT_H
