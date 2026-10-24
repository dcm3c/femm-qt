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

// One pair of lookahead. Each entity's loop reads until the group code 0 that
// starts the NEXT entity, and must hand that pair back to the main loop. That
// was done by seeking back strlen(value)+6 bytes, a guess at the length of the
// two lines on disk -- right only for AutoCAD's own "  0" + CRLF layout. With
// LF line endings, or an unindented "0", it landed elsewhere: every entity
// after the first was silently dropped, and with both (and short values) it
// could seek back past the pair it had just read and re-read the same entity
// forever. Keeping the pair
// in memory works for any layout and needs no seeking at all.
struct DxfReader {
    FILE *fp;
    bool pending = false;
    DxfPair saved;
};

static void unreadPair(DxfReader &rd, const DxfPair &pair)
{
    rd.saved = pair;
    rd.pending = true;
}

static bool readPair(DxfReader &rd, DxfPair &pair)
{
    if (rd.pending) {
        pair = rd.saved;
        rd.pending = false;
        return true;
    }
    FILE *fp = rd.fp;
    char s[512];
    if (fgets(s, sizeof(s), fp) == NULL) return false;
    pair.code = atoi(s);
    if (fgets(pair.value, sizeof(pair.value), fp) == NULL) return false;
    // Strip the line ending and any trailing padding; entity and layer names
    // are then compared exactly
    int len = (int)strlen(pair.value);
    while (len > 0 && (pair.value[len-1] == '\n' || pair.value[len-1] == '\r' ||
                       pair.value[len-1] == ' ' || pair.value[len-1] == '\t'))
        pair.value[--len] = 0;
    return true;
}

static double dval(const DxfPair &p) { return atof(p.value); }
static int ival(const DxfPair &p) { return atoi(p.value); }
static const char *sval(const DxfPair &p) { return p.value; }

// ================================================================
// Helper: add a node, returning its index (reuses existing if close)
// ================================================================

static int addOrFindNode(DocumentBase &doc, double x, double y, double tol, int grp)
{
    for (int i = 0; i < (int)doc.nodelist.size(); i++)
        if (doc.nodelist[i].GetDistance(x, y) < tol) return i;
    FNode n;
    n.x = x; n.y = y;
    n.InGroup = grp;
    doc.nodelist.push_back(n);
    return (int)doc.nodelist.size() - 1;
}

static void addLine(DocumentBase &doc, int n0, int n1, int grp)
{
    FSegment segm;
    segm.BoundaryMarker = "<None>";
    segm.InGroup = grp;
    doc.AddSegment(n0, n1, &segm);
}

// FEMM 4.2 imports every arc with a maximum segment of 5 degrees
// (bd_movecopy.cpp); 1 degree meshed each curved edge five times as finely.
static const double kArcMaxSeg = 5.0;

// ================================================================
// Helper: add a CCW arc from n0 to n1, split in two if it exceeds 180 deg
// ================================================================
//
// FEMM's arc model only holds arcs of 180 degrees or less. GetCircle puts the
// centre on the left of the chord, and the arg()-based tests (the split in
// AddNode, GetLineArcIntersection, ShortestDistanceFromArc) read angles in
// (-180, 180]. A longer arc is drawn about the wrong centre, and the first
// line that crosses it splits it into pieces of the wrong length, one of them
// negative. Reported from the field: a 292.6 degree ARC came in as a 67 degree
// arc plus two slivers. FEMM 4.2's reader halves such arcs on import
// (bd_movecopy.cpp, the ARC branch), and its arc dialog caps the angle at 180;
// so does ours (gui/arc_dialog.cpp). This is the importer's half of that.
static void addArcCCW(DocumentBase &doc, int n0, int n1, double angle, double tol, int grp)
{
    FArcSegment asegm;
    asegm.MaxSideLength = kArcMaxSeg;
    asegm.InGroup = grp;
    if (angle > 180.0) {
        // the arc's midpoint: from the chord's midpoint, out along the right-
        // hand normal by the sagitta, (chord/2)*tan(angle/4)
        CComplex a0 = doc.nodelist[n0].CC(), a1 = doc.nodelist[n1].CC();
        CComplex m = (a0 + a1) / 2.0 - CComplex(0, 1) * ((a1 - a0) / 2.0) * tan(angle * PI / 720.0);
        int nm = addOrFindNode(doc, m.re, m.im, tol, grp);
        asegm.n0 = n0; asegm.n1 = nm; asegm.ArcLength = angle / 2.0;
        doc.AddArcSegment(asegm, tol);
        asegm.n0 = nm; asegm.n1 = n1; asegm.ArcLength = angle / 2.0;
        doc.AddArcSegment(asegm, tol);
        return;
    }
    asegm.n0 = n0; asegm.n1 = n1;
    asegm.ArcLength = angle;
    doc.AddArcSegment(asegm, tol);
}

// ================================================================
// Helper: add arc from bulge (used by LWPOLYLINE and POLYLINE)
// Bulge = tan(included_angle / 4), positive = CCW; |bulge| > 1 is past 180
// ================================================================

static void addBulgeArc(DocumentBase &doc, int n0, int n1, double bulge, double tol, int grp)
{
    if (fabs(bulge) < 1e-10) {
        addLine(doc, n0, n1, grp);
        return;
    }
    double angle = 4.0 * atan(fabs(bulge)) * 180.0 / PI;
    if (bulge > 0) addArcCCW(doc, n0, n1, angle, tol, grp);
    else addArcCCW(doc, n1, n0, angle, tol, grp);
}

// ================================================================
// ReadDXF
// ================================================================

bool ReadDXF(DocumentBase &doc, const std::string &filename, double tolerance)
{
    FILE *fp = femm_fopen(filename, "rt");
    if (!fp) return false;
    DxfReader rd;
    rd.fp = fp;

    // Compute default tolerance from existing geometry
    if (tolerance <= 0) {
        double d = doc.defaultSnapDistance();
        tolerance = (d > 0) ? d : 1e-6;
    }

    DxfPair pair;

    // Layers become groups, numbered by their order in the LAYER table, as in
    // FEMM 4.2: an entity on the n-th layer listed goes in group n, and one on
    // a layer the table does not list goes in group 0.
    std::vector<std::string> layers;
    auto groupOf = [&layers](const char *name) {
        for (int j = 0; j < (int)layers.size(); j++)
            if (layers[j] == name) return j;
        return 0;
    };

    // Only the ENTITIES section is drawing geometry. The BLOCKS section holds
    // block DEFINITIONS -- the lines and arcs of every dimension, hatch and
    // inserted symbol -- at their own coordinates, and importing those put
    // stray arrowheads and leaders into the model. A file with no SECTION
    // markers at all is read whole.
    std::string section;
    bool sawSection = false;

    // State for POLYLINE/VERTEX accumulation
    bool inPolyline = false;
    bool polylineClosed = false;
    int polylineGroup = 0;
    struct PolyVertex { double x, y, bulge; };
    std::vector<PolyVertex> polyVerts;

    while (readPair(rd, pair)) {
        // Entity type is group code 0
        if (pair.code != 0) continue;

        if (strcmp(sval(pair), "SECTION") == 0) {
            sawSection = true;
            section.clear();
            if (readPair(rd, pair)) {
                if (pair.code == 2) section = sval(pair);
                else unreadPair(rd, pair);
            }
            continue;
        }
        if (strcmp(sval(pair), "ENDSEC") == 0) { section.clear(); continue; }

        // ---- LAYER (a record of the TABLES section's LAYER table) ----
        if (strcmp(sval(pair), "LAYER") == 0 && (!sawSection || section == "TABLES")) {
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                if (pair.code == 2) layers.push_back(sval(pair));
            }
            continue;
        }

        if (sawSection && section != "ENTITIES") continue;
        int grp = 0;

        // ---- POINT ----
        if (strcmp(sval(pair), "POINT") == 0) {
            double x=0, y=0;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
                case 10: x = dval(pair); break;
                case 20: y = dval(pair); break;
                }
            }
            addOrFindNode(doc, x, y, tolerance, grp);
        }

        // ---- LINE ----
        else if (strcmp(sval(pair), "LINE") == 0) {
            double x0=0, y0=0, x1=0, y1=0;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
                case 10: x0 = dval(pair); break;
                case 20: y0 = dval(pair); break;
                case 11: x1 = dval(pair); break;
                case 21: y1 = dval(pair); break;
                }
            }
            int n0 = addOrFindNode(doc, x0, y0, tolerance, grp);
            int n1 = addOrFindNode(doc, x1, y1, tolerance, grp);
            addLine(doc, n0, n1, grp);
        }

        // ---- ARC ----
        else if (strcmp(sval(pair), "ARC") == 0) {
            double cx=0, cy=0, R=0, a0=0, a1=0;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
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

            int n0 = addOrFindNode(doc, x0, y0, tolerance, grp);
            int n1 = addOrFindNode(doc, x1, y1, tolerance, grp);
            addArcCCW(doc, n0, n1, angle, tolerance, grp);
        }

        // ---- CIRCLE ----
        else if (strcmp(sval(pair), "CIRCLE") == 0) {
            double cx=0, cy=0, R=0;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
                case 10: cx = dval(pair); break;
                case 20: cy = dval(pair); break;
                case 40: R = dval(pair); break;
                }
            }
            // Circle → two 180° arcs
            int n0 = addOrFindNode(doc, cx + R, cy, tolerance, grp);
            int n1 = addOrFindNode(doc, cx - R, cy, tolerance, grp);
            addArcCCW(doc, n0, n1, 180.0, tolerance, grp);
            addArcCCW(doc, n1, n0, 180.0, tolerance, grp);
        }

        // ---- LWPOLYLINE ----
        else if (strcmp(sval(pair), "LWPOLYLINE") == 0) {
            int closed = 0;
            std::vector<PolyVertex> verts;
            PolyVertex cur = {0, 0, 0};
            bool firstVertex = true;

            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
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
                nodeIds.push_back(addOrFindNode(doc, v.x, v.y, tolerance, grp));

            for (int i = 0; i + 1 < (int)verts.size(); i++)
                addBulgeArc(doc, nodeIds[i], nodeIds[i+1], verts[i].bulge, tolerance, grp);
            if (closed && verts.size() >= 2)
                addBulgeArc(doc, nodeIds.back(), nodeIds[0], verts.back().bulge, tolerance, grp);
        }

        // ---- POLYLINE (old-style) ----
        else if (strcmp(sval(pair), "POLYLINE") == 0) {
            inPolyline = true;
            polylineClosed = false;
            polylineGroup = 0;
            polyVerts.clear();
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                if (pair.code == 8) polylineGroup = groupOf(sval(pair));
                if (pair.code == 70) polylineClosed = (ival(pair) & 1) != 0;
            }
        }

        // ---- VERTEX (within POLYLINE) ----
        else if (strcmp(sval(pair), "VERTEX") == 0 && inPolyline) {
            PolyVertex v = {0, 0, 0};
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 10: v.x = dval(pair); break;
                case 20: v.y = dval(pair); break;
                case 42: v.bulge = dval(pair); break;
                }
            }
            polyVerts.push_back(v);
        }

        // ---- SEQEND (end of POLYLINE) ----
        else if (strcmp(sval(pair), "SEQEND") == 0 && inPolyline) {
            inPolyline = false;
            grp = polylineGroup;
            std::vector<int> nodeIds;
            for (auto &v : polyVerts)
                nodeIds.push_back(addOrFindNode(doc, v.x, v.y, tolerance, grp));
            for (int i = 0; i + 1 < (int)polyVerts.size(); i++)
                addBulgeArc(doc, nodeIds[i], nodeIds[i+1], polyVerts[i].bulge, tolerance, grp);
            if (polylineClosed && polyVerts.size() >= 2)
                addBulgeArc(doc, nodeIds.back(), nodeIds[0], polyVerts.back().bulge, tolerance, grp);
            polyVerts.clear();
        }

        // ---- ELLIPSE ----
        else if (strcmp(sval(pair), "ELLIPSE") == 0) {
            double cx=0, cy=0, ex=0, ey=0, ratio=1, startP=0, endP=2*PI;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
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
                int ni = addOrFindNode(doc, px, py, tolerance, grp);
                if (i == 0) firstNode = ni;
                if (prevNode >= 0 && prevNode != ni)
                    addLine(doc, prevNode, ni, grp);
                prevNode = ni;
            }
            // Close if full ellipse
            if (fabs(endP - startP - 2*PI) < 0.01 && firstNode >= 0 && prevNode != firstNode)
                addLine(doc, prevNode, firstNode, grp);
        }

        // ---- SPLINE ----
        else if (strcmp(sval(pair), "SPLINE") == 0) {
            // Approximate spline using fit points (group 11/21) if available,
            // or control points (group 10/20) as fallback
            std::vector<std::pair<double,double>> fitPts, ctrlPts;
            int closed = 0;
            while (readPair(rd, pair)) {
                if (pair.code == 0) { unreadPair(rd, pair); break; }
                switch (pair.code) {
                case 8: grp = groupOf(sval(pair)); break;
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
                int ni = addOrFindNode(doc, p.first, p.second, tolerance, grp);
                if (firstNode < 0) firstNode = ni;
                if (prevNode >= 0 && prevNode != ni)
                    addLine(doc, prevNode, ni, grp);
                prevNode = ni;
            }
            if (closed && firstNode >= 0 && prevNode != firstNode)
                addLine(doc, prevNode, firstNode, grp);
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
