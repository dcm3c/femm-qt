// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// coord_display.h — cartesian/polar coordinate display and entry
//
// Document::Coords (and the matching member on every *ResultsDoc) selects how
// coordinates are *shown to and typed by* the user: 0 = cartesian, 1 = polar.
// It is not a change of solution geometry — the model stores (x, y) throughout,
// the mesh and the physics are untouched. The one place the flag reaches the
// solver is a Prescribed A boundary condition, which is evaluated as
// A0 + r*A1 + theta*A2 instead of A0 + x*A1 + y*A2.
//
// Formats match original FEMM (femmeView.cpp OnMouseMove / EnterPoint) so a
// model authored in either program reads the same.
#ifndef COORD_DISPLAY_H
#define COORD_DISPLAY_H

#include <QCoreApplication>
#include <QString>
#include <cmath>

namespace coorddisp {

constexpr double kPi = 3.141592653589793238462643383279;

// Status-bar readout. problemType 1 is axisymmetric, whose cartesian pair is
// labelled (r, z); polar overrides that labelling entirely.
inline QString statusText(int coords, int problemType, double x, double y)
{
    if (coords == 1)
        return QString("(%1 at %2 deg)")
            .arg(std::sqrt(x * x + y * y), 0, 'f', 4)
            .arg(std::atan2(y, x) * 180.0 / kPi, 0, 'f', 4);
    if (problemType == 1)
        return QString("(r=%1, z=%2)").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4);
    return QString("(x=%1, y=%2)").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4);
}

// Field labels for the <TAB> numeric point-entry dialog.
inline void entryLabels(int coords, int problemType, QString &l1, QString &l2)
{
    if (coords == 1) {
        l1 = QCoreApplication::translate("coorddisp", "Radius:");
        l2 = QCoreApplication::translate("coorddisp", "Degrees:");
    } else if (problemType == 1) {
        l1 = QCoreApplication::translate("coorddisp", "R:");
        l2 = QCoreApplication::translate("coorddisp", "Z:");
    } else {
        l1 = QCoreApplication::translate("coorddisp", "X:");
        l2 = QCoreApplication::translate("coorddisp", "Y:");
    }
}

// Model (x, y) -> the pair the user sees in the entry dialog.
inline void toDisplay(int coords, double x, double y, double &c1, double &c2)
{
    if (coords == 1) {
        c1 = std::sqrt(x * x + y * y);
        c2 = std::atan2(y, x) * 180.0 / kPi;
    } else {
        c1 = x;
        c2 = y;
    }
}

// The pair the user typed -> model (x, y).
inline void fromDisplay(int coords, double c1, double c2, double &x, double &y)
{
    if (coords == 1) {
        const double t = c2 * kPi / 180.0;
        x = c1 * std::cos(t);
        y = c1 * std::sin(t);
    } else {
        x = c1;
        y = c2;
    }
}

} // namespace coorddisp

#endif // COORD_DISPLAY_H
