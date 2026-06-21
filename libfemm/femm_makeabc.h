// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// femm_makeabc.h — Improvised Asymptotic Boundary Conditions.
//
// makeABC creates IABC: a series of nested annular layers whose material
// properties step down so that the outermost boundary mimics an open
// boundary at infinity. See the FEMM manual section on IABC.
//
// One overload per physics type. Each is directly callable from the GUI
// (Open Boundary toolbar button) and is also wrapped by an
// mi_/ei_/hi_/ci_makeABC handler in cmd_dispatch for the TCP IPC.

#ifndef FEMM_MAKEABC_H
#define FEMM_MAKEABC_H

#include <optional>

class MagDocument;
class ElecDocument;
class HeatDocument;
class CurrDocument;

// Any field left empty triggers physics-specific defaulting, computed
// from the document's bounding box and problem type (planar vs
// axisymmetric). Parameter names mirror the historical (enn, arr, ex,
// wye, bc) order used by the IPC-side mi_/ei_/hi_/ci_makeABC handlers.
struct MakeABCParams {
    std::optional<int>    n;       // 1..12; default 7
    std::optional<double> R;       // outer-domain radius
    std::optional<double> x;       // center x (always 0 for axi)
    std::optional<double> y;       // center y
    std::optional<int>    bctype;  // 0 = Dirichlet, 1 = Neumann
};

// Each throws std::runtime_error on failure (e.g. nLayers out of range
// after clamping is impossible, so the only failure path is geometry
// errors from underlying AddNode/AddSegment/AddArcSegment).
void mag_makeABC(MagDocument&  doc, const MakeABCParams& p);
void elec_makeABC(ElecDocument& doc, const MakeABCParams& p);
void heat_makeABC(HeatDocument& doc, const MakeABCParams& p);
void curr_makeABC(CurrDocument& doc, const MakeABCParams& p);

#endif // FEMM_MAKEABC_H
