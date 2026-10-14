// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// document.cpp — Base document geometry operations
// Ported from femm42src/femm/FemmeDoc.cpp (geometry portions only)

#include "document.h"
#include <algorithm>
#include <cmath>

// --- Snap distance ---

double DocumentBase::defaultSnapDistance() const
{
    if (nodelist.size() < 2) return 1.e-08;
    CComplex p0 = nodelist[0].CC();
    CComplex p1 = p0;
    for (size_t i = 1; i < nodelist.size(); i++) {
        CComplex p2 = nodelist[i].CC();
        if (p2.re < p0.re) p0.re = p2.re;
        if (p2.re > p1.re) p1.re = p2.re;
        if (p2.im < p0.im) p0.im = p2.im;
        if (p2.im > p1.im) p1.im = p2.im;
    }
    return abs(p1 - p0) * CLOSE_ENOUGH;
}

double DocumentBase::lengthScale() const
{
    switch (LengthUnits) {
        case 0: return 0.0254;        // inches
        case 1: return 0.001;         // mm
        case 2: return 0.01;          // cm
        case 3: return 1.0;           // meters
        case 4: return 2.54e-05;      // mils
        case 5: return 1.e-06;        // microns
        default: return 0.001;
    }
}

bool DocumentBase::getBoundingBox(double &xmin, double &ymin, double &xmax, double &ymax,
                                  bool selectedOnly) const
{
    if (!selectedOnly && nodelist.empty()) return false;

    bool any = false;
    auto include = [&](double px, double py) {
        if (!any) {
            any = true;
            xmin = xmax = px;
            ymin = ymax = py;
            return;
        }
        if (px < xmin) xmin = px;
        if (px > xmax) xmax = px;
        if (py < ymin) ymin = py;
        if (py > ymax) ymax = py;
    };

    // Nodes. Restricted to the selection, a node also counts when it is an
    // endpoint of a selected segment — the segment's extent is defined by its
    // endpoints even though those nodes carry no selection flag of their own.
    // (Selected arcs contribute their endpoints via the sampling loop below.)
    std::vector<bool> nodeWanted;
    if (selectedOnly) {
        nodeWanted.assign(nodelist.size(), false);
        for (size_t i = 0; i < nodelist.size(); i++)
            if (nodelist[i].IsSelected) nodeWanted[i] = true;
        for (const auto &s : linelist) {
            if (!s.IsSelected) continue;
            if (s.n0 >= 0 && s.n0 < (int)nodelist.size()) nodeWanted[s.n0] = true;
            if (s.n1 >= 0 && s.n1 < (int)nodelist.size()) nodeWanted[s.n1] = true;
        }
    }
    for (size_t i = 0; i < nodelist.size(); i++) {
        if (selectedOnly && !nodeWanted[i]) continue;
        include(nodelist[i].x, nodelist[i].y);
    }

    // Block labels
    for (const auto &b : blocklist) {
        if (selectedOnly && !b.IsSelected) continue;
        include(b.x, b.y);
    }

    // Arc segments — traverse at discretization level to capture the full
    // extent of the arc's bulge. Use the same rotation approach as the editor's
    // drawArcSegments (and original FEMM): GetCircle places the center so that a
    // positive (CCW) rotation by ArcLength carries n0 onto n1, so we simply step
    // by a fixed +dt with no direction to guess.
    //
    // The earlier version reconstructed the sweep direction from arg()/atan2 and
    // a CCW-vs-CW comparison that was exactly degenerate for a 180-degree arc:
    // the swept angle and ArcLength both equal PI, so the test hinged on sub-ULP
    // rounding and flipped between an FMA build (macOS/arm64) and a non-FMA x86
    // build (Windows), sampling the wrong semicircle and corrupting the box. The
    // rotation form needs no such sign and is robust at 180 deg. (cf.
    // docs/arc-180-degree-fix.md)
    for (const auto &arc : arclist) {
        if (selectedOnly && !arc.IsSelected) continue;
        if (arc.n0 < 0 || arc.n0 >= (int)nodelist.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)nodelist.size()) continue;

        CComplex c;
        double R;
        GetCircle(arc, c, R);

        // Sample along the arc at MaxSideLength intervals
        double segAngle = arc.MaxSideLength;
        if (segAngle <= 0 || segAngle > arc.ArcLength) segAngle = arc.ArcLength;
        int nsteps = (int)ceil(arc.ArcLength / segAngle);
        if (nsteps < 1) nsteps = 1;

        double dt = arc.ArcLength * PI / (180.0 * nsteps);
        CComplex rot(cos(dt), sin(dt));
        CComplex pt = nodelist[arc.n0].CC();
        for (int k = 0; k <= nsteps; k++) {
            include(pt.re, pt.im);
            pt = (pt - c) * rot + c;   // advance CCW by dt
        }
    }

    return any;
}

// --- Add operations (PSLG-aware) ---
// Ported from femm42src/femm/FemmeDoc.cpp and MOVECOPY.CPP

bool DocumentBase::AddNode(double x, double y, double d)
{
    FNode pt;
    pt.x = x; pt.y = y;
    return AddNode(pt, d);
}

// Adds the node itself, properties and all, as OF's AddNode(CNode&) does
// (bd_movecopy.cpp:1308). EnforcePSLG rebuilds the node list through here
// after every move and copy; re-adding by coordinates alone reset the group,
// point property, conductor and mesh size of every node in the model.
bool DocumentBase::AddNode(FNode &node, double d)
{
    const double x = node.x, y = node.y;

    // test to see if "too close" to existing node
    for (size_t i = 0; i < nodelist.size(); i++)
        if (nodelist[i].GetDistance(x, y) < d) return false;

    // can't put a node on top of a block label
    for (size_t i = 0; i < blocklist.size(); i++)
        if (blocklist[i].GetDistance(x, y) < d) return false;

    nodelist.push_back(node);
    int newIdx = (int)nodelist.size() - 1;

    // if node lands on an existing segment, split into two
    int k = (int)linelist.size();
    for (int i = 0; i < k; i++) {
        double sx0 = std::min(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double sx1 = std::max(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double sy0 = std::min(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        double sy1 = std::max(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        if (x < sx0 - d || x > sx1 + d || y < sy0 - d || y > sy1 + d) continue;
        if (fabs(ShortestDistance(x, y, i)) < d) {
            FSegment segm = linelist[i];
            linelist[i].n1 = newIdx;
            segm.n0 = newIdx;
            linelist.push_back(segm);
        }
    }

    // if node lands on an existing arc, split into two
    k = (int)arclist.size();
    for (int i = 0; i < k; i++) {
        if (ShortestDistanceFromArc(CComplex(x, y), arclist[i]) < d) {
            CComplex a0(nodelist[arclist[i].n0].x, nodelist[arclist[i].n0].y);
            CComplex a1(nodelist[arclist[i].n1].x, nodelist[arclist[i].n1].y);
            CComplex a2(x, y);
            CComplex c; double R;
            GetCircle(arclist[i], c, R);
            FArcSegment asegm = arclist[i];
            arclist[i].n1 = newIdx;
            arclist[i].ArcLength = arg((a2 - c) / (a0 - c)) * 180.0 / PI;
            asegm.n0 = newIdx;
            asegm.ArcLength = arg((a1 - c) / (a2 - c)) * 180.0 / PI;
            arclist.push_back(asegm);
        }
    }
    return true;
}

bool DocumentBase::AddSegment(int n0, int n1, FSegment *parsegm, double tol)
{
    if (n0 == n1) return false;
    if (n0 < 0 || n1 < 0) return false;
    if (n0 >= (int)nodelist.size() || n1 >= (int)nodelist.size()) return false;

    // don't add if the line is already in the list
    for (size_t i = 0; i < linelist.size(); i++) {
        if ((linelist[i].n0 == n0 && linelist[i].n1 == n1) ||
            (linelist[i].n0 == n1 && linelist[i].n1 == n0))
            return false;
    }

    FSegment segm;
    segm.BoundaryMarker = "<None>";
    if (parsegm) segm = *parsegm;
    segm.IsSelected = false;
    segm.n0 = n0; segm.n1 = n1;

    // find intersections with existing segments (bounding box pre-filter)
    std::vector<CComplex> newnodes;
    double xi, yi;
    double ax0 = std::min(nodelist[n0].x, nodelist[n1].x);
    double ax1 = std::max(nodelist[n0].x, nodelist[n1].x);
    double ay0 = std::min(nodelist[n0].y, nodelist[n1].y);
    double ay1 = std::max(nodelist[n0].y, nodelist[n1].y);

    for (int i = 0; i < (int)linelist.size(); i++) {
        double bx0 = std::min(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double bx1 = std::max(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        if (ax1 < bx0 || ax0 > bx1) continue;
        double by0 = std::min(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        double by1 = std::max(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        if (ay1 < by0 || ay0 > by1) continue;
        if (GetIntersection(n0, n1, i, &xi, &yi))
            newnodes.push_back(CComplex(xi, yi));
    }

    // find intersections with existing arcs (bounding box pre-filter)
    CComplex p[2];
    for (int i = 0; i < (int)arclist.size(); i++) {
        // arc bounding box: use both endpoints and the circle center/radius
        CComplex ac; double aR;
        GetCircle(arclist[i], ac, aR);
        double bx0 = ac.re - aR, bx1 = ac.re + aR;
        double by0 = ac.im - aR, by1 = ac.im + aR;
        if (ax1 < bx0 || ax0 > bx1) continue;
        if (ay1 < by0 || ay0 > by1) continue;
        int j = GetLineArcIntersection(segm, arclist[i], p);
        for (int k = 0; k < j; k++) newnodes.push_back(p[k]);
    }

    // compute tolerance for adding intersection nodes
    double t;
    if (tol == 0) {
        if (nodelist.size() < 2) t = 1.e-08;
        else {
            CComplex bb0 = nodelist[0].CC(), bb1 = bb0;
            for (size_t i = 1; i < nodelist.size(); i++) {
                if (nodelist[i].x < bb0.re) bb0.re = nodelist[i].x;
                if (nodelist[i].x > bb1.re) bb1.re = nodelist[i].x;
                if (nodelist[i].y < bb0.im) bb0.im = nodelist[i].y;
                if (nodelist[i].y > bb1.im) bb1.im = nodelist[i].y;
            }
            t = abs(bb1 - bb0) * CLOSE_ENOUGH;
        }
    } else t = tol;

    // add nodes at intersection points
    for (auto &nn : newnodes)
        AddNode(nn.re, nn.im, t);

    // add the proposed segment
    linelist.push_back(segm);

    // check if the segment passes through existing nodes;
    // if so, split it recursively
    double dmin = (tol == 0) ? abs(nodelist[n1].CC() - nodelist[n0].CC()) * 1.e-05 : tol;
    int k = (int)linelist.size() - 1;
    // recompute bounds (nodes may have been added by intersection splitting)
    ax0 = std::min(nodelist[n0].x, nodelist[n1].x);
    ax1 = std::max(nodelist[n0].x, nodelist[n1].x);
    ay0 = std::min(nodelist[n0].y, nodelist[n1].y);
    ay1 = std::max(nodelist[n0].y, nodelist[n1].y);

    for (int i = 0; i < (int)nodelist.size(); i++) {
        if (i != n0 && i != n1) {
            if (nodelist[i].x < ax0 - dmin || nodelist[i].x > ax1 + dmin) continue;
            if (nodelist[i].y < ay0 - dmin || nodelist[i].y > ay1 + dmin) continue;
            double d = ShortestDistance(nodelist[i].x, nodelist[i].y, k);
            if (abs(nodelist[i].CC() - nodelist[n0].CC()) < dmin) d = 2.0 * dmin;
            if (abs(nodelist[i].CC() - nodelist[n1].CC()) < dmin) d = 2.0 * dmin;
            if (d < dmin) {
                // remove the segment we just added and recurse
                linelist[k].ToggleSelect();
                DeleteSelectedSegments();
                AddSegment(n0, i, parsegm, dmin);
                AddSegment(i, n1, parsegm, dmin);
                return true;
            }
        }
    }

    return true;
}

bool DocumentBase::AddSegment(CComplex p0, CComplex p1, FSegment &segm, double tol)
{
    int n0 = ClosestNode(p0.re, p0.im);
    int n1 = ClosestNode(p1.re, p1.im);
    return AddSegment(n0, n1, &segm, tol);
}

bool DocumentBase::AddArcSegment(FArcSegment &asegm, double tol)
{
    if (asegm.n0 == asegm.n1) return false;
    if (asegm.n0 < 0 || asegm.n1 < 0) return false;
    if (asegm.n0 >= (int)nodelist.size() || asegm.n1 >= (int)nodelist.size()) return false;

    // don't add if arc is already in the list
    for (size_t i = 0; i < arclist.size(); i++) {
        if (arclist[i].n0 == asegm.n0 && arclist[i].n1 == asegm.n1 &&
            fabs(arclist[i].ArcLength - asegm.ArcLength) < 1.e-02)
            return false;
    }

    asegm.IsSelected = false;

    // bounding box of proposed arc (use enclosing circle)
    CComplex ac; double aR;
    GetCircle(asegm, ac, aR);
    double ax0 = ac.re - aR, ax1 = ac.re + aR;
    double ay0 = ac.im - aR, ay1 = ac.im + aR;

    // find intersections with existing segments (bounding box pre-filter)
    std::vector<CComplex> newnodes;
    CComplex p[2];
    for (int i = 0; i < (int)linelist.size(); i++) {
        double bx0 = std::min(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double bx1 = std::max(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        if (ax1 < bx0 || ax0 > bx1) continue;
        double by0 = std::min(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        double by1 = std::max(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        if (ay1 < by0 || ay0 > by1) continue;
        int j = GetLineArcIntersection(linelist[i], asegm, p);
        for (int k = 0; k < j; k++) newnodes.push_back(p[k]);
    }
    // find intersections with existing arcs (bounding box pre-filter)
    for (int i = 0; i < (int)arclist.size(); i++) {
        CComplex bc; double bR;
        GetCircle(arclist[i], bc, bR);
        if (ax1 < bc.re - bR || ax0 > bc.re + bR) continue;
        if (ay1 < bc.im - bR || ay0 > bc.im + bR) continue;
        int j = GetArcArcIntersection(asegm, arclist[i], p);
        for (int k = 0; k < j; k++) newnodes.push_back(p[k]);
    }

    // compute tolerance
    double t;
    if (tol == 0) {
        if (nodelist.size() < 2) t = 1.e-08;
        else {
            CComplex bb0 = nodelist[0].CC(), bb1 = bb0;
            for (size_t i = 1; i < nodelist.size(); i++) {
                if (nodelist[i].x < bb0.re) bb0.re = nodelist[i].x;
                if (nodelist[i].x > bb1.re) bb1.re = nodelist[i].x;
                if (nodelist[i].y < bb0.im) bb0.im = nodelist[i].y;
                if (nodelist[i].y > bb1.im) bb1.im = nodelist[i].y;
            }
            t = abs(bb1 - bb0) * CLOSE_ENOUGH;
        }
    } else t = tol;

    for (auto &nn : newnodes)
        AddNode(nn.re, nn.im, t);

    // add proposed arc
    arclist.push_back(asegm);

    // check if the arc passes through existing nodes; if so, split
    CComplex c; double R;
    GetCircle(asegm, c, R);
    double dmin = (tol == 0) ? fabs(R * PI * asegm.ArcLength / 180.0) * 1.e-05 : tol;
    int k = (int)arclist.size() - 1;

    for (int i = 0; i < (int)nodelist.size(); i++) {
        if (i != asegm.n0 && i != asegm.n1) {
            double d = ShortestDistanceFromArc(CComplex(nodelist[i].x, nodelist[i].y), arclist[k]);
            if (d < dmin) {
                CComplex a0(nodelist[asegm.n0].x, nodelist[asegm.n0].y);
                CComplex a1(nodelist[asegm.n1].x, nodelist[asegm.n1].y);
                CComplex a2(nodelist[i].x, nodelist[i].y);
                arclist[k].ToggleSelect();
                DeleteSelectedArcSegments();

                FArcSegment newarc = asegm;
                newarc.n0 = asegm.n0; newarc.n1 = i;
                newarc.ArcLength = arg((a2 - c) / (a0 - c)) * 180.0 / PI;
                AddArcSegment(newarc, dmin);

                newarc = asegm;
                newarc.n0 = i; newarc.n1 = asegm.n1;
                newarc.ArcLength = arg((a1 - c) / (a2 - c)) * 180.0 / PI;
                AddArcSegment(newarc, dmin);
                return true;
            }
        }
    }

    return true;
}

bool DocumentBase::AddArcSegment(CComplex p0, CComplex p1, FArcSegment &asegm, double tol)
{
    asegm.n0 = ClosestNode(p0.re, p0.im);
    asegm.n1 = ClosestNode(p1.re, p1.im);
    return AddArcSegment(asegm, tol);
}

bool DocumentBase::AddBlockLabel(double x, double y, double d)
{
    // can't place on top of existing block label
    for (size_t i = 0; i < blocklist.size(); i++)
        if (blocklist[i].GetDistance(x, y) < d) return false;

    // can't place on top of a node
    for (size_t i = 0; i < nodelist.size(); i++)
        if (nodelist[i].GetDistance(x, y) < d) return false;

    // can't place on a line segment
    for (int i = 0; i < (int)linelist.size(); i++) {
        double sx0 = std::min(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double sx1 = std::max(nodelist[linelist[i].n0].x, nodelist[linelist[i].n1].x);
        double sy0 = std::min(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        double sy1 = std::max(nodelist[linelist[i].n0].y, nodelist[linelist[i].n1].y);
        if (x < sx0 - d || x > sx1 + d || y < sy0 - d || y > sy1 + d) continue;
        if (ShortestDistance(x, y, i) < d) return false;
    }

    FBlockLabel blk;
    blk.x = x; blk.y = y;
    blocklist.push_back(blk);
    return true;
}

bool DocumentBase::AddBlockLabel(FBlockLabel &blabel, double d)
{
    double x = blabel.x, y = blabel.y;

    for (size_t i = 0; i < blocklist.size(); i++)
        if (blocklist[i].GetDistance(x, y) < d) return false;
    for (size_t i = 0; i < nodelist.size(); i++)
        if (nodelist[i].GetDistance(x, y) < d) return false;
    for (int i = 0; i < (int)linelist.size(); i++)
        if (ShortestDistance(x, y, i) < d) return false;

    blocklist.push_back(blabel);
    return true;
}

// --- Intersection detection ---
// Ported from femm42src/femm/FemmeDoc.cpp

bool DocumentBase::GetIntersection(int n0, int n1, int segm, double *xi, double *yi) const
{
    // Check for common endpoint
    if (n0 == linelist[segm].n0 || n0 == linelist[segm].n1) return false;
    if (n1 == linelist[segm].n0 || n1 == linelist[segm].n1) return false;

    CComplex p0 = nodelist[linelist[segm].n0].CC();
    CComplex p1 = nodelist[linelist[segm].n1].CC();
    CComplex q0 = nodelist[n0].CC();
    CComplex q1 = nodelist[n1].CC();
    double ee = std::min(abs(p1 - p0), abs(q1 - q0)) * 1.0e-8;

    // Rotate and scale the prospective line
    q0 = (q0 - p0) / (p1 - p0);
    q1 = (q1 - p0) / (p1 - p0);

    // Quick rejection
    if (Re(q0) <= 0 && Re(q1) <= 0) return false;
    if (Re(q0) >= 1 && Re(q1) >= 1) return false;
    if (Im(q0) <= 0 && Im(q1) <= 0) return false;
    if (Im(q0) >= 0 && Im(q1) >= 0) return false;

    double z = Im(q0) / Im(q0 - q1);
    double x = Re((1.0 - z) * q0 + z * q1);
    if (x < ee || x > (1.0 - ee)) return false;

    CComplex result = (1.0 - z) * nodelist[n0].CC() + z * nodelist[n1].CC();
    *xi = Re(result);
    *yi = Im(result);
    return true;
}

int DocumentBase::GetLineArcIntersection(FSegment &seg, FArcSegment &arc, CComplex *p) const
{
    CComplex p0(nodelist[seg.n0].x, nodelist[seg.n0].y);
    CComplex p1(nodelist[seg.n1].x, nodelist[seg.n1].y);
    CComplex a0(nodelist[arc.n0].x, nodelist[arc.n0].y);
    CComplex a1(nodelist[arc.n1].x, nodelist[arc.n1].y);
    double d = abs(a1 - a0);

    CComplex t = (a1 - a0) / d;
    double tta = arc.ArcLength * PI / 180.0;
    double R = d / (2.0 * sin(tta / 2.0));
    // Clamp at zero: for a 180-degree arc the discriminant is exactly 0, and FMA
    // rounding can make it slightly negative → sqrt NaN → arc fails to draw.
    CComplex c = a0 + (d / 2.0 + CComplex(0, 1) * sqrt(std::max(0.0, R * R - d * d / 4.0))) * t;

    d = abs(p1 - p0);
    t = (p1 - p0) / d;
    CComplex v = (c - p0) / t;
    if (fabs(Im(v)) > R) return 0;
    double l = sqrt(R * R - Im(v) * Im(v));

    int i = 0;

    if ((l / R) < 1.e-05) {
        p[i] = p0 + Re(v) * t;
        double Rcheck = Re((p[i] - p0) / t);
        double z = arg((p[i] - c) / (a0 - c));
        if (Rcheck > 0 && Rcheck < d && z > 0 && z < tta) i++;
        return i;
    }

    p[i] = p0 + (Re(v) + l) * t;
    double Rcheck = Re((p[i] - p0) / t);
    double z = arg((p[i] - c) / (a0 - c));
    if (Rcheck > 0 && Rcheck < d && z > 0 && z < tta) i++;

    p[i] = p0 + (Re(v) - l) * t;
    Rcheck = Re((p[i] - p0) / t);
    z = arg((p[i] - c) / (a0 - c));
    if (Rcheck > 0 && Rcheck < d && z > 0 && z < tta) i++;

    return i;
}

int DocumentBase::GetArcArcIntersection(FArcSegment &arc0, FArcSegment &arc1, CComplex *p) const
{
    CComplex c0, c1;
    double R0, R1;
    GetCircle(arc0, c0, R0);
    GetCircle(arc1, c1, R1);

    double d = abs(c1 - c0);
    if (d > R0 + R1 || d < 1.e-08) return 0;

    double l = sqrt((R0 + R1 - d) * (d + R0 - R1) * (d - R0 + R1) * (d + R0 + R1)) / (2.0 * d);
    double cv = 1.0 + (R0 / d) * (R0 / d) - (R1 / d) * (R1 / d);
    CComplex t = (c1 - c0) / d;
    double tta0 = arc0.ArcLength * PI / 180.0;
    double tta1 = arc1.ArcLength * PI / 180.0;
    CComplex a0(nodelist[arc0.n0].x, nodelist[arc0.n0].y);
    CComplex a1(nodelist[arc1.n0].x, nodelist[arc1.n0].y);

    int i = 0;

    p[i] = c0 + (cv * d / 2.0 + CComplex(0, 1) * l) * t;
    double z0 = arg((p[i] - c0) / (a0 - c0));
    double z1 = arg((p[i] - c1) / (a1 - c1));
    if (z0 > 0 && z0 < tta0 && z1 > 0 && z1 < tta1) i++;

    if (fabs(d - R0 + R1) / (R0 + R1) < 1.e-05) {
        p[i] = c0 + cv * d * t / 2.0;
        return i;
    }

    p[i] = c0 + (cv * d / 2.0 - CComplex(0, 1) * l) * t;
    z0 = arg((p[i] - c0) / (a0 - c0));
    z1 = arg((p[i] - c1) / (a1 - c1));
    if (z0 > 0 && z0 < tta0 && z1 > 0 && z1 < tta1) i++;

    return i;
}

// --- EnforcePSLG ---
// Ported from femm42src/femm/MOVECOPY.CPP
// Rebuilds all geometry lists to resolve intersections and duplicates.

void DocumentBase::EnforcePSLG(double tol)
{
    std::vector<FNode>       oldnodes = nodelist;
    std::vector<FSegment>    oldlines = linelist;
    std::vector<FArcSegment> oldarcs  = arclist;
    std::vector<FBlockLabel> oldblks  = blocklist;

    nodelist.clear();
    linelist.clear();
    arclist.clear();
    blocklist.clear();

    // compute tolerance from bounding box if not specified
    double d;
    if (tol == 0) {
        if (oldnodes.size() < 2) d = 1.e-08;
        else {
            CComplex p0 = oldnodes[0].CC(), p1 = p0;
            for (size_t i = 1; i < oldnodes.size(); i++) {
                if (oldnodes[i].x < p0.re) p0.re = oldnodes[i].x;
                if (oldnodes[i].x > p1.re) p1.re = oldnodes[i].x;
                if (oldnodes[i].y < p0.im) p0.im = oldnodes[i].y;
                if (oldnodes[i].y > p1.im) p1.im = oldnodes[i].y;
            }
            d = abs(p1 - p0) * CLOSE_ENOUGH;
        }
    } else d = tol;

    // re-add all nodes
    for (auto &n : oldnodes) AddNode(n, d);

    // re-add all segments (using coordinates to find the correct new node indices)
    for (auto &s : oldlines) {
        CComplex p0(oldnodes[s.n0].x, oldnodes[s.n0].y);
        CComplex p1(oldnodes[s.n1].x, oldnodes[s.n1].y);
        AddSegment(p0, p1, s, d);
    }

    // re-add all arcs
    for (auto &a : oldarcs) {
        CComplex p0(oldnodes[a.n0].x, oldnodes[a.n0].y);
        CComplex p1(oldnodes[a.n1].x, oldnodes[a.n1].y);
        AddArcSegment(p0, p1, a, d);
    }

    // re-add all block labels
    for (auto &b : oldblks) AddBlockLabel(b, d);

    UnselectAll();
}

// --- Closest element queries ---

int DocumentBase::ClosestNode(double x, double y) const
{
    if (nodelist.empty()) return -1;
    int best = 0;
    double bestDist = nodelist[0].GetDistance(x, y);
    for (size_t i = 1; i < nodelist.size(); i++) {
        double d = nodelist[i].GetDistance(x, y);
        if (d < bestDist) { bestDist = d; best = (int)i; }
    }
    return best;
}

int DocumentBase::ClosestBlockLabel(double x, double y) const
{
    if (blocklist.empty()) return -1;
    int best = 0;
    double bestDist = blocklist[0].GetDistance(x, y);
    for (size_t i = 1; i < blocklist.size(); i++) {
        double d = blocklist[i].GetDistance(x, y);
        if (d < bestDist) { bestDist = d; best = (int)i; }
    }
    return best;
}

double DocumentBase::ShortestDistance(double p, double q, int segm) const
{
    double x0 = nodelist[linelist[segm].n0].x;
    double y0 = nodelist[linelist[segm].n0].y;
    double x1 = nodelist[linelist[segm].n1].x;
    double y1 = nodelist[linelist[segm].n1].y;

    double dx = x1 - x0;
    double dy = y1 - y0;
    double len2 = dx * dx + dy * dy;
    if (len2 == 0) return std::sqrt((p - x0) * (p - x0) + (q - y0) * (q - y0));

    double t = ((p - x0) * dx + (q - y0) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));

    double cx = x0 + t * dx;
    double cy = y0 + t * dy;
    return std::sqrt((p - cx) * (p - cx) + (q - cy) * (q - cy));
}

int DocumentBase::ClosestSegment(double x, double y) const
{
    if (linelist.empty()) return -1;
    int best = 0;
    double bestDist = ShortestDistance(x, y, 0);
    for (size_t i = 1; i < linelist.size(); i++) {
        double d = ShortestDistance(x, y, (int)i);
        if (d < bestDist) { bestDist = d; best = (int)i; }
    }
    return best;
}

void DocumentBase::GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const
{
    // Ported from original FEMM FemmeDoc.cpp
    CComplex a0 = nodelist[asegm.n0].CC();
    CComplex a1 = nodelist[asegm.n1].CC();
    double d = abs(a1 - a0);
    if (d == 0) { c = a0; R = 0; return; }

    CComplex t = (a1 - a0) / d;
    double tta = asegm.ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    // Clamp at zero: for a 180-degree arc the discriminant is exactly 0, and FMA
    // rounding can make it slightly negative → sqrt NaN → arc fails to draw.
    c = a0 + (d / 2.0 + CComplex(0, 1) * sqrt(std::max(0.0, R * R - d * d / 4.0))) * t;
}

double DocumentBase::ShortestDistanceFromArc(CComplex p, const FArcSegment &arc) const
{
    // Ported from original FEMM FemmeDoc.cpp
    CComplex a0(nodelist[arc.n0].x, nodelist[arc.n0].y);
    CComplex a1(nodelist[arc.n1].x, nodelist[arc.n1].y);
    CComplex c;
    double R;
    GetCircle(arc, c, R);
    double d = abs(p - c);

    if (d == 0) return R;

    CComplex t = (p - c) / d;
    double l = abs(p - c - R * t);
    double z = arg(t / (a0 - c)) * 180.0 / PI;
    if (z > 0 && z < arc.ArcLength) return l;

    // point projection is outside the arc span — return distance to nearest endpoint
    double d0 = abs(p - a0);
    double d1 = abs(p - a1);
    return (d0 < d1) ? d0 : d1;
}

int DocumentBase::ClosestArcSegment(double x, double y) const
{
    if (arclist.empty()) return -1;
    CComplex p(x, y);
    int best = 0;
    double bestDist = ShortestDistanceFromArc(p, arclist[0]);
    for (size_t i = 1; i < arclist.size(); i++) {
        double d = ShortestDistanceFromArc(p, arclist[i]);
        if (d < bestDist) { bestDist = d; best = (int)i; }
    }
    return best;
}

// --- Selection ---

void DocumentBase::UnselectAll()
{
    for (auto &n : nodelist) n.IsSelected = false;
    for (auto &s : linelist) s.IsSelected = false;
    for (auto &a : arclist) a.IsSelected = false;
    for (auto &b : blocklist) b.IsSelected = false;
}

// --- Create Radius (fillet) ---
// Ported from femm42src/femm/FemmeDoc.cpp

// DEG is defined in complex.h as PI/180

bool DocumentBase::CanCreateRadius(int n) const
{
    if (n < 0 || n >= (int)nodelist.size()) return false;
    int j = 0;
    for (auto &s : linelist)
        if (s.n0 == n || s.n1 == n) j++;
    for (auto &a : arclist)
        if (a.n0 == n || a.n1 == n) j++;
    return j == 2;
}

// Replace the node indexed by n with a radius of r.
//
// This is FEMM 4.2's CbeladrawDoc::CreateRadius (beladrawDoc.cpp:2291-2566)
// transplanted, not reimplemented, and the reason is worth recording.
//
// The first port of this function did its own node bookkeeping -- retarget the
// endpoint of each curve onto a new tangency-point node, then delete the corner
// -- instead of calling AddNode()/DeleteSelectedNodes() the way the original
// does. Both bugs that produced came from that choice, not from the geometry:
//
//   * AddNode() attaches each tangency point to whichever curve it actually
//     LIES ON. Doing it by hand meant deciding, and the decision was made after
//     a swap of i1/i2 that exists only to orient the new fillet arc -- so
//     whenever that swap fired, the line was re-terminated on the ARC's
//     tangency point and the arc on the LINE's. In the original the swap comes
//     after the nodes are placed and can reach nothing but ar.n0/ar.n1.
//   * AddNode() also SPLITS the curve it lands on, and a split recomputes both
//     sweep angles. An arc is stored as (n0, n1, swept angle) with its centre
//     and radius DERIVED, so moving an endpoint without recomputing the sweep
//     silently moves the arc onto a different circle -- measured on the
//     reported model, radius 52.87 about (7.29, 19.60) became 46.75 about
//     (9.80, 25.20), which destroys the tangency the fillet exists to create.
//
// Every primitive the original needs already existed here and is itself a
// faithful port -- AddNode's arc split uses the same arg((a2-c)/(a0-c))
// formula, and DeleteSelectedNodes erases the curves attached to a deleted
// node. So the original transplants unchanged, and the call ORDER is load
// bearing: ClosestNode() is only unambiguous once the corner node is gone.
//
// Two deliberate departures from the original, both local:
//   * the seg[]/arc[] fill is bounds-guarded. The original writes
//     seg[nseg++] before testing whether the total is 2, which overruns a
//     two-element array on a node with three or more curves attached.
//   * UpdateUndo() is not called: undo is not libfemm's concern here.
bool DocumentBase::CreateRadius(int n, double r)
{
    if (r <= 0) return false;
    if (n < 0 || n >= (int)nodelist.size()) return false;

    int nseg = 0, narc = 0, k, arc[2], seg[2];

    for (k = 0; k < (int)linelist.size(); k++)
        if ((linelist[k].n0 == n) || (linelist[k].n1 == n)) {
            if (nseg < 2) seg[nseg] = k;
            nseg++;
        }
    for (k = 0; k < (int)arclist.size(); k++)
        if ((arclist[k].n0 == n) || (arclist[k].n1 == n)) {
            if (narc < 2) arc[narc] = k;
            narc++;
        }
    if ((narc + nseg) != 2) return false;

    // there are three valid cases:
    switch (nseg - narc)
    {
        case 0:  // One arc and one line
        {
            CComplex c, u, p0, p1, q, p[4], v[8], i1[8], i2[8];
            double rc, b, R[4], phi;
            FArcSegment ar;
            int j, m;

            // inherit the boundary condition from the arc so that
            // we can apply it to the newly created arc later;
            ar.InGroup        = arclist[arc[0]].InGroup;
            ar.BoundaryMarker = arclist[arc[0]].BoundaryMarker;

            // get the center and radius of the circle associated with the arc;
            GetCircle(arclist[arc[0]], c, rc);

            // get the locations of the endpoints of the segment;
            p0 = nodelist[n].CC();
            if (linelist[seg[0]].n0 == n)
                p1 = nodelist[linelist[seg[0]].n1].CC();
            else
                p1 = nodelist[linelist[seg[0]].n0].CC();

            u = (p1 - p0) / abs(p1 - p0);   // unit vector along the line
            q = p0 + u * Re((c - p0) / u);  // closest point on line to center
            u = CComplex(0, 1) * u;         // unit normal to the line

            p[0] = q + r * u; R[0] = rc + r;
            p[1] = q - r * u; R[1] = rc + r;
            p[2] = q + r * u; R[2] = rc - r;
            p[3] = q - r * u; R[3] = rc - r;

            for (j = 0, k = 0; k < 4; k++)
            {
                b = R[k] * R[k] - abs(p[k] - c) * abs(p[k] - c);
                if (b >= 0) {
                    b = sqrt(b);
                    v[j++] = p[k] + CComplex(0, 1) * b * (p[k] - c) / abs(p[k] - c);
                    v[j++] = p[k] - CComplex(0, 1) * b * (p[k] - c) / abs(p[k] - c);
                }
            }

            // locations of the centerpoints that could be for the radius that
            // we are looking for are stored in v.  We now need to paw through
            // them to find the one solution that we are after.
            u = (p1 - p0) / abs(p1 - p0);   // unit vector along the line
            for (m = 0, k = 0; k < j; k++)
            {
                i1[m] = p0 + u * Re((v[k] - p0) / u);      // intersection with the line
                i2[m] = c + rc * (v[k] - c) / abs(v[k] - c); // intersection with the arc;
                v[m] = v[k];

                // add this one to the list of possibly valid solutions if
                // both of the intersection points actually lie on the arc
                if (ShortestDistanceFromArc(i2[m], arclist[arc[0]]) < (r / 10000.) &&
                    ShortestDistance(i1[m].re, i1[m].im, seg[0]) < (r / 10000.) &&
                    abs(i1[m] - i2[m]) > (r / 10000.))
                {
                    m++;
                    if (m == 2) break;
                }
            }

            if (m == 0) return false;

            // But there are also special cases where there could be two answers.
            // We then pick the solution that has the center point closest to
            // the point to be removed.
            if (m > 1) {
                if (abs(v[0] - p0) < abs(v[1] - p0)) j = 0;
                else j = 1;
            }
            else j = 0;   // The index of the winning case is in j....

            // add new nodes at ends of radius
            AddNode(i1[j].re, i1[j].im, r / 10000.);
            AddNode(i2[j].re, i2[j].im, r / 10000.);
            UnselectAll();

            // delete the node that is to be replaced by a radius;
            n = ClosestNode(p0.re, p0.im);
            nodelist[n].IsSelected = true;
            DeleteSelectedNodes();

            // compute the angle spanned by the new arc;
            phi = arg((i2[j] - v[j]) / (i1[j] - v[j]));
            if (phi < 0) {
                c = i2[j]; i2[j] = i1[j]; i1[j] = c;
                phi = fabs(phi);
            }

            // add in the new radius;
            ar.n0 = ClosestNode(i1[j].re, i1[j].im);
            ar.n1 = ClosestNode(i2[j].re, i2[j].im);
            ar.ArcLength = phi / DEG;
            AddArcSegment(ar);

            return true;
        }
        case 2:  // Two lines
        {
            CComplex p0, p1, p2;
            double phi, len;
            FArcSegment ar;

            if (linelist[seg[0]].n0 == n) p1 = nodelist[linelist[seg[0]].n1].CC();
            else p1 = nodelist[linelist[seg[0]].n0].CC();

            if (linelist[seg[1]].n0 == n) p2 = nodelist[linelist[seg[1]].n1].CC();
            else p2 = nodelist[linelist[seg[1]].n0].CC();

            p0 = nodelist[n].CC();

            // get the angle between the lines
            phi = arg((p2 - p0) / (p1 - p0));

            // check to see if this case is degenerate
            if (fabs(phi) > (179. * DEG)) return false;

            // check to see if the points are in the wrong order
            // and fix it if they are.
            if (phi < 0) {
                p0 = p1; p1 = p2; p2 = p0; p0 = nodelist[n].CC();
                k = seg[0]; seg[0] = seg[1]; seg[1] = k;
                phi = fabs(phi);
            }

            len = r / tan(phi / 2.);  // distance from p0 to the tangency point;

            // catch the case where the desired radius is too big to fit;
            if ((abs(p1 - p0) < len) || (abs(p2 - p0) < len)) return false;

            // compute the locations of the tangency points;
            p1 = len * (p1 - p0) / abs(p1 - p0) + p0;
            p2 = len * (p2 - p0) / abs(p2 - p0) + p0;

            // inherit the boundary condition from one of the segments
            // so that we can apply it to the newly created arc later;
            ar.BoundaryMarker = linelist[seg[0]].BoundaryMarker;
            ar.InGroup        = linelist[seg[0]].InGroup;

            // add new nodes at ends of radius
            AddNode(p1.re, p1.im, len / 10000.);
            AddNode(p2.re, p2.im, len / 10000.);
            UnselectAll();

            // delete the node that is to be replaced by a radius;
            n = ClosestNode(p0.re, p0.im);
            nodelist[n].IsSelected = true;
            DeleteSelectedNodes();

            // add in the new radius;
            ar.n0 = ClosestNode(p2.re, p2.im);
            ar.n1 = ClosestNode(p1.re, p1.im);
            ar.ArcLength = 180. - phi / DEG;
            AddArcSegment(ar);

            return true;
        }
        case -2: // Two arcs
        {
            int j;
            CComplex c0, c1, c2, p[8], i1[8], i2[8];
            double a[8], b[8], c, d[8], x[8], r0, r1, r2, phi;
            FArcSegment ar;

            r0 = r;
            GetCircle(arclist[arc[0]], c1, r1);
            GetCircle(arclist[arc[1]], c2, r2);
            c = abs(c2 - c1);

            // solve for all of the different possible cases;
            a[0] = r1 + r0; b[0] = r2 + r0;
            a[1] = r1 + r0; b[1] = r2 + r0;
            a[2] = r1 - r0; b[2] = r2 - r0;
            a[3] = r1 - r0; b[3] = r2 - r0;
            a[4] = r1 - r0; b[4] = r2 + r0;
            a[5] = r1 - r0; b[5] = r2 + r0;
            a[6] = r1 + r0; b[6] = r2 - r0;
            a[7] = r1 + r0; b[7] = r2 - r0;
            for (k = 0; k < 8; k++) {
                x[k] = (b[k] * b[k] + c * c - a[k] * a[k]) / (2. * c * c);
                d[k] = sqrt(b[k] * b[k] - x[k] * x[k] * c * c);
            }
            for (k = 0; k < 8; k += 2)
            {
                // solve for the center point of the radius for each solution.
                p[k]     = ((1 - x[k]) * c + CComplex(0, 1) * d[k]) * (c2 - c1) / abs(c2 - c1) + c1;
                p[k + 1] = ((1 - x[k]) * c - CComplex(0, 1) * d[k]) * (c2 - c1) / abs(c2 - c1) + c1;
            }

            c0 = nodelist[n].CC();

            for (j = 0, k = 0; k < 8; k++)
            {
                i1[j] = c1 + r1 * (p[k] - c1) / abs(p[k] - c1); // possible intersection points
                i2[j] = c2 + r2 * (p[k] - c2) / abs(p[k] - c2); // with the arcs;
                p[j] = p[k];

                // add this one to the list of possibly valid solutions if
                // both of the intersection points actually lie on the arc
                if (ShortestDistanceFromArc(i1[j], arclist[arc[0]]) < (r0 / 10000.) &&
                    ShortestDistanceFromArc(i2[j], arclist[arc[1]]) < (r0 / 10000.) &&
                    abs(i1[j] - i2[j]) > (r0 / 10000.))
                {
                    j++;
                    if (j == 2) break;
                }
            }

            // There could be no valid solutions...
            if (j == 0) return false;

            // But there are also special cases where there could be two answers.
            // We then pick the solution that has the center point closest to
            // the point to be removed.
            if (j > 1) {
                if (abs(p[0] - c0) < abs(p[1] - c0)) j = 0;
                else j = 1;
            }
            else j = 0;   // The index of the winning case is in j....

            // inherit the boundary condition from one of the segments
            // so that we can apply it to the newly created arc later;
            ar.BoundaryMarker = arclist[arc[0]].BoundaryMarker;
            ar.InGroup        = arclist[arc[0]].InGroup;

            // add new nodes at ends of radius
            AddNode(i1[j].re, i1[j].im, c / 10000.);
            AddNode(i2[j].re, i2[j].im, c / 10000.);
            UnselectAll();

            // delete the node that is to be replaced by a radius;
            n = ClosestNode(c0.re, c0.im);
            nodelist[n].IsSelected = true;
            DeleteSelectedNodes();

            // compute the angle spanned by the new arc;
            phi = arg((i2[j] - p[j]) / (i1[j] - p[j]));
            if (phi < 0) {
                c0 = i2[j]; i2[j] = i1[j]; i1[j] = c0;
                phi = fabs(phi);
            }

            // add in the new radius;
            ar.n0 = ClosestNode(i1[j].re, i1[j].im);
            ar.n1 = ClosestNode(i2[j].re, i2[j].im);
            ar.ArcLength = phi / DEG;
            AddArcSegment(ar);

            return true;
        }
    }

    return false;
}

// --- Delete selected ---

bool DocumentBase::DeleteSelectedNodes()
{
    bool changed = false;
    // First remove any segments/arcs referencing selected nodes
    for (int i = (int)nodelist.size() - 1; i >= 0; i--) {
        if (!nodelist[i].IsSelected) continue;
        changed = true;

        // Remove segments referencing this node
        for (int j = (int)linelist.size() - 1; j >= 0; j--) {
            if (linelist[j].n0 == i || linelist[j].n1 == i)
                linelist.erase(linelist.begin() + j);
        }
        // Remove arcs referencing this node
        for (int j = (int)arclist.size() - 1; j >= 0; j--) {
            if (arclist[j].n0 == i || arclist[j].n1 == i)
                arclist.erase(arclist.begin() + j);
        }
        // Remove the node
        nodelist.erase(nodelist.begin() + i);

        // Renumber segment/arc node references
        for (auto &s : linelist) {
            if (s.n0 > i) s.n0--;
            if (s.n1 > i) s.n1--;
        }
        for (auto &a : arclist) {
            if (a.n0 > i) a.n0--;
            if (a.n1 > i) a.n1--;
        }
    }
    return changed;
}

bool DocumentBase::DeleteSelectedSegments()
{
    bool changed = false;
    for (int i = (int)linelist.size() - 1; i >= 0; i--) {
        if (linelist[i].IsSelected) {
            linelist.erase(linelist.begin() + i);
            changed = true;
        }
    }
    return changed;
}

bool DocumentBase::DeleteSelectedArcSegments()
{
    bool changed = false;
    for (int i = (int)arclist.size() - 1; i >= 0; i--) {
        if (arclist[i].IsSelected) {
            arclist.erase(arclist.begin() + i);
            changed = true;
        }
    }
    return changed;
}

bool DocumentBase::DeleteSelectedBlockLabels()
{
    bool changed = false;
    for (int i = (int)blocklist.size() - 1; i >= 0; i--) {
        if (blocklist[i].IsSelected) {
            blocklist.erase(blocklist.begin() + i);
            changed = true;
        }
    }
    return changed;
}

// --- Transforms ---

// For selected segments/arcs, mark their endpoint nodes as selected
// so the endpoints move with them. Matching OF's MOVECOPY.CPP approach.
void DocumentBase::markEndpointsOfSelectedSegments(int EditAction)
{
    if (EditAction == 1 || EditAction == 4) {
        for (auto &s : linelist)
            if (s.IsSelected) {
                nodelist[s.n0].IsSelected = true;
                nodelist[s.n1].IsSelected = true;
            }
    }
    if (EditAction == 3 || EditAction == 4) {
        for (auto &a : arclist)
            if (a.IsSelected) {
                nodelist[a.n0].IsSelected = true;
                nodelist[a.n1].IsSelected = true;
            }
    }
}

void DocumentBase::TranslateMove(double dx, double dy, int EditAction)
{
    if (EditAction == 1 || EditAction == 3) {
        markEndpointsOfSelectedSegments(EditAction);
        TranslateMove(dx, dy, 0);
        return;
    }

    if (EditAction == 4)
        markEndpointsOfSelectedSegments(EditAction);

    for (auto &n : nodelist) {
        if ((EditAction == 0 || EditAction == 4) && n.IsSelected) {
            n.x += dx; n.y += dy;
        }
    }
    for (auto &b : blocklist) {
        if ((EditAction == 2 || EditAction == 4) && b.IsSelected) {
            b.x += dx; b.y += dy;
        }
    }
    EnforcePSLG();
}

// A permanent magnet's magnetization direction is part of its orientation, so a
// geometric transform has to carry it along -- rotating or mirroring a magnet
// without it leaves the body moved and the field still pointing the old way.
// Guarded on coercivity exactly as OF does (MOVECOPY.CPP:659-664): MagDir is
// meaningless for a non-magnet, and rotating it there would dirty the file for
// no reason. materialCoercivity() is a DocumentBase hook that only the
// magnetics document answers, so the three scalar physics get 0 and skip.
//
// MagDirFctn (the position-dependent form) is deliberately left alone, as OF
// leaves it: where it is set the solver ignores MagDir entirely, so there is
// nothing to correct.
bool DocumentBase::blockIsMagnet(const FBlockLabel &b) const
{
    return materialCoercivity(b.BlockType) != 0.0;
}

void DocumentBase::RotateMove(CComplex c, double t, int EditAction)
{
    if (EditAction == 1 || EditAction == 3) {
        markEndpointsOfSelectedSegments(EditAction);
        RotateMove(c, t, 0);
        return;
    }

    if (EditAction == 4)
        markEndpointsOfSelectedSegments(EditAction);

    double theta = t * PI / 180.0;
    CComplex rot(cos(theta), sin(theta));

    for (auto &n : nodelist) {
        if ((EditAction == 0 || EditAction == 4) && n.IsSelected) {
            CComplex p = (n.CC() - c) * rot + c;
            n.x = p.re; n.y = p.im;
        }
    }
    for (auto &b : blocklist) {
        if ((EditAction == 2 || EditAction == 4) && b.IsSelected) {
            CComplex p = (CComplex(b.x, b.y) - c) * rot + c;
            b.x = p.re; b.y = p.im;
            if (blockIsMagnet(b)) b.MagDir += t;
        }
    }
    EnforcePSLG();
}

void DocumentBase::ScaleMove(double bx, double by, double sf, int EditAction)
{
    if (EditAction == 1 || EditAction == 3) {
        markEndpointsOfSelectedSegments(EditAction);
        ScaleMove(bx, by, sf, 0);
        return;
    }

    if (EditAction == 4)
        markEndpointsOfSelectedSegments(EditAction);

    for (auto &n : nodelist) {
        if ((EditAction == 0 || EditAction == 4) && n.IsSelected) {
            n.x = bx + sf * (n.x - bx);
            n.y = by + sf * (n.y - by);
        }
    }
    for (auto &b : blocklist) {
        if ((EditAction == 2 || EditAction == 4) && b.IsSelected) {
            b.x = bx + sf * (b.x - bx);
            b.y = by + sf * (b.y - by);
            // The user-facing mesh size is a LENGTH, but MaxArea stores it as
            // an area (PI*meshsize^2/4), so it scales as sf^2. MaxArea==0
            // (automesh) is unaffected and stays automesh. Matches OF
            // cd_movecopy.cpp ScaleMove.
            b.MaxArea *= sf * sf;
        }
    }
    EnforcePSLG();
}

void DocumentBase::MirrorSelected(double x0, double y0, double x1, double y1, int ea)
{
    // Mirror is a copy operation (creates mirrored duplicates), matching OF.
    CComplex x(x0, y0);
    CComplex p((x1 - x0), (y1 - y0));
    if (abs(p) == 0) return;
    p /= abs(p);

    auto mirrorPt = [&](double px, double py, double &ox, double &oy) {
        CComplex y = (CComplex(px, py) - x) / p;
        y = p * y.Conj() + x;
        ox = y.re; oy = y.im;
    };

    size_t origNodes = nodelist.size();
    size_t origSegs = linelist.size();
    size_t origArcs = arclist.size();
    size_t origBlks = blocklist.size();

    if (ea == 0 || ea == 4) {
        for (size_t i = 0; i < origNodes; i++) {
            if (nodelist[i].IsSelected) {
                FNode n = nodelist[i];
                mirrorPt(n.x, n.y, n.x, n.y);
                n.IsSelected = false;
                nodelist.push_back(n);
            }
        }
    }

    if (ea == 1 || ea == 4) {
        for (size_t i = 0; i < origSegs; i++) {
            if (linelist[i].IsSelected) {
                FNode n0 = nodelist[linelist[i].n0];
                FNode n1 = nodelist[linelist[i].n1];
                mirrorPt(n0.x, n0.y, n0.x, n0.y);
                mirrorPt(n1.x, n1.y, n1.x, n1.y);
                int j = (int)nodelist.size();
                FSegment s = linelist[i];
                s.n0 = j; s.n1 = j + 1;
                s.IsSelected = false;
                nodelist.push_back(n0);
                nodelist.push_back(n1);
                linelist.push_back(s);
            }
        }
    }

    if (ea == 2 || ea == 4) {
        for (size_t i = 0; i < origBlks; i++) {
            if (blocklist[i].IsSelected) {
                FBlockLabel b = blocklist[i];
                mirrorPt(b.x, b.y, b.x, b.y);
                // DELIBERATE DIVERGENCE FROM OF, decided 2026-08-21: OF
                // guards the coercivity test on rotate and copy-rotate but NOT
                // on mirror -- both its mirror sites (MOVECOPY.CPP:1949, 2047)
                // reflect MagDir for every selected label. We guard all three,
                // for consistency and to avoid rewriting a field that means
                // nothing for the material. No physical difference either way,
                // since MagDir is ignored on a non-magnet. Do not "restore" OF
                // behaviour here without asking; the inconsistency is OF's.
                if (blockIsMagnet(b)) {
                    // Reflect the direction in the same line, not just the
                    // point: the OF expression, MOVECOPY.CPP:1949.
                    CComplex d = exp(CComplex(0, b.MagDir * PI / 180.0));
                    b.MagDir = (180.0 / PI) * arg(p * (d / p).Conj());
                }
                b.IsSelected = false;
                blocklist.push_back(b);
            }
        }
    }

    if (ea == 3 || ea == 4) {
        for (size_t i = 0; i < origArcs; i++) {
            if (arclist[i].IsSelected) {
                // Swap n0/n1 to reverse arc direction after mirroring
                FNode n0 = nodelist[arclist[i].n1];
                FNode n1 = nodelist[arclist[i].n0];
                mirrorPt(n0.x, n0.y, n0.x, n0.y); n0.IsSelected = false;
                mirrorPt(n1.x, n1.y, n1.x, n1.y); n1.IsSelected = false;
                int j = (int)nodelist.size();
                FArcSegment a = arclist[i];
                a.n0 = j; a.n1 = j + 1;
                a.IsSelected = false;
                nodelist.push_back(n0);
                nodelist.push_back(n1);
                arclist.push_back(a);
            }
        }
    }

    EnforcePSLG();
}

void DocumentBase::TranslateCopy(double dx, double dy, int ncopies, int EditAction)
{
    size_t origNodes = nodelist.size();
    size_t origSegs = linelist.size();
    size_t origArcs = arclist.size();
    size_t origBlks = blocklist.size();

    for (int nc = 0; nc < ncopies; nc++) {
        double ox = dx * (nc + 1), oy = dy * (nc + 1);

        if (EditAction == 0 || EditAction == 4) {
            for (size_t i = 0; i < origNodes; i++) {
                if (nodelist[i].IsSelected) {
                    FNode n = nodelist[i];
                    n.x += ox; n.y += oy;
                    n.IsSelected = false;
                    nodelist.push_back(n);
                }
            }
        }

        if (EditAction == 1 || EditAction == 4) {
            for (size_t i = 0; i < origSegs; i++) {
                if (linelist[i].IsSelected) {
                    FNode n0 = nodelist[linelist[i].n0];
                    FNode n1 = nodelist[linelist[i].n1];
                    n0.x += ox; n0.y += oy; n0.IsSelected = false;
                    n1.x += ox; n1.y += oy; n1.IsSelected = false;
                    int j = (int)nodelist.size();
                    FSegment s = linelist[i];
                    s.n0 = j; s.n1 = j + 1;
                    s.IsSelected = false;
                    nodelist.push_back(n0);
                    nodelist.push_back(n1);
                    linelist.push_back(s);
                }
            }
        }

        if (EditAction == 2 || EditAction == 4) {
            for (size_t i = 0; i < origBlks; i++) {
                if (blocklist[i].IsSelected) {
                    FBlockLabel b = blocklist[i];
                    b.x += ox; b.y += oy;
                    b.IsSelected = false;
                    blocklist.push_back(b);
                }
            }
        }

        if (EditAction == 3 || EditAction == 4) {
            for (size_t i = 0; i < origArcs; i++) {
                if (arclist[i].IsSelected) {
                    FNode n0 = nodelist[arclist[i].n0];
                    FNode n1 = nodelist[arclist[i].n1];
                    n0.x += ox; n0.y += oy; n0.IsSelected = false;
                    n1.x += ox; n1.y += oy; n1.IsSelected = false;
                    int j = (int)nodelist.size();
                    FArcSegment a = arclist[i];
                    a.n0 = j; a.n1 = j + 1;
                    a.IsSelected = false;
                    nodelist.push_back(n0);
                    nodelist.push_back(n1);
                    arclist.push_back(a);
                }
            }
        }
    }
    EnforcePSLG();
}

void DocumentBase::RotateCopy(CComplex c, double t, int ncopies, int EditAction)
{
    size_t origNodes = nodelist.size();
    size_t origSegs = linelist.size();
    size_t origArcs = arclist.size();
    size_t origBlks = blocklist.size();

    for (int nc = 0; nc < ncopies; nc++) {
        double theta = t * (nc + 1) * PI / 180.0;
        CComplex rot(cos(theta), sin(theta));

        if (EditAction == 0 || EditAction == 4) {
            for (size_t i = 0; i < origNodes; i++) {
                if (nodelist[i].IsSelected) {
                    FNode n = nodelist[i];
                    CComplex p = (n.CC() - c) * rot + c;
                    n.x = p.re; n.y = p.im;
                    n.IsSelected = false;
                    nodelist.push_back(n);
                }
            }
        }

        if (EditAction == 1 || EditAction == 4) {
            for (size_t i = 0; i < origSegs; i++) {
                if (linelist[i].IsSelected) {
                    FNode n0 = nodelist[linelist[i].n0];
                    FNode n1 = nodelist[linelist[i].n1];
                    CComplex p0 = (n0.CC() - c) * rot + c;
                    CComplex p1 = (n1.CC() - c) * rot + c;
                    n0.x = p0.re; n0.y = p0.im; n0.IsSelected = false;
                    n1.x = p1.re; n1.y = p1.im; n1.IsSelected = false;
                    int j = (int)nodelist.size();
                    FSegment s = linelist[i];
                    s.n0 = j; s.n1 = j + 1;
                    s.IsSelected = false;
                    nodelist.push_back(n0);
                    nodelist.push_back(n1);
                    linelist.push_back(s);
                }
            }
        }

        if (EditAction == 2 || EditAction == 4) {
            for (size_t i = 0; i < origBlks; i++) {
                if (blocklist[i].IsSelected) {
                    FBlockLabel b = blocklist[i];
                    CComplex p = (CComplex(b.x, b.y) - c) * rot + c;
                    b.x = p.re; b.y = p.im;
                    // t*(nc+1), the angle THIS copy was turned through, not the
                    // per-copy step -- copy 3 of a 30 degree sweep is at 90.
                    if (blockIsMagnet(b)) b.MagDir += t * (nc + 1);
                    b.IsSelected = false;
                    blocklist.push_back(b);
                }
            }
        }

        if (EditAction == 3 || EditAction == 4) {
            for (size_t i = 0; i < origArcs; i++) {
                if (arclist[i].IsSelected) {
                    FNode n0 = nodelist[arclist[i].n0];
                    FNode n1 = nodelist[arclist[i].n1];
                    CComplex p0 = (n0.CC() - c) * rot + c;
                    CComplex p1 = (n1.CC() - c) * rot + c;
                    n0.x = p0.re; n0.y = p0.im; n0.IsSelected = false;
                    n1.x = p1.re; n1.y = p1.im; n1.IsSelected = false;
                    int j = (int)nodelist.size();
                    FArcSegment a = arclist[i];
                    a.n0 = j; a.n1 = j + 1;
                    a.IsSelected = false;
                    nodelist.push_back(n0);
                    nodelist.push_back(n1);
                    arclist.push_back(a);
                }
            }
        }
    }
    EnforcePSLG();
}

// ================================================================
// Undo / Redo
// ================================================================

DocumentBase::GeometrySnapshot DocumentBase::takeSnapshot() const
{
    return { nodelist, linelist, arclist, blocklist };
}

void DocumentBase::restoreSnapshot(const GeometrySnapshot &snap)
{
    nodelist = snap.nodes;
    linelist = snap.segs;
    arclist = snap.arcs;
    blocklist = snap.blocks;
}

void DocumentBase::pushUndo()
{
    m_undoStack.push_back(takeSnapshot());
    if ((int)m_undoStack.size() > MAX_UNDO_LEVELS)
        m_undoStack.pop_front();
    m_redoStack.clear();
}

bool DocumentBase::canUndo() const { return !m_undoStack.empty(); }
bool DocumentBase::canRedo() const { return !m_redoStack.empty(); }

void DocumentBase::undo()
{
    if (m_undoStack.empty()) return;
    m_redoStack.push_back(takeSnapshot());
    restoreSnapshot(m_undoStack.back());
    m_undoStack.pop_back();
}

void DocumentBase::redo()
{
    if (m_redoStack.empty()) return;
    m_undoStack.push_back(takeSnapshot());
    restoreSnapshot(m_redoStack.back());
    m_redoStack.pop_back();
}

void DocumentBase::clearUndoHistory()
{
    m_undoStack.clear();
    m_redoStack.clear();
}
