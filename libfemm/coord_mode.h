// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// coord_mode.h — the "cart"/"polar" argument shared by every *_setgrid binding
//
// Document::Coords (and the matching member on each *ResultsDoc) is
// 0 = cartesian, 1 = polar. The flag controls coordinate display and entry; the
// model always stores (x, y). It reaches the solver only through a Prescribed A
// boundary condition, which polar evaluates as A0 + r*A1 + theta*A2.
#ifndef COORD_MODE_H
#define COORD_MODE_H

#include <string>

// Returns 0 for cartesian, 1 for polar, -1 if the text is neither. OF accepts
// exactly "cart" and "polar" (case-insensitively); "cartesian" is allowed here
// because it is what the file header spells and users reach for.
inline int parseCoordMode(std::string s)
{
    for (auto &c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (s == "cart" || s == "cartesian") return 0;
    if (s == "polar") return 1;
    return -1;
}

// The error OF raises for anything else, reused verbatim so ported scripts see
// the message they expect.
#define COORD_MODE_ERR "Unknown option for set grid"

#endif // COORD_MODE_H
