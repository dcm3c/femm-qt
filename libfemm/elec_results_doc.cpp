// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_results_doc.cpp — Electrostatics postprocessor solution document
// Reads .res files and performs full post-load processing.
// Ported from femm42src/femm/belaviewDoc.cpp and bv_makemask.cpp.

#include "elec_results_doc.h"
#include "utf8_io.h"
#include "fknDlg.h"   // CFknDlg (needed for MakeMask PCG solver progress)
#include "spars.h"     // CBigLinProb (sparse matrix for MakeMask Laplace solve)
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

static double sqr(double x) { return x * x; }

static const double LengthConvTable[6] = {
    0.0254,   // inches
    0.001,    // mm
    0.01,     // cm
    1.0,      // meters
    2.54e-05, // mils
    1.e-06    // microns
};

double ElecResultsDoc::LengthConv() const
{
    if (LengthUnits >= 0 && LengthUnits < 6)
        return LengthConvTable[LengthUnits];
    return 0.001;
}

// --- File parsing helpers ---
static char *StripKey(char *s)
{
    char *p = strchr(s, '=');
    if (p) return p + 1;
    return s;
}

static std::string ExtractQuoted(char *v)
{
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

#ifdef _WIN32
#define strnicmp _strnicmp
#else
#define strnicmp strncasecmp
#endif

ElecResultsDoc::~ElecResultsDoc() {}

// ================================================================
// OpenDocument — read .res file
// Ported from belaviewDoc::OnOpenDocument
// ================================================================

bool ElecResultsDoc::OpenDocument(const std::string &pathname)
{
    FILE *fp;
    int i, j, k, t;
    char s[1024], q[1024];
    char *v;
    double b;
    bool flag = false;

    EPointProp PProp;
    EBoundaryProp BProp;
    ERMaterialProp MProp;
    ERCircuit CProp;
    FNode node;
    FSegment segm;
    FArcSegment asegm;
    ERElement elm;
    ERBlockLabel blk;
    ERMeshNode mnode;

    // Clear existing data
    nodelist.clear();
    linelist.clear();
    arclist.clear();
    labellist.clear();
    nodeproplist.clear();
    blockproplist.clear();
    circproplist.clear();
    meshnode.clear();
    meshelem.clear();
    contour.clear();

    fp = femm_fopen(pathname, "rt");
    if (fp == nullptr) return false;

    filePath = pathname;

    // Define defaults
    ProblemType = 0;
    Coords = 0;
    bHasMask = false;
    Depth = -1;
    extRo = extRi = extZo = 0;

    // Parse the header
    while (!flag) {
        if (fgets(s, 1024, fp) == nullptr) break;
        sscanf(s, "%s", q);

        // Format version
        if (strnicmp(q, "[format]", 8) == 0) {
            q[0] = 0;
        }

        // Depth
        if (strnicmp(q, "[depth]", 7) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &Depth);
            q[0] = 0;
        }

        // Length units
        if (strnicmp(q, "[lengthunits]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (strnicmp(q, "inches", 6) == 0) LengthUnits = 0;
            else if (strnicmp(q, "millimeters", 11) == 0) LengthUnits = 1;
            else if (strnicmp(q, "centimeters", 1) == 0) LengthUnits = 2;
            else if (strnicmp(q, "mils", 4) == 0) LengthUnits = 4;
            else if (strnicmp(q, "microns", 6) == 0) LengthUnits = 5;
            else if (strnicmp(q, "meters", 6) == 0) LengthUnits = 3;
            q[0] = 0;
        }

        // Problem type
        if (strnicmp(q, "[problemtype]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (strnicmp(q, "planar", 6) == 0) ProblemType = 0;
            if (strnicmp(q, "axisymmetric", 3) == 0) ProblemType = 1;
            q[0] = 0;
        }

        // Coordinates
        if (strnicmp(q, "[coordinates]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (strnicmp(q, "cartesian", 4) == 0) Coords = 0;
            if (strnicmp(q, "polar", 5) == 0) Coords = 1;
            q[0] = 0;
        }

        // Comment (skip)
        if (strnicmp(q, "[comment]", 9) == 0) {
            q[0] = 0;
        }

        // External region parameters
        if (strnicmp(q, "[extzo]", 7) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &extZo);
            q[0] = 0;
        }
        if (strnicmp(q, "[extro]", 7) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &extRo);
            q[0] = 0;
        }
        if (strnicmp(q, "[extri]", 7) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &extRi);
            q[0] = 0;
        }

        // Point properties
        if (strnicmp(q, "<beginpoint>", 11) == 0) {
            PProp.PointName = "New Point Property";
            PProp.V = PProp.qp = 0;
            q[0] = 0;
        }
        if (strnicmp(q, "<pointname>", 11) == 0) {
            v = StripKey(s);
            PProp.PointName = ExtractQuoted(v);
            q[0] = 0;
        }
        if (strnicmp(q, "<vp>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &PProp.V);
            q[0] = 0;
        }
        if (strnicmp(q, "<qp>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &PProp.qp);
            q[0] = 0;
        }
        if (strnicmp(q, "<endpoint>", 9) == 0) {
            nodeproplist.push_back(PProp);
            q[0] = 0;
        }

        // Boundary properties (read but not stored in results doc —
        // only needed during pre-processing; skip storage)
        if (strnicmp(q, "<beginbdry>", 11) == 0) {
            BProp.BdryName = "New Boundary";
            BProp.BdryFormat = 0;
            BProp.V = BProp.qs = 0;
            BProp.c0 = BProp.c1 = 0;
            q[0] = 0;
        }
        if (strnicmp(q, "<bdryname>", 10) == 0) {
            v = StripKey(s);
            BProp.BdryName = ExtractQuoted(v);
            q[0] = 0;
        }
        if (strnicmp(q, "<bdrytype>", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &BProp.BdryFormat);
            q[0] = 0;
        }
        if (strnicmp(q, "<vs>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &BProp.V);
            q[0] = 0;
        }
        if (strnicmp(q, "<qs>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &BProp.qs);
            q[0] = 0;
        }
        if (strnicmp(q, "<c0>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &BProp.c0);
            q[0] = 0;
        }
        if (strnicmp(q, "<c1>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &BProp.c1);
            q[0] = 0;
        }
        if (strnicmp(q, "<endbdry>", 9) == 0) {
            // lineproplist not stored in results doc
            q[0] = 0;
        }

        // Block (material) properties
        if (strnicmp(q, "<beginblock>", 12) == 0) {
            MProp.BlockName = "New Material";
            MProp.ex = 1.0;
            MProp.ey = 1.0;
            MProp.qv = 0.0;
            q[0] = 0;
        }
        if (strnicmp(q, "<blockname>", 10) == 0) {
            v = StripKey(s);
            MProp.BlockName = ExtractQuoted(v);
            q[0] = 0;
        }
        if (strnicmp(q, "<ex>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &MProp.ex);
            q[0] = 0;
        }
        if (strnicmp(q, "<ey>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &MProp.ey);
            q[0] = 0;
        }
        if (strnicmp(q, "<qv>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &MProp.qv);
            q[0] = 0;
        }
        if (strnicmp(q, "<endblock>", 9) == 0) {
            blockproplist.push_back(MProp);
            q[0] = 0;
        }

        // Conductor (circuit) properties
        if (strnicmp(q, "<beginconductor>", 16) == 0) {
            CProp.CircName = "New Circuit";
            CProp.V = CProp.q = 0;
            CProp.CircType = 0;
            q[0] = 0;
        }
        if (strnicmp(q, "<conductorname>", 15) == 0) {
            v = StripKey(s);
            CProp.CircName = ExtractQuoted(v);
            q[0] = 0;
        }
        if (strnicmp(q, "<vc>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &CProp.V);
            q[0] = 0;
        }
        if (strnicmp(q, "<qc>", 4) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &CProp.q);
            q[0] = 0;
        }
        if (strnicmp(q, "<conductortype>", 15) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &CProp.CircType);
            q[0] = 0;
        }
        if (strnicmp(q, "<endconductor>", 14) == 0) {
            circproplist.push_back(CProp);
            q[0] = 0;
        }

        // Points list
        if (strnicmp(q, "[numpoints]", 11) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                int ncond = 0;
                sscanf(s, "%lf\t%lf\t%i\t%i\t%i", &node.x, &node.y, &t, &node.InGroup, &ncond);
                // BoundaryMarker stored as string in FNode; skip numeric marker
                node.InConductor = (ncond > 0 && ncond <= (int)circproplist.size())
                                 ? circproplist[ncond - 1].CircName : std::string();
                nodelist.push_back(node);
            }
            q[0] = 0;
        }

        // Segments
        if (strnicmp(q, "[numsegments]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                int hidden = 0, ingroup = 0, scond = 0;
                sscanf(s, "%i\t%i\t%lf\t%i\t%i\t%i\t%i",
                       &segm.n0, &segm.n1, &segm.MaxSideLength, &t, &hidden, &ingroup, &scond);
                segm.Hidden = (hidden != 0);
                segm.InGroup = ingroup;
                segm.InConductor = (scond > 0 && scond <= (int)circproplist.size())
                                 ? circproplist[scond - 1].CircName : std::string();
                linelist.push_back(segm);
            }
            q[0] = 0;
        }

        // Arc segments
        if (strnicmp(q, "[numarcsegments]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                int hidden = 0, ingroup = 0, acond = 0;
                sscanf(s, "%i\t%i\t%lf\t%lf\t%i\t%i\t%i\t%i",
                       &asegm.n0, &asegm.n1, &asegm.ArcLength,
                       &asegm.MaxSideLength, &t, &hidden, &ingroup, &acond);
                asegm.Hidden = (hidden != 0);
                asegm.InGroup = ingroup;
                asegm.InConductor = (acond > 0 && acond <= (int)circproplist.size())
                                  ? circproplist[acond - 1].CircName : std::string();
                arclist.push_back(asegm);
            }
            q[0] = 0;
        }

        // Holes (read but don't store)
        if (strnicmp(q, "[numholes]", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                // holes not stored in results doc
            }
            q[0] = 0;
        }

        // Block labels
        if (strnicmp(q, "[numblocklabels]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                int isext = 0;
                sscanf(s, "%lf\t%lf\t%i\t%lf\t%i\t%i",
                       &blk.x, &blk.y, &blk.BlockType, &blk.MaxArea,
                       &blk.InGroup, &isext);
                blk.IsDefault = (isext & 2) != 0;
                blk.IsExternal = (isext & 1) != 0;
                if (blk.MaxArea < 0) blk.MaxArea = 0;
                else blk.MaxArea = PI * blk.MaxArea * blk.MaxArea / 4.0;
                blk.BlockType -= 1;
                labellist.push_back(blk);
            }
            q[0] = 0;
        }

        // Solution marker — end of header
        if (strnicmp(q, "[solution]", 10) == 0) {
            flag = true;
            q[0] = 0;
        }
    }

    // Read mesh nodes
    fscanf(fp, "%i\n", &k);
    meshnode.resize(k);
    for (i = 0; i < k; i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%lf\t%lf\t%lf\t%i", &meshnode[i].x, &meshnode[i].y,
               &meshnode[i].V, &meshnode[i].Q);
    }

    // Read mesh elements
    fscanf(fp, "%i\n", &k);
    meshelem.resize(k);
    for (i = 0; i < k; i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%i\t%i\t%i\t%i",
               &meshelem[i].p[0], &meshelem[i].p[1], &meshelem[i].p[2],
               &meshelem[i].lbl);
        if (meshelem[i].lbl >= 0 && meshelem[i].lbl < (int)labellist.size())
            meshelem[i].blk = labellist[meshelem[i].lbl].BlockType;
    }

    // Read circuit data
    fscanf(fp, "%i\n", &k);
    for (i = 0; i < k && i < (int)circproplist.size(); i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%lf\t%lf", &circproplist[i].V, &circproplist[i].q);
    }

    fclose(fp);

    PostProcessing();
    return true;
}

// ================================================================
// PostProcessing — compute all derived fields after loading
// Ported from the tail of belaviewDoc::OnOpenDocument
// ================================================================

void ElecResultsDoc::PostProcessing()
{
    int i, j;
    double b;

    // Scale depth to meters
    if (Depth == -1) Depth = 1;
    else Depth *= LengthConv();

    // Element centroids and bounding radii
    for (i = 0; i < (int)meshelem.size(); i++) {
        meshelem[i].ctr = Ctr(i);
        meshelem[i].rsqr = 0;
        for (j = 0; j < 3; j++) {
            b = sqr(meshnode[meshelem[i].p[j]].x - meshelem[i].ctr.re) +
                sqr(meshnode[meshelem[i].p[j]].y - meshelem[i].ctr.im);
            if (b > meshelem[i].rsqr) meshelem[i].rsqr = b;
        }
    }

    // Find displacement in each element
    for (i = 0; i < (int)meshelem.size(); i++) GetElementD(i);

    // Find extreme values of V
    if (!meshnode.empty()) {
        A_Low = meshnode[0].V;
        A_High = meshnode[0].V;
        for (i = 1; i < (int)meshnode.size(); i++) {
            if (meshnode[i].V > A_High) A_High = meshnode[i].V;
            if (meshnode[i].V < A_Low) A_Low = meshnode[i].V;
        }
    }
    A_lb = A_Low;
    A_ub = A_High;

    // Build connectivity (needed for smoothing and boundary edges)
    BuildConnectivity();

    // Find boundary edges
    FindBoundaryEdges();

    // Plot bounds for V
    d_PlotBounds[0][0] = A_Low;
    d_PlotBounds[0][1] = A_High;
    PlotBounds[0][0] = d_PlotBounds[0][0];
    PlotBounds[0][1] = d_PlotBounds[0][1];

    // Compute smoothed nodal D
    for (i = 0; i < (int)meshelem.size(); i++)
        GetNodalD(meshelem[i].d, i);

    // Find extreme values of D and E, excluding external regions
    std::vector<short> isExt(meshelem.size(), 0);
    int extCount = 0;
    for (i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl >= 0 && meshelem[i].lbl < (int)labellist.size()) {
            if (labellist[meshelem[i].lbl].IsExternal) isExt[i] = 1;
        }
        if (meshelem[i].blk >= 0 && meshelem[i].blk < (int)blockproplist.size()) {
            std::string &nm = blockproplist[meshelem[i].blk].BlockName;
            if (nm.size() > 1 && nm[0] == 'e') {
                for (int kk = 1; kk <= 9; kk++) {
                    if (nm[1] == ('0' + kk)) { isExt[i] = 1; break; }
                }
            }
        }
        if (isExt[i]) extCount++;
    }
    // If every element is external, treat none as external
    if (extCount == (int)meshelem.size())
        std::fill(isExt.begin(), isExt.end(), 0);

    // Find first non-external element
    i = 0;
    while (i < (int)meshelem.size() && isExt[i]) i++;
    if (i < (int)meshelem.size()) {
        CComplex Eelm;

        // Collect D and E values for percentile-based bounds
        std::vector<double> dVals, eVals;
        dVals.reserve(meshelem.size());
        eVals.reserve(meshelem.size());
        for (i = 0; i < (int)meshelem.size(); i++) {
            if (isExt[i]) continue;
            dVals.push_back(abs(meshelem[i].D));
            if (meshelem[i].blk >= 0 && meshelem[i].blk < (int)blockproplist.size()) {
                Eelm = CComplex(
                    meshelem[i].D.re / blockproplist[meshelem[i].blk].ex,
                    meshelem[i].D.im / blockproplist[meshelem[i].blk].ey) / eo * AECF(i);
            }
            eVals.push_back(abs(Eelm));
        }
        std::sort(dVals.begin(), dVals.end());
        std::sort(eVals.begin(), eVals.end());

        if (!dVals.empty()) {
            d_PlotBounds[1][0] = dVals.front();
            // Use 99th percentile for upper bound to clip corner singularities
            int idx99 = (int)(dVals.size() * 0.99);
            if (idx99 >= (int)dVals.size()) idx99 = (int)dVals.size() - 1;
            d_PlotBounds[1][1] = dVals[idx99];
        }
        if (!eVals.empty()) {
            d_PlotBounds[2][0] = eVals.front();
            int idx99 = (int)(eVals.size() * 0.99);
            if (idx99 >= (int)eVals.size()) idx99 = (int)eVals.size() - 1;
            d_PlotBounds[2][1] = eVals[idx99];
        }
    }

    PlotBounds[1][0] = d_PlotBounds[1][0];
    PlotBounds[1][1] = d_PlotBounds[1][1];
    PlotBounds[2][0] = d_PlotBounds[2][0];
    PlotBounds[2][1] = d_PlotBounds[2][1];
}

// ================================================================
// GetElementD — compute constant D for element k
// Ported from belaviewDoc::GetElementD
// ================================================================

void ElecResultsDoc::GetElementD(int k)
{
    int i, n[3];
    double b[3], c[3], da;
    CComplex E(0, 0);

    for (i = 0; i < 3; i++) n[i] = meshelem[k].p[i];

    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    double lc = LengthConv();
    for (i = 0; i < 3; i++)
        E = E - meshnode[n[i]].V * CComplex(b[i], c[i]) / (da * lc);

    meshelem[k].D = eo * CComplex(
        E.re * blockproplist[meshelem[k].blk].ex,
        E.im * blockproplist[meshelem[k].blk].ey) / AECF(k);
}

// ================================================================
// GetNodalD — least-squares fit for smoothed nodal D
// Ported from belaviewDoc::GetNodalD
// ================================================================

void ElecResultsDoc::GetNodalD(CComplex *d, int N)
{
    int i, j, k, n, m, p, eos, nos, qn;
    int lf, rt;
    double xi, yi, ii, xx, xy, yy, iv, xv, yv, dx, dy, dv, Ex, Ey, det;
    int q[21];
    bool flag;

    for (i = 0; i < 3; i++) {
        j = meshelem[N].p[i];
        lf = rt = -1;
        flag = false;
        for (eos = 0; eos < NumList[j]; eos++)
            if (ConList[j][eos] == N) break;

        // scan ccw
        for (k = 0, m = eos, qn = 0; k < NumList[j]; k++) {
            n = ConList[j][m];
            if (!IsSameMaterial(n, N)) break;

            // figure out next node in ccw direction
            for (nos = 0; nos < 3; nos++)
                if (meshelem[n].p[nos] == j) break;
            if (nos == 3) break;
            nos--;
            if (nos < 0) nos = 2;
            p = meshelem[n].p[nos];

            if (qn < 20) q[qn++] = p;

            // if this is a fixed boundary, stop
            if ((meshnode[j].Q != -2) && (meshnode[p].Q != -2)) {
                rt = p;
                break;
            }

            m++;
            if (m == NumList[j]) m = 0;
        }

        // scan cw
        for (k = 0, m = eos; k < NumList[j]; k++) {
            n = ConList[j][m];
            if (!IsSameMaterial(n, N)) break;

            for (nos = 0; nos < 3; nos++)
                if (meshelem[n].p[nos] == j) break;
            if (nos == 3) break;
            nos++;
            if (nos > 2) nos = 0;
            p = meshelem[n].p[nos];

            if (qn < 20) q[qn++] = p;

            if ((meshnode[j].Q != -2) && (meshnode[p].Q != -2)) {
                lf = p;
                break;
            }

            m--;
            if (m < 0) m = NumList[j] - 1;
        }

        // Catch special cases
        if ((lf == rt) && (rt != -1) && (meshnode[j].Q != -2)) {
            d[i] = meshelem[N].D;
            flag = true;
        } else if ((rt != -1) && (meshnode[j].Q != -2) && (lf == -1)) {
            d[i] = meshelem[N].D;
            flag = true;
        } else if ((lf != -1) && (meshnode[j].Q != -2) && (rt == -1)) {
            d[i] = meshelem[N].D;
            flag = true;
        } else if ((lf == -1) && (rt == -1) && (meshnode[j].Q != -2)) {
            d[i] = meshelem[N].D;
            flag = true;
        } else if ((lf != -1) && (rt != -1) && (meshnode[j].Q != -2)) {
            // Node on boundary with fixed charge — check angle
            CComplex xc, yc;
            xc = meshnode[lf].CC() - meshnode[j].CC();
            xc = xc / abs(xc);
            yc = meshnode[j].CC() - meshnode[rt].CC();
            yc = yc / abs(yc);
            if (fabs(arg(xc / yc)) > 10.0001 * PI / 180.0) {
                d[i] = meshelem[N].D;
                flag = true;
            }
        }

        if (!flag) {
            // Nominal case: fit a plane through neighbors to solve for E,
            // then multiply by permittivity to get D.
            xi = yi = ii = xx = xy = yy = iv = xv = yv = 0;
            q[qn++] = j;

            for (k = 0; k < qn; k++) {
                dx = meshnode[q[k]].x - meshnode[j].x;
                dy = meshnode[q[k]].y - meshnode[j].y;
                dv = meshnode[j].V - meshnode[q[k]].V;

                ii += 1.0;
                xi += dx;
                yi += dy;
                xx += dx * dx;
                xy += dx * dy;
                yy += dy * dy;
                iv += dv;
                xv += dx * dv;
                yv += dy * dv;
            }
            double lc = LengthConv();
            det = (-(ii * xy * xy) + 2 * xi * xy * yi - xx * yi * yi -
                   xi * xi * yy + ii * xx * yy) * lc;

            if (det == 0) {
                d[i] = meshelem[N].D;
            } else {
                Ex = (iv * xy * yi - xv * yi * yi - ii * xy * yv +
                      xi * yi * yv - iv * xi * yy + ii * xv * yy) / det;
                Ey = (iv * xi * xy - ii * xv * xy + xi * xv * yi -
                      iv * xx * yi - xi * xi * yv + ii * xx * yv) / det;
                d[i] = CComplex(
                    blockproplist[meshelem[N].blk].ex * Ex * eo,
                    blockproplist[meshelem[N].blk].ey * Ey * eo);
                d[i] = d[i] / AECF(N, meshnode[j].CC());
            }
        }
    }
}

// ================================================================
// GetPointValues — both overloads
// Ported from belaviewDoc::GetPointValues
// ================================================================

bool ElecResultsDoc::GetPointValues(double x, double y, EPointVals &u)
{
    int k = InTriangle(x, y);
    if (k < 0) return false;
    return GetPointValues(x, y, k, u);
}

bool ElecResultsDoc::GetPointValues(double x, double y, int k, EPointVals &u)
{
    int i, n[3];
    double a[3], b[3], c[3], da;

    for (i = 0; i < 3; i++) n[i] = meshelem[k].p[i];
    a[0] = meshnode[n[1]].x * meshnode[n[2]].y - meshnode[n[2]].x * meshnode[n[1]].y;
    a[1] = meshnode[n[2]].x * meshnode[n[0]].y - meshnode[n[0]].x * meshnode[n[2]].y;
    a[2] = meshnode[n[0]].x * meshnode[n[1]].y - meshnode[n[1]].x * meshnode[n[0]].y;
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    GetPointD(x, y, u.D, meshelem[k]);
    u.e = CComplex(blockproplist[meshelem[k].blk].ex,
                    blockproplist[meshelem[k].blk].ey);
    u.e = u.e / AECF(k, CComplex(x, y));

    u.V = 0;
    for (i = 0; i < 3; i++)
        u.V += meshnode[n[i]].V * (a[i] + b[i] * x + c[i] * y) / da;

    u.E.re = u.D.re / (u.e.re * eo);
    u.E.im = u.D.im / (u.e.im * eo);

    u.nrg = Re(u.D * conj(u.E)) / 2.0;

    return true;
}

// ================================================================
// GetPointD — interpolated D at arbitrary point
// Ported from belaviewDoc::GetPointD
// ================================================================

void ElecResultsDoc::GetPointD(double x, double y, CComplex &D, ERElement &elm)
{
    int i, n[3];
    double da, a[3], b[3], c[3];

    if (!Smooth) {
        D = elm.D;
        return;
    }

    for (i = 0; i < 3; i++) n[i] = elm.p[i];
    a[0] = meshnode[n[1]].x * meshnode[n[2]].y - meshnode[n[2]].x * meshnode[n[1]].y;
    a[1] = meshnode[n[2]].x * meshnode[n[0]].y - meshnode[n[0]].x * meshnode[n[2]].y;
    a[2] = meshnode[n[0]].x * meshnode[n[1]].y - meshnode[n[1]].x * meshnode[n[0]].y;
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    D = CComplex(0, 0);
    for (i = 0; i < 3; i++)
        D = D + elm.d[i] * (a[i] + b[i] * x + c[i] * y) / da;
}

// ================================================================
// InTriangle / InTriangleTest
// Ported from belaviewDoc
// ================================================================

int ElecResultsDoc::InTriangle(double x, double y)
{
    static int k = 0;
    int j, hi, lo, sz;
    double z;

    sz = (int)meshelem.size();
    if (k < 0 || k >= sz) k = 0;

    if (InTriangleTest(x, y, k)) return k;

    hi = k;
    lo = k;

    for (j = 0; j < sz; j += 2) {
        hi++;
        if (hi >= sz) hi = 0;
        lo--;
        if (lo < 0) lo = sz - 1;

        z = sqr(meshelem[hi].ctr.re - x) + sqr(meshelem[hi].ctr.im - y);
        if (z <= meshelem[hi].rsqr) {
            if (InTriangleTest(x, y, hi)) { k = hi; return k; }
        }

        z = sqr(meshelem[lo].ctr.re - x) + sqr(meshelem[lo].ctr.im - y);
        if (z <= meshelem[lo].rsqr) {
            if (InTriangleTest(x, y, lo)) { k = lo; return k; }
        }
    }

    return -1;
}

bool ElecResultsDoc::InTriangleTest(double x, double y, int i) const
{
    if (i < 0 || i >= (int)meshelem.size()) return false;

    int j, k2;
    double z;

    for (j = 0; j < 3; j++) {
        k2 = j + 1;
        if (k2 == 3) k2 = 0;
        if (meshelem[i].p[k2] > meshelem[i].p[j]) {
            z = (meshnode[meshelem[i].p[k2]].x - meshnode[meshelem[i].p[j]].x) *
                    (y - meshnode[meshelem[i].p[j]].y) -
                (meshnode[meshelem[i].p[k2]].y - meshnode[meshelem[i].p[j]].y) *
                    (x - meshnode[meshelem[i].p[j]].x);
            if (z < 0) return false;
        } else {
            z = (meshnode[meshelem[i].p[j]].x - meshnode[meshelem[i].p[k2]].x) *
                    (y - meshnode[meshelem[i].p[k2]].y) -
                (meshnode[meshelem[i].p[j]].y - meshnode[meshelem[i].p[k2]].y) *
                    (x - meshnode[meshelem[i].p[k2]].x);
            if (z > 0) return false;
        }
    }

    return true;
}

// ================================================================
// Ctr / ElmArea
// ================================================================

CComplex ElecResultsDoc::Ctr(int i) const
{
    CComplex c(0, 0);
    for (int j = 0; j < 3; j++) {
        c.re += meshnode[meshelem[i].p[j]].x / 3.0;
        c.im += meshnode[meshelem[i].p[j]].y / 3.0;
    }
    return c;
}

double ElecResultsDoc::ElmArea(int i) const
{
    int n[3];
    for (int j = 0; j < 3; j++) n[j] = meshelem[i].p[j];
    double b0 = meshnode[n[1]].y - meshnode[n[2]].y;
    double b1 = meshnode[n[2]].y - meshnode[n[0]].y;
    double c0 = meshnode[n[2]].x - meshnode[n[1]].x;
    double c1 = meshnode[n[0]].x - meshnode[n[2]].x;
    return (b0 * c1 - b1 * c0) / 2.0;
}

// ================================================================
// IsSameMaterial
// Ported from belaviewDoc::IsSameMaterial
// ================================================================

bool ElecResultsDoc::IsSameMaterial(int e1, int e2) const
{
    int b1 = meshelem[e1].blk;
    int b2 = meshelem[e2].blk;

    if (b1 < 0 || b1 >= (int)blockproplist.size()) return false;
    if (b2 < 0 || b2 >= (int)blockproplist.size()) return false;

    return (blockproplist[b1].ex == blockproplist[b2].ex) &&
           (blockproplist[b1].ey == blockproplist[b2].ey);
}

// ================================================================
// HenrotteVector — gradient of mask function
// Ported from belaviewDoc::HenrotteVector
// ================================================================

CComplex ElecResultsDoc::HenrotteVector(int k) const
{
    int n[3];
    double b[3], c[3], da;
    for (int i = 0; i < 3; i++) n[i] = meshelem[k].p[i];
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    CComplex v(0, 0);
    double lc = LengthConv();
    for (int i = 0; i < 3; i++)
        v = v - meshnode[n[i]].msk * CComplex(b[i], c[i]) / (da * lc);

    return v;
}

// ================================================================
// AECF — axisymmetric external correction factor
// Ported from belaviewDoc::AECF (both overloads)
// ================================================================

double ElecResultsDoc::AECF(int k) const
{
    if (!ProblemType) return 1.0;
    if (k < 0 || k >= (int)meshelem.size()) return 1.0;
    if (meshelem[k].lbl < 0 || meshelem[k].lbl >= (int)labellist.size()) return 1.0;
    if (!labellist[meshelem[k].lbl].IsExternal) return 1.0;

    double r = abs(meshelem[k].ctr - CComplex(0, extZo));
    return (r * r) / (extRo * extRi);
}

double ElecResultsDoc::AECF(int k, CComplex p) const
{
    if (!ProblemType) return 1.0;
    if (k < 0 || k >= (int)meshelem.size()) return 1.0;
    if (meshelem[k].lbl < 0 || meshelem[k].lbl >= (int)labellist.size()) return 1.0;
    if (!labellist[meshelem[k].lbl].IsExternal) return 1.0;

    double r = abs(p - CComplex(0, extZo));
    if (r == 0) return AECF(k);
    return (r * r) / (extRo * extRi);
}

// ================================================================
// BlockIntegral — 7 types (0-6)
// Ported from belaviewDoc::BlockIntegral
// ================================================================

CComplex ElecResultsDoc::BlockIntegral(int inttype)
{
    int i, k;
    double B1, B2, F1, F2, y;
    CComplex c, z(0, 0);
    double a, R = 0;
    double r[3];
    double lc = LengthConv();

    // Types 5 and 6 (Henrotte force/torque) need the mask
    if (inttype >= 5) {
        if (!MakeMask()) return CComplex(0, 0);
    }

    for (i = 0; i < (int)meshelem.size(); i++) {
        if (labellist[meshelem[i].lbl].IsSelected && inttype < 5) {
            a = ElmArea(i) * lc * lc;
            if (ProblemType == 1) {
                for (k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;
                R = (r[0] + r[1] + r[2]) / 3.0;
            }

            switch (inttype) {
            case 0: { // stored energy
                if (ProblemType == 1) a *= (2.0 * PI * R);
                else a *= Depth;
                // E = D/(e*eo) with AECF
                CComplex Dk = meshelem[i].D;
                CComplex Ek(
                    Dk.re / blockproplist[meshelem[i].blk].ex,
                    Dk.im / blockproplist[meshelem[i].blk].ey);
                Ek = Ek / eo * AECF(i);
                z += a * Re(Dk * conj(Ek)) / 2.0;
                break;
            }
            case 1: // cross-section area
                z += a;
                break;
            case 2: // volume
                if (ProblemType == 1) a *= (2.0 * PI * R);
                else a *= Depth;
                z += a;
                break;
            case 3: // average D
                if (ProblemType == 1) a *= (2.0 * PI * R);
                else a *= Depth;
                z += a * meshelem[i].D;
                break;
            case 4: { // average E
                if (ProblemType == 1) a *= (2.0 * PI * R);
                else a *= Depth;
                CComplex Dk = meshelem[i].D;
                CComplex Ek(
                    Dk.re / blockproplist[meshelem[i].blk].ex,
                    Dk.im / blockproplist[meshelem[i].blk].ey);
                Ek = Ek / eo * AECF(i);
                z += a * Ek;
                break;
            }
            default:
                break;
            }
        }

        // Integrals evaluated over all elements (Henrotte force/torque)
        if (inttype > 4) {
            a = ElmArea(i) * lc * lc;
            if (ProblemType == 1) {
                for (k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;
                R = (r[0] + r[1] + r[2]) / 3.0;
                a *= (2.0 * PI * R);
            } else {
                a *= Depth;
            }

            switch (inttype) {
            case 5: { // Henrotte force
                B1 = meshelem[i].D.re;
                B2 = meshelem[i].D.im;
                c = HenrotteVector(i);

                // x (or r) direction
                if (ProblemType == 0) {
                    y = ((B1 * B1 - B2 * B2) * c.re + 2.0 * B1 * B2 * c.im) /
                        (2.0 * eo) * AECF(i);
                    z.re += (a * y);
                }

                // y (or z) direction
                y = ((B2 * B2 - B1 * B1) * c.im + 2.0 * B1 * B2 * c.re) /
                    (2.0 * eo) * AECF(i);
                z.im += (a * y);
                break;
            }
            case 6: { // Henrotte torque (planar only)
                if (ProblemType != 0) break;
                B1 = meshelem[i].D.re;
                B2 = meshelem[i].D.im;
                c = HenrotteVector(i);

                F1 = ((B1 * B1 - B2 * B2) * c.re +
                       2.0 * B1 * B2 * c.im) / (2.0 * eo);
                F2 = ((B2 * B2 - B1 * B1) * c.im +
                       2.0 * B1 * B2 * c.re) / (2.0 * eo);

                CComplex cc(0, 0);
                for (k = 0; k < 3; k++) {
                    cc.re += meshnode[meshelem[i].p[k]].x * lc / 3.0;
                    cc.im += meshnode[meshelem[i].p[k]].y * lc / 3.0;
                }

                y = cc.re * F2 - cc.im * F1;
                y *= AECF(i);
                z += (a * y);
                break;
            }
            default:
                break;
            }
        }
    }

    // Integrals 3 and 4 are averages: divide by volume
    if (inttype == 3 || inttype == 4) z = z / BlockIntegral(2);

    return z;
}

// ================================================================
// LineIntegral — 5 types (0-4)
// Ported from belaviewDoc::LineIntegral
// ================================================================

void ElecResultsDoc::LineIntegral(int inttype, double *z)
{
    // inttype:
    //   0 = E.t
    //   1 = D.n
    //   2 = Contour length
    //   3 = Stress tensor force
    //   4 = Stress tensor torque

    double lc = LengthConv();

    // E.t
    if (inttype == 0) {
        EPointVals u;
        int k2 = (int)contour.size();
        GetPointValues(contour[0].re, contour[0].im, u);
        z[0] = u.V;
        GetPointValues(contour[k2 - 1].re, contour[k2 - 1].im, u);
        z[0] -= u.V;
    }

    // D.n
    if (inttype == 1) {
        CComplex n, t, pt;
        EPointVals v;
        double dz, u, d, Dn;
        int i, j, k2, m, elm;
        int NumPlotPoints = d_LineIntegralPoints;
        bool flg;

        z[0] = 0;
        z[1] = 0;
        for (k2 = 1; k2 < (int)contour.size(); k2++) {
            dz = abs(contour[k2] - contour[k2 - 1]) / (double)NumPlotPoints;
            for (i = 0, elm = -1; i < NumPlotPoints; i++) {
                u = ((double)i + 0.5) / (double)NumPlotPoints;
                pt = contour[k2 - 1] + u * (contour[k2] - contour[k2 - 1]);
                t = contour[k2] - contour[k2 - 1];
                t = t / abs(t);
                n = CComplex(-t.im, t.re); // I*t
                pt = pt + n * 1.e-06;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    flg = false;
                    for (j = 0; j < 3; j++)
                        for (m = 0; m < NumList[meshelem[elm].p[j]]; m++) {
                            elm = ConList[meshelem[elm].p[j]][m];
                            if (InTriangleTest(pt.re, pt.im, elm)) {
                                flg = true;
                                m = 100;
                                j = 3;
                            }
                        }
                    if (!flg) elm = InTriangle(pt.re, pt.im);
                }
                bool ok = false;
                if (elm >= 0) ok = GetPointValues(pt.re, pt.im, elm, v);

                if (ok) {
                    Dn = Re(v.D / n);
                    if (ProblemType == 1)
                        d = 2.0 * PI * pt.re * sqr(lc);
                    else
                        d = Depth * lc;
                    z[0] += Dn * dz * d;
                    z[1] += dz * d;
                }
            }
        }
        if (z[1] != 0) z[1] = z[0] / z[1]; // average D.n
    }

    // Contour length
    if (inttype == 2) {
        int i, k2 = (int)contour.size();
        z[0] = 0;
        for (i = 0; i < k2 - 1; i++)
            z[0] += abs(contour[i + 1] - contour[i]);
        z[0] *= lc;

        if (ProblemType == 1) {
            z[1] = 0;
            for (i = 0; i < k2 - 1; i++)
                z[1] += PI * (contour[i].re + contour[i + 1].re) *
                         abs(contour[i + 1] - contour[i]);
            z[1] *= lc * lc;
        } else {
            z[1] = z[0] * Depth;
        }
    }

    // Stress tensor force
    if (inttype == 3) {
        CComplex n, t, pt;
        double Hn, Bn, BH, dF1, dF2;
        EPointVals v;
        double dz, dza, u;
        int i, j, k2, m, elm;
        int NumPlotPoints = d_LineIntegralPoints;
        bool flg;

        for (i = 0; i < 4; i++) z[i] = 0;

        for (k2 = 1; k2 < (int)contour.size(); k2++) {
            dz = abs(contour[k2] - contour[k2 - 1]) / (double)NumPlotPoints;
            for (i = 0, elm = -1; i < NumPlotPoints; i++) {
                u = ((double)i + 0.5) / (double)NumPlotPoints;
                pt = contour[k2 - 1] + u * (contour[k2] - contour[k2 - 1]);
                t = contour[k2] - contour[k2 - 1];
                t = t / abs(t);
                n = CComplex(-t.im, t.re); // I*t
                pt = pt + n * 1.e-06;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    flg = false;
                    for (j = 0; j < 3; j++)
                        for (m = 0; m < NumList[meshelem[elm].p[j]]; m++) {
                            elm = ConList[meshelem[elm].p[j]][m];
                            if (InTriangleTest(pt.re, pt.im, elm)) {
                                flg = true;
                                m = 100;
                                j = 3;
                            }
                        }
                    if (!flg) elm = InTriangle(pt.re, pt.im);
                }
                bool ok = false;
                if (elm >= 0) ok = GetPointValues(pt.re, pt.im, elm, v);

                if (ok) {
                    Hn = Re(v.E / n);
                    Bn = Re(v.D / n);
                    BH = Re(v.D * conj(v.E));
                    dF1 = v.E.re * Bn + v.D.re * Hn - n.re * BH;
                    dF2 = v.E.im * Bn + v.D.im * Hn - n.im * BH;

                    dza = dz * lc;
                    if (ProblemType == 1) {
                        dza *= 2.0 * PI * pt.re * lc;
                        dF1 = 0;
                    } else {
                        dza *= Depth;
                    }

                    z[0] += dF1 * dza / 2.0;
                    z[1] += dF2 * dza / 2.0;
                }
            }
        }
    }

    // Stress tensor torque
    if (inttype == 4) {
        CComplex n, t, pt;
        double Hn, Bn, BH, dF1, dF2, dT;
        EPointVals v;
        double dz, dza, u;
        int i, j, k2, m, elm;
        int NumPlotPoints = d_LineIntegralPoints;
        bool flg;

        z[0] = z[1] = 0;

        for (k2 = 1; k2 < (int)contour.size(); k2++) {
            dz = abs(contour[k2] - contour[k2 - 1]) / (double)NumPlotPoints;
            for (i = 0, elm = -1; i < NumPlotPoints; i++) {
                u = ((double)i + 0.5) / (double)NumPlotPoints;
                pt = contour[k2 - 1] + u * (contour[k2] - contour[k2 - 1]);
                t = contour[k2] - contour[k2 - 1];
                t = t / abs(t);
                n = CComplex(-t.im, t.re); // I*t
                pt = pt + n * 1.e-06;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    flg = false;
                    for (j = 0; j < 3; j++)
                        for (m = 0; m < NumList[meshelem[elm].p[j]]; m++) {
                            elm = ConList[meshelem[elm].p[j]][m];
                            if (InTriangleTest(pt.re, pt.im, elm)) {
                                flg = true;
                                m = 100;
                                j = 3;
                            }
                        }
                    if (!flg) elm = InTriangle(pt.re, pt.im);
                }
                bool ok = false;
                if (elm >= 0) ok = GetPointValues(pt.re, pt.im, elm, v);

                if (ok) {
                    Hn = Re(v.E / n);
                    Bn = Re(v.D / n);
                    BH = Re(v.D * conj(v.E));
                    dF1 = v.E.re * Bn + v.D.re * Hn - n.re * BH;
                    dF2 = v.E.im * Bn + v.D.im * Hn - n.im * BH;
                    dT = pt.re * dF2 - dF1 * pt.im;
                    dza = dz * lc * lc;

                    z[0] += dT * dza * Depth / 2.0;
                }
            }
        }
    }
}

// ================================================================
// BendContour
// Ported from belaviewDoc::BendContour
// ================================================================

void ElecResultsDoc::BendContour(double angle, double anglestep)
{
    if (angle == 0) return;
    if (anglestep == 0) anglestep = 1;

    int k, n;
    double d, tta, dtta, R;
    CComplex c, a0, a1;

    k = (int)contour.size() - 1;
    if (k < 1) return;

    if (angle < -180.0 || angle > 180.0) return;
    n = (int)ceil(fabs(angle / anglestep));
    tta = angle * PI / 180.0;
    dtta = tta / (double)n;

    a1 = contour[k];
    contour.erase(contour.begin() + k);
    a0 = contour[k - 1];

    d = abs(a1 - a0);
    R = d / (2.0 * sin(fabs(tta / 2.0)));
    if (tta > 0)
        c = a0 + (R / d) * (a1 - a0) * exp(CComplex(0, 1) * (PI - tta) / 2.0);
    else
        c = a0 + (R / d) * (a1 - a0) * exp(CComplex(0, -1) * (PI + tta) / 2.0);

    for (k = 1; k <= n; k++)
        contour.push_back(c + (a0 - c) * exp(CComplex(0, k * dtta)));
}

// ================================================================
// GetLineValues — line plot data
// Ported from belaviewDoc::GetLineValues
// ================================================================

void ElecResultsDoc::GetLineValues(std::vector<XYPlotPoint> &pts,
                                    int PlotType, int NumPlotPoints)
{
    std::vector<double> q(contour.size(), 0);
    double z, u, dz;
    CComplex pt, n, t;
    int i, j, k, m, elm;
    EPointVals v;
    bool flg;

    for (i = 1, z = 0; i < (int)contour.size(); i++) {
        z += abs(contour[i] - contour[i - 1]);
        q[i] = z;
    }
    dz = z / (NumPlotPoints - 1);

    pts.resize(NumPlotPoints);
    for (auto &p : pts) p.vals.resize(1, 0);

    for (i = 0, k = 1, z = 0, elm = -1; i < NumPlotPoints; i++, z += dz) {
        while (z > q[k] && k < (int)contour.size() - 1) k++;
        u = (z - q[k - 1]) / (q[k] - q[k - 1]);
        pt = contour[k - 1] + u * (contour[k] - contour[k - 1]);
        t = contour[k] - contour[k - 1];
        t = t / abs(t);
        n = CComplex(-t.im, t.re); // I*t
        pt = pt + n * 1.e-06;

        if (elm < 0)
            elm = InTriangle(pt.re, pt.im);
        else if (!InTriangleTest(pt.re, pt.im, elm)) {
            flg = false;
            for (j = 0; j < 3; j++)
                for (m = 0; m < NumList[meshelem[elm].p[j]]; m++) {
                    elm = ConList[meshelem[elm].p[j]][m];
                    if (InTriangleTest(pt.re, pt.im, elm)) {
                        flg = true;
                        m = 100;
                        j = 3;
                    }
                }
            if (!flg) elm = InTriangle(pt.re, pt.im);
        }
        bool ok = false;
        if (elm >= 0) ok = GetPointValues(pt.re, pt.im, elm, v);

        pts[i].x = z;
        if (ok) {
            switch (PlotType) {
            case 0: pts[i].vals[0] = v.V; break;
            case 1: pts[i].vals[0] = abs(v.D); break;
            case 2: pts[i].vals[0] = Re(v.D / n); break;
            case 3: pts[i].vals[0] = Re(v.D / t); break;
            case 4: pts[i].vals[0] = abs(v.E); break;
            case 5: pts[i].vals[0] = Re(v.E / n); break;
            case 6: pts[i].vals[0] = Re(v.E / t); break;
            default: pts[i].vals[0] = 0; break;
            }
        }
    }
}

// ================================================================
// BuildConnectivity
// ================================================================

void ElecResultsDoc::BuildConnectivity()
{
    int nn = (int)meshnode.size();
    int ne = (int)meshelem.size();

    NumList.assign(nn, 0);
    ConList.clear();
    ConList.resize(nn);

    // Count connections
    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int idx = meshelem[i].p[j];
            if (idx >= 0 && idx < nn) NumList[idx]++;
        }

    // Allocate
    for (int i = 0; i < nn; i++)
        ConList[i].reserve(NumList[i]);

    // Reset counts for building
    NumList.assign(nn, 0);

    // Build lists
    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int k = meshelem[i].p[j];
            if (k >= 0 && k < nn) {
                ConList[k].push_back(i);
                NumList[k]++;
            }
        }

    // Sort each connection list in counter-clockwise order
    for (int i = 0; i < nn; i++) {
        CComplex nodeCC = meshnode[i].CC();
        int sz = NumList[i];
        for (int jj = 0; jj < sz; jj++) {
            for (int kk = 0; kk < sz - jj - 1; kk++) {
                CComplex u0 = meshelem[ConList[i][kk]].ctr - nodeCC;
                CComplex u1 = meshelem[ConList[i][kk + 1]].ctr - nodeCC;
                if (arg(u0) > arg(u1)) {
                    int tmp = ConList[i][kk];
                    ConList[i][kk] = ConList[i][kk + 1];
                    ConList[i][kk + 1] = tmp;
                }
            }
        }
    }
}

// ================================================================
// FindBoundaryEdges
// Ported from belaviewDoc::FindBoundaryEdges
// ================================================================

void ElecResultsDoc::FindBoundaryEdges()
{
    static int plus1mod3[3] = {1, 2, 0};
    static int minus1mod3[3] = {2, 0, 1};

    int NumEls = (int)meshelem.size();

    // Init all neighbor flags to 0 (unfinished)
    for (int i = 0; i < NumEls; i++)
        for (int j = 0; j < 3; j++)
            meshelem[i].n[j] = 0;

    // For each element edge, search for a neighbor sharing both vertices
    for (int i = 0; i < NumEls; i++) {
        for (int j = 0; j < 3; j++) {
            if (meshelem[i].n[j] == 0) {
                int orgi = meshelem[i].p[plus1mod3[j]];
                int desti = meshelem[i].p[minus1mod3[j]];
                bool done = false;
                for (int ni = 0; ni < NumList[orgi]; ni++) {
                    int ei = ConList[orgi][ni];
                    if (ei == i) continue;
                    if (meshelem[ei].p[0] == desti ||
                        meshelem[ei].p[1] == desti ||
                        meshelem[ei].p[2] == desti) {
                        done = true;
                        break;
                    }
                }
                if (!done) meshelem[i].n[j] = 1;
            }
        }
    }
}

// ================================================================
// MakeMask — solve Laplace equation for weighted stress tensor mask
// Ported from bv_makemask.cpp
// ================================================================

bool ElecResultsDoc::IsKosher(int k) const
{
    // For axisymmetric: determines if a node on r=0 is at the extents
    // of the r=0 domain (can safely be set to zero).
    if (ProblemType == 0 || meshnode[k].x > 1.e-6) return true;

    int score = 0;
    for (int i = 0; i < NumList[k]; i++) {
        for (int j = 0; j < 3; j++) {
            int n = meshelem[ConList[k][i]].p[j];
            if (n != k && meshnode[n].x < 1.e-6) {
                score++;
                if (score > 1) return false;
            }
        }
    }
    return true;
}

bool ElecResultsDoc::MakeMask()
{
    if (bHasMask) return true;

    int i, j, k, d;
    CBigLinProb L;
    double v;
    double Me[3][3], be[3];
    double p[3], q[3];
    double a;
    int n[3];

    int NumNodes = (int)meshnode.size();
    int NumEls = (int)meshelem.size();
    bool bOnAxis = false;

    static int plus1mod3[3] = {1, 2, 0};
    static int minus1mod3[3] = {2, 0, 1};

    // Figure out bandwidth
    int bw = 0;
    for (i = 0; i < NumEls; i++)
        for (j = 0; j < 3; j++) {
            k = (j + 1) % 3;
            d = abs(meshelem[i].p[j] - meshelem[i].p[k]);
            if (d > bw) bw = d;
        }
    bw++;

    L.Create(NumNodes, bw);

    // Check if axisymmetric selection lies along r=0
    if (ProblemType == 1) {
        for (i = 0; i < NumEls; i++)
            if (labellist[meshelem[i].lbl].IsSelected) {
                for (j = 0; j < 3; j++)
                    if (meshnode[meshelem[i].p[j]].x < 1.e-6) {
                        bOnAxis = true;
                        break;
                    }
                if (bOnAxis) break;
            }

        if (!bOnAxis)
            for (i = 0; i < NumNodes; i++)
                if (meshnode[i].IsSelected && meshnode[i].x < 1.e-6) {
                    bOnAxis = true;
                    break;
                }
    }

    // Sort materials: 0=air, 1=not air
    std::vector<int> matflag(blockproplist.size(), 0);
    for (i = 0; i < (int)blockproplist.size(); i++) {
        k = 0;
        if (blockproplist[i].ex != 1 || blockproplist[i].ey != 1) k = 1;
        if (blockproplist[i].qv != 0) k = 1;
        matflag[i] = k;
    }
    std::vector<int> lblflag(labellist.size(), 0);
    for (i = 0; i < (int)labellist.size(); i++)
        lblflag[i] = matflag[labellist[i].BlockType];

    // Determine which nodal values should be fixed
    for (i = 0; i < NumNodes; i++) {
        if (meshnode[i].Q != -2) L.V[i] = 0;
        else L.V[i] = -1;
    }

    // Exterior edges to zero
    for (i = 0; i < NumEls; i++)
        for (j = 0; j < 3; j++)
            if (meshelem[i].n[j] == 1) {
                k = meshelem[i].p[plus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
                k = meshelem[i].p[minus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
            }

    // Selected blocks to 1
    for (i = 0; i < NumEls; i++) {
        if (labellist[meshelem[i].lbl].IsSelected)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 1;
        else if (lblflag[meshelem[i].lbl] != 0)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 0;
    }

    // Nodes with point charges not in selected region should be zero
    if (!nodeproplist.empty()) {
        std::vector<CComplex> pp;
        for (i = 0; i < (int)nodelist.size(); i++)
            if (!nodelist[i].BoundaryMarker.empty())
                pp.push_back(nodelist[i].CC());

        int npts = (int)pp.size();
        if (npts > 0)
            for (i = 0; i < NumNodes; i++)
                for (j = 0; j < npts; j++)
                    if (abs(pp[j] - meshnode[i].CC()) < 1.e-8) {
                        if (L.V[i] < 0) L.V[i] = 0;
                        npts--;
                        if (npts > 0) { pp[j] = pp[npts]; j = npts; }
                        else { j = npts; i = NumNodes; }
                    }
    }

    for (i = 0; i < NumNodes; i++)
        if (meshnode[i].IsSelected) L.V[i] = 1;

    // Build element matrices (Laplace stiffness)
    for (i = 0; i < NumEls; i++) {
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) Me[j][k] = 0;
            be[j] = 0;
        }
        for (k = 0; k < 3; k++) n[k] = meshelem[i].p[k];
        p[0] = meshnode[n[1]].y - meshnode[n[2]].y;
        p[1] = meshnode[n[2]].y - meshnode[n[0]].y;
        p[2] = meshnode[n[0]].y - meshnode[n[1]].y;
        q[0] = meshnode[n[2]].x - meshnode[n[1]].x;
        q[1] = meshnode[n[0]].x - meshnode[n[2]].x;
        q[2] = meshnode[n[1]].x - meshnode[n[0]].x;
        a = (p[0] * q[1] - p[1] * q[0]) / 2.0;

        // Validity check
        if (!labellist[meshelem[i].lbl].IsSelected && lblflag[meshelem[i].lbl]) {
            int cnt = 0;
            for (j = 0; j < 3; j++)
                if (L.V[n[j]] == 0) cnt++;
            if (cnt < 3) return false; // invalid selection
        }

        // Weighting
        v = labellist[meshelem[i].lbl].MaxArea;
        if (v <= 0) v = sqrt(a);
        else v = sqrt(v);

        // Stiffness matrix
        for (j = 0; j < 3; j++)
            for (k = 0; k < 3; k++)
                Me[j][k] += v * (p[j] * p[k] + q[j] * q[k]) / a;

        // Apply prescribed values
        for (j = 0; j < 3; j++) {
            if (L.V[n[j]] >= 0) {
                for (k = 0; k < 3; k++) {
                    if (j != k) {
                        be[k] -= Me[k][j] * L.V[n[j]];
                        Me[k][j] = 0;
                        Me[j][k] = 0;
                    }
                }
                be[j] = L.V[n[j]] * Me[j][j];
            }
        }

        // Assemble into global
        for (j = 0; j < 3; j++) {
            for (k = j; k < 3; k++)
                if (Me[j][k] != 0)
                    L.Put(L.Get(n[j], n[k]) - Me[j][k], n[j], n[k]);
            L.b[n[j]] -= be[j];
        }
    }

    // Solve
    CFknDlg dummyView(true);   // quiet: don't leak PCGSolve progress to the terminal
    if (progressCallback)
        dummyView.m_prog1.onProgress = progressCallback;
    L.TheView = &dummyView;
    if (L.PCGSolve(0) == 0) return false;

    // Copy solution
    for (i = 0; i < NumNodes; i++) {
        if (L.V[i] > 0.5) meshnode[i].msk = 1;
        else meshnode[i].msk = 0;
    }

    bHasMask = true;
    return true;
}

// ================================================================
// Geometry query helpers (for contour mode)
// ================================================================

int ElecResultsDoc::ClosestInputNode(double x, double y) const
{
    if (nodelist.empty()) return -1;
    int j = 0;
    double d0 = nodelist[0].GetDistance(x, y);
    for (int i = 1; i < (int)nodelist.size(); i++) {
        double d1 = nodelist[i].GetDistance(x, y);
        if (d1 < d0) { d0 = d1; j = i; }
    }
    return j;
}

double ElecResultsDoc::ShortestDistanceFromSegment(double p, double q, int segm) const
{
    double x0 = nodelist[linelist[segm].n0].x;
    double y0 = nodelist[linelist[segm].n0].y;
    double x1 = nodelist[linelist[segm].n1].x;
    double y1 = nodelist[linelist[segm].n1].y;

    double t = ((p - x0) * (x1 - x0) + (q - y0) * (y1 - y0)) /
               ((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    if (t > 1.0) t = 1.0;
    if (t < 0.0) t = 0.0;

    double x2 = x0 + t * (x1 - x0);
    double y2 = y0 + t * (y1 - y0);

    return sqrt((p - x2) * (p - x2) + (q - y2) * (q - y2));
}

void ElecResultsDoc::GetInputCircle(int arcIdx, CComplex &c, double &R) const
{
    CComplex a0(nodelist[arclist[arcIdx].n0].x, nodelist[arclist[arcIdx].n0].y);
    CComplex a1(nodelist[arclist[arcIdx].n1].x, nodelist[arclist[arcIdx].n1].y);
    double d = abs(a1 - a0);
    CComplex t = (a1 - a0) / d;
    double tta = arclist[arcIdx].ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    c = a0 + (d / 2.0 + CComplex(0, sqrt(std::max(0.0, R * R - d * d / 4.0)))) * t;
}

void ElecResultsDoc::GetCircle(const FArcSegment &arc, CComplex &c, double &R) const
{
    CComplex a0(nodelist[arc.n0].x, nodelist[arc.n0].y);
    CComplex a1(nodelist[arc.n1].x, nodelist[arc.n1].y);
    double d = abs(a1 - a0);
    CComplex t = (a1 - a0) / d;
    double tta = arc.ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    c = a0 + (d / 2.0 + CComplex(0, sqrt(std::max(0.0, R * R - d * d / 4.0)))) * t;
}

double ElecResultsDoc::ShortestDistanceFromInputArc(CComplex p, int arcIdx) const
{
    CComplex a0(nodelist[arclist[arcIdx].n0].x, nodelist[arclist[arcIdx].n0].y);
    CComplex a1(nodelist[arclist[arcIdx].n1].x, nodelist[arclist[arcIdx].n1].y);
    CComplex c;
    double R;
    GetInputCircle(arcIdx, c, R);
    double d = abs(p - c);

    if (d == 0) return R;

    CComplex t = (p - c) / d;
    double l = abs(p - c - R * t);
    double z = arg(t / (a0 - c)) * 180.0 / PI;
    if (z > 0 && z < arclist[arcIdx].ArcLength) return l;

    z = abs(p - a0);
    l = abs(p - a1);
    if (z < l) return z;
    return l;
}
