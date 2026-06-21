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

bool DocumentBase::getBoundingBox(double &xmin, double &ymin, double &xmax, double &ymax) const
{
    if (nodelist.empty()) return false;

    xmin = xmax = nodelist[0].x;
    ymin = ymax = nodelist[0].y;

    // Nodes
    for (const auto &n : nodelist) {
        if (n.x < xmin) xmin = n.x;
        if (n.x > xmax) xmax = n.x;
        if (n.y < ymin) ymin = n.y;
        if (n.y > ymax) ymax = n.y;
    }

    // Block labels
    for (const auto &b : blocklist) {
        if (b.x < xmin) xmin = b.x;
        if (b.x > xmax) xmax = b.x;
        if (b.y < ymin) ymin = b.y;
        if (b.y > ymax) ymax = b.y;
    }

    // Arc segments — traverse at discretization level to capture full extent
    for (const auto &arc : arclist) {
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

        // Find start angle
        CComplex p0 = nodelist[arc.n0].CC();
        double startAngle = arg((p0 - c) / R);

        // Direction: arc goes from n0 to n1 over ArcLength degrees
        double totalRad = arc.ArcLength * PI / 180.0;
        // Determine sign: check if going CCW or CW from n0 to n1
        CComplex p1 = nodelist[arc.n1].CC();
        double endAngle = arg((p1 - c) / R);
        double diff = endAngle - startAngle;
        // Normalize to [0, 2*PI)
        while (diff < 0) diff += 2.0 * PI;
        while (diff >= 2.0 * PI) diff -= 2.0 * PI;
        double sign = 1.0;
        if (fabs(diff - totalRad) > fabs(diff + totalRad - 2.0 * PI))
            sign = -1.0;

        for (int k = 0; k <= nsteps; k++) {
            double theta = startAngle + sign * totalRad * k / nsteps;
            double px = c.re + R * cos(theta);
            double py = c.im + R * sin(theta);
            if (px < xmin) xmin = px;
            if (px > xmax) xmax = px;
            if (py < ymin) ymin = py;
            if (py > ymax) ymax = py;
        }
    }

    return true;
}

// --- Add operations (PSLG-aware) ---
// Ported from femm42src/femm/FemmeDoc.cpp and MOVECOPY.CPP

bool DocumentBase::AddNode(double x, double y, double d)
{
    // test to see if "too close" to existing node
    for (size_t i = 0; i < nodelist.size(); i++)
        if (nodelist[i].GetDistance(x, y) < d) return false;

    // can't put a node on top of a block label
    for (size_t i = 0; i < blocklist.size(); i++)
        if (blocklist[i].GetDistance(x, y) < d) return false;

    FNode pt;
    pt.x = x; pt.y = y;
    nodelist.push_back(pt);
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

bool DocumentBase::AddNode(FNode &node, double d)
{
    return AddNode(node.x, node.y, d);
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

bool DocumentBase::CreateRadius(int n, double r)
{
    if (r <= 0 || n < 0 || n >= (int)nodelist.size()) return false;

    // Find the two segments/arcs connected to node n
    int nseg = 0, narc = 0, seg[2] = {-1,-1}, arc[2] = {-1,-1};
    for (int k = 0; k < (int)linelist.size(); k++)
        if (linelist[k].n0 == n || linelist[k].n1 == n) {
            if (nseg < 2) seg[nseg] = k;
            nseg++;
        }
    for (int k = 0; k < (int)arclist.size(); k++)
        if (arclist[k].n0 == n || arclist[k].n1 == n) {
            if (narc < 2) arc[narc] = k;
            narc++;
        }
    if ((narc + nseg) != 2) return false;

    // Helper: remove node n and renumber all segment/arc references
    auto removeNode = [&](int idx) {
        nodelist.erase(nodelist.begin() + idx);
        for (auto &s : linelist) {
            if (s.n0 > idx) s.n0--;
            if (s.n1 > idx) s.n1--;
        }
        for (auto &a : arclist) {
            if (a.n0 > idx) a.n0--;
            if (a.n1 > idx) a.n1--;
        }
    };

    // Helper: update endpoint of a segment from old node to new node
    auto retargetSeg = [&](int segIdx, int oldNode, int newNode) {
        if (linelist[segIdx].n0 == oldNode) linelist[segIdx].n0 = newNode;
        if (linelist[segIdx].n1 == oldNode) linelist[segIdx].n1 = newNode;
    };

    // Helper: update endpoint of an arc from old node to new node
    auto retargetArc = [&](int arcIdx, int oldNode, int newNode) {
        if (arclist[arcIdx].n0 == oldNode) arclist[arcIdx].n0 = newNode;
        if (arclist[arcIdx].n1 == oldNode) arclist[arcIdx].n1 = newNode;
    };

    CComplex p0 = nodelist[n].CC();

    switch (nseg - narc) {
    case 2: // Two lines
    {
        CComplex p1, p2;
        if (linelist[seg[0]].n0 == n) p1 = nodelist[linelist[seg[0]].n1].CC();
        else p1 = nodelist[linelist[seg[0]].n0].CC();
        if (linelist[seg[1]].n0 == n) p2 = nodelist[linelist[seg[1]].n1].CC();
        else p2 = nodelist[linelist[seg[1]].n0].CC();

        double phi = arg((p2 - p0) / (p1 - p0));
        if (fabs(phi) > (179.0 * DEG)) return false;

        if (phi < 0) {
            std::swap(p1, p2);
            std::swap(seg[0], seg[1]);
            phi = fabs(phi);
        }

        double len = r / tan(phi / 2.0);
        if (abs(p1 - p0) < len || abs(p2 - p0) < len) return false;

        CComplex t1 = len * (p1 - p0) / abs(p1 - p0) + p0;  // tangent point on seg[0]
        CComplex t2 = len * (p2 - p0) / abs(p2 - p0) + p0;  // tangent point on seg[1]

        // Inherit boundary condition from one of the segments
        FArcSegment ar;
        ar.BoundaryMarker = linelist[seg[0]].BoundaryMarker;
        ar.InGroup = linelist[seg[0]].InGroup;

        // Add two new nodes
        FNode nd1; nd1.x = t1.re; nd1.y = t1.im;
        FNode nd2; nd2.x = t2.re; nd2.y = t2.im;
        int newN1 = (int)nodelist.size();
        nodelist.push_back(nd1);
        int newN2 = (int)nodelist.size();
        nodelist.push_back(nd2);

        // Retarget segments to the new tangent-point nodes
        retargetSeg(seg[0], n, newN1);
        retargetSeg(seg[1], n, newN2);

        // Remove the old corner node
        removeNode(n);
        // Adjust new node indices since removal shifts everything above n
        if (newN1 > n) newN1--;
        if (newN2 > n) newN2--;

        // Add the fillet arc
        ar.n0 = newN2;
        ar.n1 = newN1;
        ar.ArcLength = 180.0 - phi / DEG;
        arclist.push_back(ar);
        return true;
    }
    case 0: // One arc and one line
    {
        CComplex c, u, p1, q, p[4], v[8], i1[8], i2[8];
        double rc, b, R[4], phi;
        int j, m;

        FArcSegment ar;
        ar.InGroup = arclist[arc[0]].InGroup;
        ar.BoundaryMarker = arclist[arc[0]].BoundaryMarker;

        GetCircle(arclist[arc[0]], c, rc);

        if (linelist[seg[0]].n0 == n)
            p1 = nodelist[linelist[seg[0]].n1].CC();
        else
            p1 = nodelist[linelist[seg[0]].n0].CC();

        u = (p1 - p0) / abs(p1 - p0);
        q = p0 + u * Re((c - p0) / u);
        u = CComplex(0, 1) * u;

        p[0] = q + r * u; R[0] = rc + r;
        p[1] = q - r * u; R[1] = rc + r;
        p[2] = q + r * u; R[2] = rc - r;
        p[3] = q - r * u; R[3] = rc - r;

        for (j = 0, m = 0; m < 4; m++) {
            b = R[m] * R[m] - abs(p[m] - c) * abs(p[m] - c);
            if (b >= 0) {
                b = sqrt(b);
                CComplex dir = (p[m] - c) / abs(p[m] - c);
                v[j++] = p[m] + CComplex(0, 1) * b * dir;
                v[j++] = p[m] - CComplex(0, 1) * b * dir;
            }
        }

        u = (p1 - p0) / abs(p1 - p0);
        for (m = 0, b = 0; b < j; b++) {
            int bi = (int)b;
            i1[m] = p0 + u * Re((v[bi] - p0) / u);
            i2[m] = c + rc * (v[bi] - c) / abs(v[bi] - c);
            v[m] = v[bi];

            if (ShortestDistanceFromArc(i2[m], arclist[arc[0]]) < (r / 10000.0) &&
                ShortestDistance(Re(i1[m]), Im(i1[m]), seg[0]) < (r / 10000.0) &&
                abs(i1[m] - i2[m]) > (r / 10000.0))
            {
                m++;
                if (m == 2) break;
            }
        }

        if (m == 0) return false;

        int best = 0;
        if (m > 1)
            best = (abs(v[0] - p0) < abs(v[1] - p0)) ? 0 : 1;

        // Compute fillet arc angle
        phi = arg((i2[best] - v[best]) / (i1[best] - v[best]));
        CComplex arcStart = i1[best], arcEnd = i2[best];
        if (phi < 0) {
            std::swap(arcStart, arcEnd);
            phi = fabs(phi);
        }

        // Add new tangent-point nodes
        FNode nd1; nd1.x = arcStart.re; nd1.y = arcStart.im;
        FNode nd2; nd2.x = arcEnd.re;   nd2.y = arcEnd.im;
        int newN1 = (int)nodelist.size();
        nodelist.push_back(nd1);
        int newN2 = (int)nodelist.size();
        nodelist.push_back(nd2);

        // Retarget segment and arc to new nodes
        retargetSeg(seg[0], n, newN1);
        retargetArc(arc[0], n, newN2);

        // Remove old corner node
        removeNode(n);
        if (newN1 > n) newN1--;
        if (newN2 > n) newN2--;

        ar.n0 = newN1;
        ar.n1 = newN2;
        ar.ArcLength = phi / DEG;
        arclist.push_back(ar);
        return true;
    }
    case -2: // Two arcs
    {
        CComplex c0pos = p0, c1, c2, pts[8], ii1[8], ii2[8];
        double a[8], bv[8], cv, dv[8], xv[8], r0, r1, r2, phi;
        int j, k;

        FArcSegment ar;
        ar.BoundaryMarker = arclist[arc[0]].BoundaryMarker;
        ar.InGroup = arclist[arc[0]].InGroup;

        r0 = r;
        GetCircle(arclist[arc[0]], c1, r1);
        GetCircle(arclist[arc[1]], c2, r2);
        cv = abs(c2 - c1);

        a[0] = r1 + r0; bv[0] = r2 + r0;
        a[1] = r1 + r0; bv[1] = r2 + r0;
        a[2] = r1 - r0; bv[2] = r2 - r0;
        a[3] = r1 - r0; bv[3] = r2 - r0;
        a[4] = r1 - r0; bv[4] = r2 + r0;
        a[5] = r1 - r0; bv[5] = r2 + r0;
        a[6] = r1 + r0; bv[6] = r2 - r0;
        a[7] = r1 + r0; bv[7] = r2 - r0;

        for (k = 0; k < 8; k++) {
            xv[k] = (bv[k] * bv[k] + cv * cv - a[k] * a[k]) / (2.0 * cv * cv);
            double disc = bv[k] * bv[k] - xv[k] * xv[k] * cv * cv;
            dv[k] = (disc >= 0) ? sqrt(disc) : 0;
        }

        CComplex dir = (c2 - c1) / abs(c2 - c1);
        for (k = 0; k < 8; k += 2) {
            pts[k]     = ((1 - xv[k]) * cv + CComplex(0, 1) * dv[k]) * dir + c1;
            pts[k + 1] = ((1 - xv[k]) * cv - CComplex(0, 1) * dv[k]) * dir + c1;
        }

        for (j = 0, k = 0; k < 8; k++) {
            ii1[j] = c1 + r1 * (pts[k] - c1) / abs(pts[k] - c1);
            ii2[j] = c2 + r2 * (pts[k] - c2) / abs(pts[k] - c2);
            pts[j] = pts[k];

            if (ShortestDistanceFromArc(ii1[j], arclist[arc[0]]) < (r0 / 10000.0) &&
                ShortestDistanceFromArc(ii2[j], arclist[arc[1]]) < (r0 / 10000.0) &&
                abs(ii1[j] - ii2[j]) > (r0 / 10000.0))
            {
                j++;
                if (j == 2) break;
            }
        }

        if (j == 0) return false;

        int best = 0;
        if (j > 1)
            best = (abs(pts[0] - c0pos) < abs(pts[1] - c0pos)) ? 0 : 1;

        phi = arg((ii2[best] - pts[best]) / (ii1[best] - pts[best]));
        CComplex arcStart = ii1[best], arcEnd = ii2[best];
        if (phi < 0) {
            std::swap(arcStart, arcEnd);
            phi = fabs(phi);
        }

        FNode nd1; nd1.x = arcStart.re; nd1.y = arcStart.im;
        FNode nd2; nd2.x = arcEnd.re;   nd2.y = arcEnd.im;
        int newN1 = (int)nodelist.size();
        nodelist.push_back(nd1);
        int newN2 = (int)nodelist.size();
        nodelist.push_back(nd2);

        retargetArc(arc[0], n, newN1);
        retargetArc(arc[1], n, newN2);

        removeNode(n);
        if (newN1 > n) newN1--;
        if (newN2 > n) newN2--;

        ar.n0 = newN1;
        ar.n1 = newN2;
        ar.ArcLength = phi / DEG;
        arclist.push_back(ar);
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
