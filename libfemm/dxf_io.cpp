// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// dxf_io.cpp — DXF import/export for FEMM geometry
// Reader: clean group-code parser supporting LINE, ARC, CIRCLE,
// LWPOLYLINE, POLYLINE/VERTEX (with bulge), ELLIPSE, SPLINE.
// Writer: ported from femm42src/femm/bd_movecopy.cpp WriteDXF.

#include "dxf_io.h"
#include "utf8_io.h"
#include "document.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// ================================================================
// DXF group code reader — reads two lines at a time (code, value)
// ================================================================

struct DxfPair {
    int code;
    char value[512];
};

static bool readPair(FILE *fp, DxfPair &pair)
{
    char s[512];
    if (fgets(s, sizeof(s), fp) == NULL) return false;
    pair.code = atoi(s);
    if (fgets(pair.value, sizeof(pair.value), fp) == NULL) return false;
    // Strip trailing newline/CR
    int len = (int)strlen(pair.value);
    while (len > 0 && (pair.value[len-1] == '\n' || pair.value[len-1] == '\r'))
        pair.value[--len] = 0;
    return true;
}

static double dval(const DxfPair &p) { return atof(p.value); }
static int ival(const DxfPair &p) { return atoi(p.value); }
static const char *sval(const DxfPair &p) { return p.value; }

// ================================================================
// Helper: add a node, returning its index (reuses existing if close)
// ================================================================

static int addOrFindNode(DocumentBase &doc, double x, double y, double tol)
{
    for (int i = 0; i < (int)doc.nodelist.size(); i++)
        if (doc.nodelist[i].GetDistance(x, y) < tol) return i;
    FNode n;
    n.x = x; n.y = y;
    doc.nodelist.push_back(n);
    return (int)doc.nodelist.size() - 1;
}

// ================================================================
// Helper: add arc from bulge (used by LWPOLYLINE and POLYLINE)
// Bulge = tan(included_angle / 4), positive = CCW
// ================================================================

static void addBulgeArc(DocumentBase &doc, int n0, int n1, double bulge, double tol)
{
    if (fabs(bulge) < 1e-10) {
        doc.AddSegment(n0, n1);
        return;
    }
    double x0 = doc.nodelist[n0].x, y0 = doc.nodelist[n0].y;
    double x1 = doc.nodelist[n1].x, y1 = doc.nodelist[n1].y;

    double angle = 4.0 * atan(fabs(bulge)) * 180.0 / PI;
    FArcSegment asegm;
    if (bulge > 0) { asegm.n0 = n0; asegm.n1 = n1; }
    else { asegm.n0 = n1; asegm.n1 = n0; }
    asegm.ArcLength = angle;
    asegm.MaxSideLength = 1.0;
    doc.AddArcSegment(asegm, tol);
}

// ================================================================
// ReadDXF
// ================================================================

bool ReadDXF(DocumentBase &doc, const std::string &filename, double tolerance)
{
    FILE *fp = femm_fopen(filename, "rt");
    if (!fp) return false;

    // Compute default tolerance from existing geometry
    if (tolerance <= 0) {
        double d = doc.defaultSnapDistance();
        tolerance = (d > 0) ? d : 1e-6;
    }

    DxfPair pair;

    // State for POLYLINE/VERTEX accumulation
    bool inPolyline = false;
    bool polylineClosed = false;
    struct PolyVertex { double x, y, bulge; };
    std::vector<PolyVertex> polyVerts;

    while (readPair(fp, pair)) {
        // Entity type is group code 0
        if (pair.code != 0) continue;

        // ---- LINE ----
        if (strncmp(sval(pair), "LINE", 4) == 0 && strlen(sval(pair)) == 4) {
            double x0=0, y0=0, x1=0, y1=0;
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 10: x0 = dval(pair); break;
                case 20: y0 = dval(pair); break;
                case 11: x1 = dval(pair); break;
                case 21: y1 = dval(pair); break;
                }
            }
            int n0 = addOrFindNode(doc, x0, y0, tolerance);
            int n1 = addOrFindNode(doc, x1, y1, tolerance);
            doc.AddSegment(n0, n1);
        }

        // ---- ARC ----
        else if (strncmp(sval(pair), "ARC", 3) == 0 && strlen(sval(pair)) == 3) {
            double cx=0, cy=0, R=0, a0=0, a1=0;
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 10: cx = dval(pair); break;
                case 20: cy = dval(pair); break;
                case 40: R = dval(pair); break;
                case 50: a0 = dval(pair); break;
                case 51: a1 = dval(pair); break;
                }
            }
            double x0 = cx + R * cos(a0 * PI / 180.0);
            double y0 = cy + R * sin(a0 * PI / 180.0);
            double x1 = cx + R * cos(a1 * PI / 180.0);
            double y1 = cy + R * sin(a1 * PI / 180.0);
            double angle = a1 - a0;
            if (angle < 0) angle += 360.0;

            int n0 = addOrFindNode(doc, x0, y0, tolerance);
            int n1 = addOrFindNode(doc, x1, y1, tolerance);
            FArcSegment asegm;
            asegm.n0 = n0; asegm.n1 = n1;
            asegm.ArcLength = angle;
            asegm.MaxSideLength = 1.0;
            doc.AddArcSegment(asegm, tolerance);
        }

        // ---- CIRCLE ----
        else if (strncmp(sval(pair), "CIRCLE", 6) == 0) {
            double cx=0, cy=0, R=0;
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 10: cx = dval(pair); break;
                case 20: cy = dval(pair); break;
                case 40: R = dval(pair); break;
                }
            }
            // Circle → two 180° arcs
            int n0 = addOrFindNode(doc, cx + R, cy, tolerance);
            int n1 = addOrFindNode(doc, cx - R, cy, tolerance);
            FArcSegment a1, a2;
            a1.n0 = n0; a1.n1 = n1; a1.ArcLength = 180.0; a1.MaxSideLength = 1.0;
            a2.n0 = n1; a2.n1 = n0; a2.ArcLength = 180.0; a2.MaxSideLength = 1.0;
            doc.AddArcSegment(a1, tolerance);
            doc.AddArcSegment(a2, tolerance);
        }

        // ---- LWPOLYLINE ----
        else if (strncmp(sval(pair), "LWPOLYLINE", 10) == 0) {
            int closed = 0;
            std::vector<PolyVertex> verts;
            PolyVertex cur = {0, 0, 0};
            bool firstVertex = true;

            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 90: break;  // vertex count — derived from verts vector instead
                case 70: closed = ival(pair) & 1; break;
                case 10:
                    if (!firstVertex) verts.push_back(cur);
                    cur = {dval(pair), 0, 0};
                    firstVertex = false;
                    break;
                case 20: cur.y = dval(pair); break;
                case 42: cur.bulge = dval(pair); break;
                }
            }
            if (!firstVertex) verts.push_back(cur);

            // Add segments/arcs between consecutive vertices
            std::vector<int> nodeIds;
            for (auto &v : verts)
                nodeIds.push_back(addOrFindNode(doc, v.x, v.y, tolerance));

            for (int i = 0; i + 1 < (int)verts.size(); i++)
                addBulgeArc(doc, nodeIds[i], nodeIds[i+1], verts[i].bulge, tolerance);
            if (closed && verts.size() >= 2)
                addBulgeArc(doc, nodeIds.back(), nodeIds[0], verts.back().bulge, tolerance);
        }

        // ---- POLYLINE (old-style) ----
        else if (strncmp(sval(pair), "POLYLINE", 8) == 0 && strlen(sval(pair)) == 8) {
            inPolyline = true;
            polylineClosed = false;
            polyVerts.clear();
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                if (pair.code == 70) polylineClosed = (ival(pair) & 1) != 0;
            }
        }

        // ---- VERTEX (within POLYLINE) ----
        else if (strncmp(sval(pair), "VERTEX", 6) == 0 && inPolyline) {
            PolyVertex v = {0, 0, 0};
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 10: v.x = dval(pair); break;
                case 20: v.y = dval(pair); break;
                case 42: v.bulge = dval(pair); break;
                }
            }
            polyVerts.push_back(v);
        }

        // ---- SEQEND (end of POLYLINE) ----
        else if (strncmp(sval(pair), "SEQEND", 6) == 0 && inPolyline) {
            inPolyline = false;
            std::vector<int> nodeIds;
            for (auto &v : polyVerts)
                nodeIds.push_back(addOrFindNode(doc, v.x, v.y, tolerance));
            for (int i = 0; i + 1 < (int)polyVerts.size(); i++)
                addBulgeArc(doc, nodeIds[i], nodeIds[i+1], polyVerts[i].bulge, tolerance);
            if (polylineClosed && polyVerts.size() >= 2)
                addBulgeArc(doc, nodeIds.back(), nodeIds[0], polyVerts.back().bulge, tolerance);
            polyVerts.clear();
        }

        // ---- ELLIPSE ----
        else if (strncmp(sval(pair), "ELLIPSE", 7) == 0) {
            double cx=0, cy=0, ex=0, ey=0, ratio=1, startP=0, endP=2*PI;
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 10: cx = dval(pair); break;
                case 20: cy = dval(pair); break;
                case 11: ex = dval(pair); break;  // major axis endpoint (relative)
                case 21: ey = dval(pair); break;
                case 40: ratio = dval(pair); break; // minor/major ratio
                case 41: startP = dval(pair); break;
                case 42: endP = dval(pair); break;
                }
            }
            // Approximate ellipse as polyline segments
            double a = sqrt(ex*ex + ey*ey);  // semi-major axis
            double b = a * ratio;             // semi-minor axis
            double rot = atan2(ey, ex);       // rotation angle

            int nsegs = 36;
            double dt = (endP - startP) / nsegs;
            int prevNode = -1;
            int firstNode = -1;
            for (int i = 0; i <= nsegs; i++) {
                double t = startP + dt * i;
                double px = cx + a * cos(t) * cos(rot) - b * sin(t) * sin(rot);
                double py = cy + a * cos(t) * sin(rot) + b * sin(t) * cos(rot);
                int ni = addOrFindNode(doc, px, py, tolerance);
                if (i == 0) firstNode = ni;
                if (prevNode >= 0 && prevNode != ni)
                    doc.AddSegment(prevNode, ni);
                prevNode = ni;
            }
            // Close if full ellipse
            if (fabs(endP - startP - 2*PI) < 0.01 && firstNode >= 0 && prevNode != firstNode)
                doc.AddSegment(prevNode, firstNode);
        }

        // ---- SPLINE ----
        else if (strncmp(sval(pair), "SPLINE", 6) == 0) {
            // Approximate spline using fit points (group 11/21) if available,
            // or control points (group 10/20) as fallback
            std::vector<std::pair<double,double>> fitPts, ctrlPts;
            int closed = 0;
            while (readPair(fp, pair)) {
                if (pair.code == 0) { fseek(fp, -(long)(strlen(pair.value)+6), SEEK_CUR); break; }
                switch (pair.code) {
                case 70: closed = ival(pair) & 1; break;
                case 10: ctrlPts.push_back({dval(pair), 0}); break;
                case 20: if (!ctrlPts.empty()) ctrlPts.back().second = dval(pair); break;
                case 11: fitPts.push_back({dval(pair), 0}); break;
                case 21: if (!fitPts.empty()) fitPts.back().second = dval(pair); break;
                }
            }
            // Prefer fit points if available
            auto &pts = fitPts.empty() ? ctrlPts : fitPts;
            int prevNode = -1;
            int firstNode = -1;
            for (auto &p : pts) {
                int ni = addOrFindNode(doc, p.first, p.second, tolerance);
                if (firstNode < 0) firstNode = ni;
                if (prevNode >= 0 && prevNode != ni)
                    doc.AddSegment(prevNode, ni);
                prevNode = ni;
            }
            if (closed && firstNode >= 0 && prevNode != firstNode)
                doc.AddSegment(prevNode, firstNode);
        }
    }

    fclose(fp);
    return true;
}

// ================================================================
// WriteDXF — ported from original FEMM
// ================================================================

bool WriteDXF(const DocumentBase &doc, const std::string &filename)
{
    FILE *fp = femm_fopen(filename, "wt");
    if (!fp) return false;

    // Compute extents
    double extminx=0, extminy=0, extmaxx=1, extmaxy=1;
    if (doc.nodelist.size() >= 2) {
        extminx = extmaxx = doc.nodelist[0].x;
        extminy = extmaxy = doc.nodelist[0].y;
        for (auto &n : doc.nodelist) {
            if (n.x < extminx) extminx = n.x;
            if (n.x > extmaxx) extmaxx = n.x;
            if (n.y < extminy) extminy = n.y;
            if (n.y > extmaxy) extmaxy = n.y;
        }
    }
    double R = 0.025 * sqrt((extmaxx-extminx)*(extmaxx-extminx) +
                             (extmaxy-extminy)*(extmaxy-extminy));

    // Header
    fprintf(fp, "  0\nSECTION\n  2\nHEADER\n  9\n");
    fprintf(fp, "$INSBASE\n 10\n0.0\n 20\n0.0\n  9\n");
    fprintf(fp, "$EXTMIN\n 10\n%.17g\n 20\n%.17g\n  9\n", extminx-R, extminy-R);
    fprintf(fp, "$EXTMAX\n 10\n%.17g\n 20\n%.17g\n  9\n", extmaxx+R, extmaxy+R);
    fprintf(fp, "$LIMMIN\n 10\n%.17g\n 20\n%.17g\n  9\n", extminx-R, extminy-R);
    fprintf(fp, "$LIMMAX\n 10\n%.17g\n 20\n%.17g\n  9\n", extmaxx+R, extmaxy+R);
    fprintf(fp, "$TEXTSTYLE\n  7\nSTANDARD\n  9\n$CLAYER\n");
    fprintf(fp, "  8\ndefault\n  0\nENDSEC\n  0\n");

    // Tables (minimal)
    fprintf(fp, "SECTION\n  2\nTABLES\n  0\n");
    fprintf(fp, "TABLE\n  2\nLTYPE\n 70\n1\n  0\n");
    fprintf(fp, "LTYPE\n  2\nCONTINUOUS\n 70\n64\n  3\nSolid line\n 72\n65\n 73\n0\n 40\n0.0\n  0\nENDTAB\n  0\n");
    fprintf(fp, "TABLE\n  2\nLAYER\n 70\n1\n  0\n");
    fprintf(fp, "LAYER\n  2\ndefault\n 70\n64\n 62\n7\n  6\nCONTINUOUS\n  0\nENDTAB\n  0\n");
    fprintf(fp, "TABLE\n  2\nSTYLE\n 70\n0\n  0\nENDTAB\n  0\n");
    fprintf(fp, "TABLE\n  2\nVIEW\n 70\n0\n  0\nENDTAB\n  0\n");
    fprintf(fp, "ENDSEC\n  0\n");

    // Blocks (empty)
    fprintf(fp, "SECTION\n  2\nBLOCKS\n  0\nENDSEC\n  0\n");

    // Entities
    fprintf(fp, "SECTION\n  2\nENTITIES\n  0\n");

    // Write LINE entities for segments
    for (auto &seg : doc.linelist) {
        if (seg.n0 < 0 || seg.n0 >= (int)doc.nodelist.size()) continue;
        if (seg.n1 < 0 || seg.n1 >= (int)doc.nodelist.size()) continue;
        fprintf(fp, "LINE\n  8\ndefault\n");
        fprintf(fp, " 10\n%.17g\n 20\n%.17g\n 30\n0.0\n",
            doc.nodelist[seg.n0].x, doc.nodelist[seg.n0].y);
        fprintf(fp, " 11\n%.17g\n 21\n%.17g\n 31\n0.0\n  0\n",
            doc.nodelist[seg.n1].x, doc.nodelist[seg.n1].y);
    }

    // Write ARC entities for arc segments
    for (auto &arc : doc.arclist) {
        if (arc.n0 < 0 || arc.n0 >= (int)doc.nodelist.size()) continue;
        if (arc.n1 < 0 || arc.n1 >= (int)doc.nodelist.size()) continue;

        CComplex c;
        double radius;
        doc.GetCircle(arc, c, radius);

        CComplex p0(doc.nodelist[arc.n0].x, doc.nodelist[arc.n0].y);
        CComplex p1(doc.nodelist[arc.n1].x, doc.nodelist[arc.n1].y);
        double a0 = atan2((p0 - c).im, (p0 - c).re) * 180.0 / PI;
        double a1 = atan2((p1 - c).im, (p1 - c).re) * 180.0 / PI;
        if (a0 < 0) a0 += 360.0;
        if (a1 < 0) a1 += 360.0;

        fprintf(fp, "ARC\n  8\ndefault\n");
        fprintf(fp, " 10\n%.17g\n 20\n%.17g\n 30\n0.0\n 40\n%.17g\n 50\n%.17g\n 51\n%.17g\n  0\n",
            c.re, c.im, radius, a0, a1);
    }

    fprintf(fp, "ENDSEC\n  0\nEOF\n");
    fclose(fp);
    return true;
}
