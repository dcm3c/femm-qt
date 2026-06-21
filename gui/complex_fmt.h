// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// complex_fmt.h — QString formatting for CComplex values (GUI side only)
#ifndef COMPLEX_FMT_H
#define COMPLEX_FMT_H

#include <QString>
#include "complex.h"

// Format a CComplex for display in a PyLineEdit.
// Takes by value to work around CComplex::ToString() not being const.
inline QString toQString(CComplex c) { char buf[128]; return c.ToString(buf); }

#endif // COMPLEX_FMT_H
