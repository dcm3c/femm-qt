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

// Assemble the "x+yj" form from a per-component number formatter. One place
// owns the sign / zero-part logic; callers choose how each component number is
// rendered -- fixed significant digits for readouts, shortest round-trip for
// editable fields.
template <typename FmtFn>
inline QString femmFmtComplexWith(CComplex z, FmtFn fmt) {
    if (z.im == 0)  return fmt(z.re);
    if (z.re == 0)  return fmt(z.im) + "j";
    return z.im > 0 ? fmt(z.re) + "+" + fmt(z.im) + "j"
                    : fmt(z.re) + "-" + fmt(-z.im) + "j";
}

// Readout form: fixed significant digits, to keep solver noise off the screen.
inline QString femmFmtComplex(CComplex z, int prec = 8) {
    return femmFmtComplexWith(z, [prec](double v) {
        return QString::number(v, 'g', prec);
    });
}

// Editable-field form: the shortest decimal that reads back as the identical
// double (like Python's repr) -- readable ("0.1", "20+10j") yet lossless, so a
// value survives an open/OK round-trip through the Python evaluator exactly.
// Values that genuinely need 16-17 digits still get them.
inline QString femmFmtComplexExact(CComplex z) {
    return femmFmtComplexWith(z, [](double v) -> QString {
        for (int p = 15; p <= 17; ++p) {
            QString s = QString::number(v, 'g', p);
            if (s.toDouble() == v) return s;
        }
        return QString::number(v, 'g', 17);
    });
}

#endif // GUI_COMPLEX_FORMAT_H
