// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// femm_makeabc.cpp — Improvised Asymptotic Boundary Conditions.
//
// Each per-physics function:
//   1. Applies defaults to any std::nullopt parameter.
//   2. Draws the two centerline boundary arcs (or one line + one arc
//      in axisymmetric problems) that delimit the interior domain.
//   3. For each layer k = 1..n: draws a nested arc, adds an "u<k>" /
//      "e<k>" material with the k-th permeability table value, places
//      a block label inside the new annulus and tags it with that material.
//   4. If bctype == 0 (Dirichlet), creates the "A=0" / "V=0" boundary
//      property and assigns it to the outer arc(s).
//
// IMPORTANT: the table-selection rule is reversed between magnetics
// and scalar physics — scalar problems' Dirichlet BC uses the
// Neumann-derived (u..1) permeability values and vice versa. This is
// a property of the scalar potential formulation, not a typo.

#include "femm_makeabc.h"
#include "femm_iabc.h"
#include "mag_document.h"
#include "elec_document.h"
#include "heat_document.h"
#include "curr_document.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr double PI_VAL = 3.141592653589793238462643383279;

struct BBox { double x0, x1, y0, y1; bool empty; };

template<class Doc>
BBox bbox(const Doc& d) {
    BBox b{0, 0, 0, 0, true};
    if (d.nodelist.empty()) return b;
    b.x0 = b.x1 = d.nodelist[0].x;
    b.y0 = b.y1 = d.nodelist[0].y;
    b.empty = false;
    for (auto& nd : d.nodelist) {
        if (nd.x < b.x0) b.x0 = nd.x;
        if (nd.x > b.x1) b.x1 = nd.x;
        if (nd.y < b.y0) b.y0 = nd.y;
        if (nd.y > b.y1) b.y1 = nd.y;
    }
    return b;
}

// Resolve all five MakeABCParams fields applying the documented defaults.
// Returns (n, R, x, y, bctype) plus the planar/axi flag for downstream use.
template<class Doc>
void resolve_defaults(const Doc& d, const MakeABCParams& p, int default_bctype_planar,
                      int& n, double& R, double& x, double& y, int& bctype, bool& planar)
{
    n = p.n.value_or(7);
    if (n < 1)  n = 1;
    if (n > 12) n = 12;

    planar = (d.ProblemType == 0);

    BBox b = bbox(d);

    if (planar) {
        if (p.R) R = *p.R;
        else     R = 0.75 * std::sqrt(
                       (b.x1 - b.x0) * (b.x1 - b.x0) +
                       (b.y1 - b.y0) * (b.y1 - b.y0));
        x = p.x.value_or((b.x0 + b.x1) / 2.0);
        y = p.y.value_or((b.y0 + b.y1) / 2.0);
        bctype = p.bctype.value_or(default_bctype_planar);
    } else {
        // Axisymmetric: center on z-axis. The defaults have an unusual
        // fallback chain (arr takes precedence over ex/wye when an
        // explicit R isn't given) preserved here for compatibility.
        x = 0;
        if (p.y) {
            y = *p.y;
            R = p.R.value_or(0.75 * std::sqrt(
                                 (b.x1 - b.x0) * (b.x1 - b.x0) +
                                 (b.y1 - b.y0) * (b.y1 - b.y0)));
        } else if (p.x) {
            // Historical: y = ex when wye is nil but ex was passed
            y = *p.x;
            R = p.R.value_or(1.5 * std::sqrt(
                                 b.x1 * b.x1 +
                                 (b.y1 - b.y0) * (b.y1 - b.y0) / 4.0));
        } else if (p.R) {
            y = (b.y0 + b.y1) / 2.0;
            R = *p.R;
        } else {
            y = (b.y0 + b.y1) / 2.0;
            R = 1.5 * std::sqrt(b.x1 * b.x1 +
                                (b.y1 - b.y0) * (b.y1 - b.y0) / 4.0);
        }
        // Default bctype for scalar physics in axi is 0 (Dirichlet); for
        // magnetics it is 0 in either orientation.
        bctype = p.bctype.value_or(default_bctype_planar == 0 ? 0 : 0);
    }
}

// Add a node and arc connecting (x1,y1) → (x2,y2). The arc sweeps
// `arclen` degrees and is approximated by segments of at most `maxseg`
// degrees. Same shape as mi_drawarc / ei_drawarc / etc.
template<class Doc>
void draw_arc(Doc& d, double x1, double y1, double x2, double y2,
              double arclen, double maxseg)
{
    d.AddNode(x1, y1, d.defaultSnapDistance());
    d.AddNode(x2, y2, d.defaultSnapDistance());
    int n0 = d.ClosestNode(x1, y1);
    int n1 = d.ClosestNode(x2, y2);
    if (n0 < 0 || n1 < 0 || n0 == n1) return;
    FArcSegment arc;
    arc.n0 = n0;
    arc.n1 = n1;
    arc.ArcLength = arclen;
    arc.MaxSideLength = maxseg;
    d.AddArcSegment(arc);
}

template<class Doc>
void draw_line(Doc& d, double x1, double y1, double x2, double y2)
{
    d.AddNode(x1, y1, d.defaultSnapDistance());
    d.AddNode(x2, y2, d.defaultSnapDistance());
    int n0 = d.ClosestNode(x1, y1);
    int n1 = d.ClosestNode(x2, y2);
    if (n0 >= 0 && n1 >= 0 && n0 != n1) d.AddSegment(n0, n1);
}

// Place a block label at (x,y) with material `matname` selected.
// Mirrors mi_addblocklabel + mi_selectlabel + mi_setblockprop + mi_clearselected.
template<class Doc>
void place_labeled_block(Doc& d, double x, double y, const std::string& matname)
{
    d.AddBlockLabel(x, y, d.defaultSnapDistance());
    int k = d.ClosestBlockLabel(x, y);
    if (k < 0) return;
    auto& b = d.blocklist[k];
    b.BlockType = matname;
    b.MaxArea = 0;  // automesh
    b.InGroup = 0;
}

// Tag the outermost arc(s) with the Dirichlet boundary marker name.
// In planar, two arcs (left and right). In axisymmetric, only the right arc.
template<class Doc>
void tag_outer_arcs(Doc& d, double R, double cx, double cy, bool planar,
                    const std::string& bcname)
{
    int kr = d.ClosestArcSegment(1.1 * R + cx, cy);
    if (kr >= 0) {
        d.arclist[kr].BoundaryMarker = bcname;
        d.arclist[kr].MaxSideLength = 1;
    }
    if (planar) {
        int kl = d.ClosestArcSegment(-1.1 * R + cx, cy);
        if (kl >= 0 && kl != kr) {
            d.arclist[kl].BoundaryMarker = bcname;
            d.arclist[kl].MaxSideLength = 1;
        }
    }
}

// Block-label z-offset for layer k: z = r * exp(i * pi * k / (2*(n+1))).
inline void layer_label_offset(int n, int k, double r, double& dx, double& dy) {
    double theta = PI_VAL * (double)k / (2.0 * (double)(n + 1));
    dx = r * std::cos(theta);
    dy = r * std::sin(theta);
}

inline std::string mat_name(char prefix, int k) {
    char b[16];
    std::snprintf(b, sizeof(b), "%c%d", prefix, k);
    return b;
}

} // namespace

// ----------------------------------------------------------------------
// Magnetics — uses u2D0/uAx0 for Dirichlet, u2D1/uAx1 for Neumann.
// Boundary marker "A=0". Default bctype 0 in either orientation.
// ----------------------------------------------------------------------

void mag_makeABC(MagDocument& doc, const MakeABCParams& p)
{
    int n; double R, x, y; int bctype; bool planar;
    resolve_defaults(doc, p, /*default_bctype_planar=*/0, n, R, x, y, bctype, planar);

    if (planar) draw_arc(doc, x, y + R, x, y - R, 180, 1);
    else        draw_line(doc, 0, y - 1.1 * R, 0, y + 1.1 * R);
    draw_arc(doc, x, y - R, x, y + R, 180, 1);

    double d = 0.1 * R / (2.0 * n);

    for (int k = 1; k <= n; k++) {
        double r = R * (1.0 + (2.0 * k - 1.0) / (20.0 * n));
        draw_arc(doc, x, y - r - d, x, y + r + d, 180, 1);

        std::string name = mat_name('u', k);
        double dx, dy;
        layer_label_offset(n, k, r, dx, dy);
        place_labeled_block(doc, x + dx, y + dy, name);

        FMaterialProp m;
        m.BlockName = name;
        double mu = planar
            ? (bctype == 0 ? femm_iabc::u2D0[n-1][k-1] : femm_iabc::u2D1[n-1][k-1])
            : (bctype == 0 ? femm_iabc::uAx0[n-1][k-1] : femm_iabc::uAx1[n-1][k-1]);
        m.mu_x = mu;
        m.mu_y = mu;
        doc.blockproplist.push_back(m);

        if (planar)
            draw_arc(doc, x, y + r + d, x, y - r - d, 180, 1);
    }

    if (bctype == 0) {
        FBoundaryProp b;
        b.BdryName = "A=0";
        doc.lineproplist.push_back(b);
        tag_outer_arcs(doc, R, x, y, planar, std::string("A=0"));
    }
}

// ----------------------------------------------------------------------
// Electrostatics — tables swap by bctype (Dirichlet ↔ Neumann).
// Material "e<k>" with ex=ey=table[n][k]. Default bctype: planar 1, axi 0.
// Boundary marker "V=0". ei_addmaterial: (name, ex, ey, qv).
// ei_addboundprop: (name, V, qs, c0, c1, BdryFormat).
// ----------------------------------------------------------------------

void elec_makeABC(ElecDocument& doc, const MakeABCParams& p)
{
    int n; double R, x, y; int bctype; bool planar;
    resolve_defaults(doc, p, /*default_bctype_planar=*/1, n, R, x, y, bctype, planar);

    if (planar) draw_arc(doc, x, y + R, x, y - R, 180, 1);
    else        draw_line(doc, 0, y - 1.1 * R, 0, y + 1.1 * R);
    draw_arc(doc, x, y - R, x, y + R, 180, 1);

    double d = 0.1 * R / (2.0 * n);

    for (int k = 1; k <= n; k++) {
        double r = R * (1.0 + (2.0 * k - 1.0) / (20.0 * n));
        draw_arc(doc, x, y - r - d, x, y + r + d, 180, 1);

        std::string name = mat_name('e', k);
        double dx, dy;
        layer_label_offset(n, k, r, dx, dy);
        place_labeled_block(doc, x + dx, y + dy, name);

        EMaterialProp m;
        m.BlockName = name;
        // Swap: scalar physics use "_1" table for Dirichlet, "_0" for Neumann.
        double e = planar
            ? (bctype == 0 ? femm_iabc::u2D1[n-1][k-1] : femm_iabc::u2D0[n-1][k-1])
            : (bctype == 0 ? femm_iabc::uAx1[n-1][k-1] : femm_iabc::uAx0[n-1][k-1]);
        m.ex = e;
        m.ey = e;
        doc.blockproplist.push_back(m);

        if (planar)
            draw_arc(doc, x, y + r + d, x, y - r - d, 180, 1);
    }

    if (bctype == 0) {
        EBoundaryProp b;
        b.BdryName = "V=0";
        doc.lineproplist.push_back(b);
        tag_outer_arcs(doc, R, x, y, planar, std::string("V=0"));
    }
}

// ----------------------------------------------------------------------
// Heat flow — same table-swap as electrostatics.
// Material "e<k>" with Kx=Ky=table[n][k]. Default bctype: planar 1, axi 0.
// Boundary marker "V=0" (kept that name for historical reasons).
// hi_addboundprop:
// (name, BdryFormat=0, Tset=0, qs=0, ...). Format 0 = prescribed T.
// ----------------------------------------------------------------------

void heat_makeABC(HeatDocument& doc, const MakeABCParams& p)
{
    int n; double R, x, y; int bctype; bool planar;
    resolve_defaults(doc, p, /*default_bctype_planar=*/1, n, R, x, y, bctype, planar);

    if (planar) draw_arc(doc, x, y + R, x, y - R, 180, 1);
    else        draw_line(doc, 0, y - 1.1 * R, 0, y + 1.1 * R);
    draw_arc(doc, x, y - R, x, y + R, 180, 1);

    double d = 0.1 * R / (2.0 * n);

    for (int k = 1; k <= n; k++) {
        double r = R * (1.0 + (2.0 * k - 1.0) / (20.0 * n));
        draw_arc(doc, x, y - r - d, x, y + r + d, 180, 1);

        std::string name = mat_name('e', k);
        double dx, dy;
        layer_label_offset(n, k, r, dx, dy);
        place_labeled_block(doc, x + dx, y + dy, name);

        HMaterialProp m;
        m.BlockName = name;
        double K = planar
            ? (bctype == 0 ? femm_iabc::u2D1[n-1][k-1] : femm_iabc::u2D0[n-1][k-1])
            : (bctype == 0 ? femm_iabc::uAx1[n-1][k-1] : femm_iabc::uAx0[n-1][k-1]);
        m.Kx = K;
        m.Ky = K;
        doc.blockproplist.push_back(m);

        if (planar)
            draw_arc(doc, x, y + r + d, x, y - r - d, 180, 1);
    }

    if (bctype == 0) {
        HBoundaryProp b;
        b.BdryName = "V=0";
        // BdryFormat=0 (prescribed T) with Tset=0 == "V=0" (i.e. T=0).
        doc.lineproplist.push_back(b);
        tag_outer_arcs(doc, R, x, y, planar, std::string("V=0"));
    }
}

// ----------------------------------------------------------------------
// Current flow — same table-swap as electrostatics.
// Material "e<k>": ci_addmaterial(name, ox=0, oy=0, ex=table, ey=ex).
// Default bctype: planar 1, axi 0. Boundary marker "V=0".
// ci_addboundprop: (name, vs, qs, c0, c1, BdryFormat) — all complex/zero.
// ----------------------------------------------------------------------

void curr_makeABC(CurrDocument& doc, const MakeABCParams& p)
{
    int n; double R, x, y; int bctype; bool planar;
    resolve_defaults(doc, p, /*default_bctype_planar=*/1, n, R, x, y, bctype, planar);

    if (planar) draw_arc(doc, x, y + R, x, y - R, 180, 1);
    else        draw_line(doc, 0, y - 1.1 * R, 0, y + 1.1 * R);
    draw_arc(doc, x, y - R, x, y + R, 180, 1);

    double d = 0.1 * R / (2.0 * n);

    for (int k = 1; k <= n; k++) {
        double r = R * (1.0 + (2.0 * k - 1.0) / (20.0 * n));
        draw_arc(doc, x, y - r - d, x, y + r + d, 180, 1);

        std::string name = mat_name('e', k);
        double dx, dy;
        layer_label_offset(n, k, r, dx, dy);
        place_labeled_block(doc, x + dx, y + dy, name);

        CurrMaterialProp m;
        m.BlockName = name;
        m.ox = 0;
        m.oy = 0;
        double e = planar
            ? (bctype == 0 ? femm_iabc::u2D1[n-1][k-1] : femm_iabc::u2D0[n-1][k-1])
            : (bctype == 0 ? femm_iabc::uAx1[n-1][k-1] : femm_iabc::uAx0[n-1][k-1]);
        m.ex = e;
        m.ey = e;
        doc.blockproplist.push_back(m);

        if (planar)
            draw_arc(doc, x, y + r + d, x, y - r - d, 180, 1);
    }

    if (bctype == 0) {
        CurrBoundaryProp b;
        b.BdryName = "V=0";
        doc.lineproplist.push_back(b);
        tag_outer_arcs(doc, R, x, y, planar, std::string("V=0"));
    }
}
