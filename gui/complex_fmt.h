// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// complex_fmt.h — QString formatting for CComplex values in editable dialog
// fields (Circuit current, material Jsrc, boundary c0/c1, point props, ...).
#ifndef COMPLEX_FMT_H
#define COMPLEX_FMT_H

#include <QString>
#include "complex.h"
#include "complex_format.h"   // femmFmtComplex — the one shared display formatter

// Format a CComplex for an editable PyLineEdit field. Delegates to the shared
// display formatter so these fields read the same Python-style "x+1j" way as
// the post-processor readouts, instead of the OF "x+I*y" wire form. Uses the
// shortest-round-trip variant: readable (a typed "20+10j" shows as "20+10j",
// not %.17g's "20+10.000000000000002j"), yet lossless -- the field text is
// re-parsed by the Python evaluator on OK, so it must survive the round-trip.
inline QString toQString(CComplex c) { return femmFmtComplexExact(c); }

#endif // COMPLEX_FMT_H
