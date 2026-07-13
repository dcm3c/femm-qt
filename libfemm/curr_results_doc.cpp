// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_results_doc.cpp — Current flow postprocessor solution document
// Reads .anc files and performs full post-load processing.
// Ported from femm42src/femm/CVIEWDOC.CPP and cv_problem.h.

#include "curr_results_doc.h"
#include "utf8_io.h"
#include "fknDlg.h"
#include "spars.h"
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

double CurrResultsDoc::LengthConv() const
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

CurrResultsDoc::~CurrResultsDoc() {}

// ================================================================
// OpenDocument — read .anc file
// Format: header (echoed .fec) then [Solution] with complex nodes
// ================================================================

bool CurrResultsDoc::OpenDocument(const std::string &pathname)
{
    FILE *fp;
    int i, j, k, t;
    char s[1024], q[1024];
    char *v;
    bool flag = false;

    CurrPointProp PProp;
    CurrBoundaryProp BProp;
    CRMaterialProp MProp;
    CRCircuit CProp;
    FNode node;
    FSegment segm;
    FArcSegment asegm;
    ERBlockLabel blk;

    // Clear existing data
    nodelist.clear();
    linelist.clear();
    arclist.clear();
    labellist.clear();
    nodeproplist.clear();
    lineproplist_props.clear();
    blockproplist.clear();
    circproplist.clear();
    cmeshnode.clear();
    cmeshelem.clear();
    meshnode.clear();
    meshelem.clear();
    contour.clear();

    fp = femm_fopen(pathname, "rt");
    if (fp == nullptr) return false;

    filePath = pathname;

    // Defaults
    ProblemType = 0;
    Coords = 0;
    Frequency = 0;
    bHasMask = false;
    Depth = -1;
    extRo = extRi = extZo = 0;

    // Temp storage for raw material data (before computing Kx, Ky)
    struct RawMat { std::string name; double ox=0, oy=0, ex=1, ey=1, ltx=0, lty=0; };
    std::vector<RawMat> rawMats;

    // Parse the header
    while (!flag) {
        if (fgets(s, 1024, fp) == nullptr) break;
        sscanf(s, "%s", q);

        if (strnicmp(q, "[format]", 8) == 0) { q[0] = 0; }

        if (strnicmp(q, "[frequency]", 11) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &Frequency); q[0] = 0;
        }

        if (strnicmp(q, "[depth]", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &Depth); q[0] = 0;
        }

        if (strnicmp(q, "[lengthunits]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%s", q);
            if (strnicmp(q, "inches", 6) == 0) LengthUnits = 0;
            else if (strnicmp(q, "millimeters", 11) == 0) LengthUnits = 1;
            else if (strnicmp(q, "centimeters", 1) == 0) LengthUnits = 2;
            else if (strnicmp(q, "mils", 4) == 0) LengthUnits = 4;
            else if (strnicmp(q, "microns", 6) == 0) LengthUnits = 5;
            else if (strnicmp(q, "meters", 6) == 0) LengthUnits = 3;
            q[0] = 0;
        }

        if (strnicmp(q, "[problemtype]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%s", q);
            if (strnicmp(q, "planar", 6) == 0) ProblemType = 0;
            if (strnicmp(q, "axisymmetric", 3) == 0) ProblemType = 1;
            q[0] = 0;
        }

        if (strnicmp(q, "[coordinates]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%s", q);
            if (strnicmp(q, "cartesian", 4) == 0) Coords = 0;
            if (strnicmp(q, "polar", 5) == 0) Coords = 1;
            q[0] = 0;
        }

        if (strnicmp(q, "[comment]", 9) == 0) { q[0] = 0; }

        // External region
        if (strnicmp(q, "[extzo]", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &extZo); q[0] = 0; }
        if (strnicmp(q, "[extro]", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &extRo); q[0] = 0; }
        if (strnicmp(q, "[extri]", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &extRi); q[0] = 0; }

        // Point properties
        if (strnicmp(q, "<beginpoint>", 11) == 0) { PProp = CurrPointProp(); q[0] = 0; }
        if (strnicmp(q, "<pointname>", 11) == 0) { PProp.PointName = ExtractQuoted(StripKey(s)); q[0] = 0; }
        if (strnicmp(q, "<vpr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.vp.re); q[0] = 0; }
        if (strnicmp(q, "<vpi>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.vp.im); q[0] = 0; }
        if (strnicmp(q, "<qpr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.qp.re); q[0] = 0; }
        if (strnicmp(q, "<qpi>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.qp.im); q[0] = 0; }
        if (strnicmp(q, "<endpoint>", 9) == 0) { nodeproplist.push_back(PProp); q[0] = 0; }

        // Boundary properties
        if (strnicmp(q, "<beginbdry>", 11) == 0) { BProp = CurrBoundaryProp(); q[0] = 0; }
        if (strnicmp(q, "<bdryname>", 10) == 0) { BProp.BdryName = ExtractQuoted(StripKey(s)); q[0] = 0; }
        if (strnicmp(q, "<bdrytype>", 10) == 0) { v = StripKey(s); sscanf(v, "%i", &BProp.BdryFormat); q[0] = 0; }
        if (strnicmp(q, "<vsr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.vs.re); q[0] = 0; }
        if (strnicmp(q, "<vsi>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.vs.im); q[0] = 0; }
        if (strnicmp(q, "<qsr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.qs.re); q[0] = 0; }
        if (strnicmp(q, "<qsi>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.qs.im); q[0] = 0; }
        if (strnicmp(q, "<c0r>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.c0.re); q[0] = 0; }
        if (strnicmp(q, "<c0i>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.c0.im); q[0] = 0; }
        if (strnicmp(q, "<c1r>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.c1.re); q[0] = 0; }
        if (strnicmp(q, "<c1i>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &BProp.c1.im); q[0] = 0; }
        if (strnicmp(q, "<endbdry>", 9) == 0) { lineproplist_props.push_back(BProp); q[0] = 0; }

        // Block (material) properties — store raw, compute Kx/Ky after we know Frequency
        if (strnicmp(q, "<beginblock>", 12) == 0) {
            rawMats.push_back(RawMat());
            q[0] = 0;
        }
        if (strnicmp(q, "<blockname>", 10) == 0 && !rawMats.empty()) {
            rawMats.back().name = ExtractQuoted(StripKey(s)); q[0] = 0;
        }
        if (strnicmp(q, "<ox>", 4) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().ox); q[0] = 0; }
        if (strnicmp(q, "<oy>", 4) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().oy); q[0] = 0; }
        if (strnicmp(q, "<ex>", 4) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().ex); q[0] = 0; }
        if (strnicmp(q, "<ey>", 4) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().ey); q[0] = 0; }
        if (strnicmp(q, "<ltx>", 5) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().ltx); q[0] = 0; }
        if (strnicmp(q, "<lty>", 5) == 0 && !rawMats.empty()) { v = StripKey(s); sscanf(v, "%lf", &rawMats.back().lty); q[0] = 0; }
        if (strnicmp(q, "<endblock>", 9) == 0) { q[0] = 0; } // raw mat already pushed

        // Conductor properties
        if (strnicmp(q, "<beginconductor>", 16) == 0) {
            CProp = CRCircuit();
            q[0] = 0;
        }
        if (strnicmp(q, "<conductorname>", 15) == 0) { CProp.CircName = ExtractQuoted(StripKey(s)); q[0] = 0; }
        if (strnicmp(q, "<vcr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.V.re); q[0] = 0; }
        if (strnicmp(q, "<vci>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.V.im); q[0] = 0; }
        if (strnicmp(q, "<qcr>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.q.re); q[0] = 0; }
        if (strnicmp(q, "<qci>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.q.im); q[0] = 0; }
        if (strnicmp(q, "<conductortype>", 15) == 0) { v = StripKey(s); sscanf(v, "%i", &CProp.CircType); q[0] = 0; }
        if (strnicmp(q, "<endconductor>", 14) == 0) { circproplist.push_back(CProp); q[0] = 0; }

        // Input geometry
        if (strnicmp(q, "[numpoints]", 11) == 0) {
            v = StripKey(s); sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                node = FNode();
                sscanf(s, "%lf\t%lf", &node.x, &node.y);
                nodelist.push_back(node);
            }
            q[0] = 0;
        }

        if (strnicmp(q, "[numsegments]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                segm = FSegment();
                int hidden = 0, ingroup = 0;
                sscanf(s, "%i\t%i\t%lf\t%i\t%i\t%i",
                       &segm.n0, &segm.n1, &segm.MaxSideLength, &t, &hidden, &ingroup);
                segm.Hidden = (hidden != 0);
                segm.InGroup = ingroup;
                linelist.push_back(segm);
            }
            q[0] = 0;
        }

        if (strnicmp(q, "[numarcsegments]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                asegm = FArcSegment();
                int hidden = 0, ingroup = 0;
                sscanf(s, "%i\t%i\t%lf\t%lf\t%i\t%i\t%i",
                       &asegm.n0, &asegm.n1, &asegm.ArcLength,
                       &asegm.MaxSideLength, &t, &hidden, &ingroup);
                asegm.Hidden = (hidden != 0);
                asegm.InGroup = ingroup;
                arclist.push_back(asegm);
            }
            q[0] = 0;
        }

        if (strnicmp(q, "[numholes]", 10) == 0) {
            v = StripKey(s); sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) fgets(s, 1024, fp);
            q[0] = 0;
        }

        if (strnicmp(q, "[numblocklabels]", 13) == 0) {
            v = StripKey(s); sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                blk = ERBlockLabel();
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

    // Compute effective conductivity from raw material data
    // K = ox*1e6 + j*2*pi*f*eo*ex  (ox in MS/m -> S/m)
    // Permittivity with loss tangent: ex_complex = ex * exp(-j*atan(ltx))
    for (auto &rm : rawMats) {
        CRMaterialProp mp;
        mp.BlockName = rm.name;
        mp.ox = rm.ox * 1.e6;   // MS/m to S/m
        mp.oy = rm.oy * 1.e6;

        // Complex permittivity with loss tangent
        mp.ex = rm.ex * exp(CComplex(0, -atan(rm.ltx)));
        mp.ey = rm.ey * exp(CComplex(0, -atan(rm.lty)));

        // Effective conductivity: K = sigma + j*omega*epsilon*e0
        double omega = 2.0 * PI * Frequency;
        mp.Kx = mp.ox + CComplex(0, 1) * omega * eo * mp.ex;
        mp.Ky = mp.oy + CComplex(0, 1) * omega * eo * mp.ey;

        blockproplist.push_back(mp);
    }

    // Read mesh nodes with complex voltage: x y Re(V) Im(V) Q
    fscanf(fp, "%i\n", &k);
    cmeshnode.resize(k);
    meshnode.resize(k);
    for (i = 0; i < k; i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%lf\t%lf\t%lf\t%lf\t%i",
               &cmeshnode[i].x, &cmeshnode[i].y,
               &cmeshnode[i].V.re, &cmeshnode[i].V.im,
               &cmeshnode[i].Q);
        // Copy to display projection
        meshnode[i].x = cmeshnode[i].x;
        meshnode[i].y = cmeshnode[i].y;
        meshnode[i].Q = cmeshnode[i].Q;
    }

    // Read mesh elements: p0 p1 p2 lbl
    fscanf(fp, "%i\n", &k);
    cmeshelem.resize(k);
    meshelem.resize(k);
    for (i = 0; i < k; i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%i\t%i\t%i\t%i",
               &cmeshelem[i].p[0], &cmeshelem[i].p[1], &cmeshelem[i].p[2],
               &cmeshelem[i].lbl);
        if (cmeshelem[i].lbl >= 0 && cmeshelem[i].lbl < (int)labellist.size())
            cmeshelem[i].blk = labellist[cmeshelem[i].lbl].BlockType;
        // Copy topology to display element
        for (j = 0; j < 3; j++) meshelem[i].p[j] = cmeshelem[i].p[j];
        meshelem[i].blk = cmeshelem[i].blk;
        meshelem[i].lbl = cmeshelem[i].lbl;
    }

    // Read circuit data: Re(V) Im(V) Re(q) Im(q)
    fscanf(fp, "%i\n", &k);
    for (i = 0; i < k && i < (int)circproplist.size(); i++) {
        fgets(s, 1024, fp);
        sscanf(s, "%lf\t%lf\t%lf\t%lf",
               &circproplist[i].V.re, &circproplist[i].V.im,
               &circproplist[i].q.re, &circproplist[i].q.im);
    }

    fclose(fp);

    PostProcessing();
    return true;
}

// ================================================================
// PostProcessing — compute all derived fields after loading
// ================================================================

void CurrResultsDoc::PostProcessing()
{
    int i, j;
    double b;

    // Scale depth to meters
    if (Depth == -1) Depth = 1;
    else Depth *= LengthConv();

    // Element centroids and bounding radii
    for (i = 0; i < (int)cmeshelem.size(); i++) {
        cmeshelem[i].ctr = Ctr(i);
        cmeshelem[i].rsqr = 0;
        for (j = 0; j < 3; j++) {
            b = sqr(cmeshnode[cmeshelem[i].p[j]].x - cmeshelem[i].ctr.re) +
                sqr(cmeshnode[cmeshelem[i].p[j]].y - cmeshelem[i].ctr.im);
            if (b > cmeshelem[i].rsqr) cmeshelem[i].rsqr = b;
        }
        // Copy to display element
        meshelem[i].ctr = cmeshelem[i].ctr;
        meshelem[i].rsqr = cmeshelem[i].rsqr;
    }

    // Compute current density in each element
    for (i = 0; i < (int)cmeshelem.size(); i++) GetElementJ(i);

    // Build connectivity (needed for smoothing and boundary edges)
    BuildConnectivity();

    // Find boundary edges
    FindBoundaryEdges();

    // Compute smoothed nodal J
    for (i = 0; i < (int)cmeshelem.size(); i++)
        GetNodalJ(cmeshelem[i].jx, cmeshelem[i].jy, i);

    // Compute PlotBounds
    // Exclude external region elements
    std::vector<short> isExt(cmeshelem.size(), 0);
    int extCount = 0;
    for (i = 0; i < (int)cmeshelem.size(); i++) {
        if (cmeshelem[i].lbl >= 0 && cmeshelem[i].lbl < (int)labellist.size()) {
            if (labellist[cmeshelem[i].lbl].IsExternal) isExt[i] = 1;
        }
        if (isExt[i]) extCount++;
    }
    if (extCount == (int)cmeshelem.size())
        std::fill(isExt.begin(), isExt.end(), 0);

    // Voltage bounds from nodes
    {
        std::vector<double> vMag, vRe, vIm;
        vMag.reserve(cmeshnode.size());
        vRe.reserve(cmeshnode.size());
        vIm.reserve(cmeshnode.size());
        for (auto &nd : cmeshnode) {
            vMag.push_back(abs(nd.V));
            vRe.push_back(nd.V.re);
            vIm.push_back(nd.V.im);
        }
        auto bounds = [](std::vector<double> &v, double out[2]) {
            if (v.empty()) { out[0] = out[1] = 0; return; }
            std::sort(v.begin(), v.end());
            out[0] = v.front();
            out[1] = v.back();
        };
        bounds(vMag, d_PlotBounds[0]);
        bounds(vRe,  d_PlotBounds[1]);
        bounds(vIm,  d_PlotBounds[2]);
    }

    // J and E bounds from elements (99th percentile for upper bound)
    {
        std::vector<double> jMag, jRe, jIm, eMag, eRe, eIm;
        for (i = 0; i < (int)cmeshelem.size(); i++) {
            if (isExt[i]) continue;
            CComplex Jx = cmeshelem[i].Jx;
            CComplex Jy = cmeshelem[i].Jy;

            // |J| = sqrt(|Jx|^2 + |Jy|^2)
            double jm = sqrt(abs(Jx) * abs(Jx) + abs(Jy) * abs(Jy));
            double jr = sqrt(Jx.re * Jx.re + Jy.re * Jy.re);
            double ji = sqrt(Jx.im * Jx.im + Jy.im * Jy.im);
            jMag.push_back(jm);
            jRe.push_back(jr);
            jIm.push_back(ji);

            // E = J / K
            int blk = cmeshelem[i].blk;
            if (blk >= 0 && blk < (int)blockproplist.size()) {
                CComplex Ex = (abs(blockproplist[blk].Kx) > 0) ? Jx / blockproplist[blk].Kx : CComplex(0,0);
                CComplex Ey = (abs(blockproplist[blk].Ky) > 0) ? Jy / blockproplist[blk].Ky : CComplex(0,0);
                double aecf = AECF(i);
                Ex = Ex * aecf;
                Ey = Ey * aecf;
                double em = sqrt(abs(Ex) * abs(Ex) + abs(Ey) * abs(Ey));
                double er = sqrt(Ex.re * Ex.re + Ey.re * Ey.re);
                double ei = sqrt(Ex.im * Ex.im + Ey.im * Ey.im);
                eMag.push_back(em);
                eRe.push_back(er);
                eIm.push_back(ei);
            }
        }
        auto bounds99 = [](std::vector<double> &v, double out[2]) {
            if (v.empty()) { out[0] = out[1] = 0; return; }
            std::sort(v.begin(), v.end());
            out[0] = v.front();
            int idx99 = (int)(v.size() * 0.99);
            if (idx99 >= (int)v.size()) idx99 = (int)v.size() - 1;
            out[1] = v[idx99];
        };
        bounds99(jMag, d_PlotBounds[3]);
        bounds99(jRe,  d_PlotBounds[4]);
        bounds99(jIm,  d_PlotBounds[5]);
        bounds99(eMag, d_PlotBounds[6]);
        bounds99(eRe,  d_PlotBounds[7]);
        bounds99(eIm,  d_PlotBounds[8]);
    }

    for (i = 0; i < 9; i++) {
        PlotBounds[i][0] = d_PlotBounds[i][0];
        PlotBounds[i][1] = d_PlotBounds[i][1];
    }

    // Set up contour range from voltage
    A_Low = d_PlotBounds[1][0];  // Re(V) range for contours
    A_High = d_PlotBounds[1][1];
    A_lb = A_Low;
    A_ub = A_High;

    // Update display projection
    UpdateDisplayProjection();
}

// ================================================================
// UpdateDisplayProjection — project complex V to display meshnode[i].V
// ================================================================

void CurrResultsDoc::UpdateDisplayProjection()
{
    for (int i = 0; i < (int)cmeshnode.size(); i++) {
        switch (voltageDisplay) {
        case VM_Magnitude:  meshnode[i].V = abs(cmeshnode[i].V); break;
        case VM_Real:       meshnode[i].V = cmeshnode[i].V.re; break;
        case VM_Imaginary:  meshnode[i].V = cmeshnode[i].V.im; break;
        }
        meshnode[i].msk = cmeshnode[i].msk;
    }

    // Update contour range based on display mode
    switch (voltageDisplay) {
    case VM_Magnitude:  A_Low = d_PlotBounds[0][0]; A_High = d_PlotBounds[0][1]; break;
    case VM_Real:       A_Low = d_PlotBounds[1][0]; A_High = d_PlotBounds[1][1]; break;
    case VM_Imaginary:  A_Low = d_PlotBounds[2][0]; A_High = d_PlotBounds[2][1]; break;
    }
    A_lb = A_Low;
    A_ub = A_High;
}

// ================================================================
// GetElementJ — compute current density J = K * E for element k
// E = -grad(V) where V is complex
// ================================================================

void CurrResultsDoc::GetElementJ(int k)
{
    int i, n[3];
    double b[3], c[3], da;

    for (i = 0; i < 3; i++) n[i] = cmeshelem[k].p[i];

    b[0] = cmeshnode[n[1]].y - cmeshnode[n[2]].y;
    b[1] = cmeshnode[n[2]].y - cmeshnode[n[0]].y;
    b[2] = cmeshnode[n[0]].y - cmeshnode[n[1]].y;
    c[0] = cmeshnode[n[2]].x - cmeshnode[n[1]].x;
    c[1] = cmeshnode[n[0]].x - cmeshnode[n[2]].x;
    c[2] = cmeshnode[n[1]].x - cmeshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    double lc = LengthConv();

    // E = -grad(V) / lc   (complex Ex, complex Ey)
    CComplex Ex(0, 0), Ey(0, 0);
    for (i = 0; i < 3; i++) {
        Ex = Ex - cmeshnode[n[i]].V * b[i] / (da * lc);
        Ey = Ey - cmeshnode[n[i]].V * c[i] / (da * lc);
    }

    // J = K * E (component-wise)
    int blk = cmeshelem[k].blk;
    if (blk >= 0 && blk < (int)blockproplist.size()) {
        cmeshelem[k].Jx = blockproplist[blk].Kx * Ex / AECF(k);
        cmeshelem[k].Jy = blockproplist[blk].Ky * Ey / AECF(k);
    } else {
        cmeshelem[k].Jx = cmeshelem[k].Jy = CComplex(0, 0);
    }

    // Also store D = J magnitude info in display element for basic operations
    // Pack as CComplex(|Jx|, |Jy|) for vector plot bounds
    meshelem[k].D = CComplex(abs(cmeshelem[k].Jx), abs(cmeshelem[k].Jy));
}

// ================================================================
// GetNodalJ — least-squares smoothed nodal current density
// Same approach as GetNodalD but with complex V and separate Jx, Jy
// ================================================================

void CurrResultsDoc::GetNodalJ(CComplex *jx, CComplex *jy, int N)
{
    int i, j, k, n, m, p, eos, nos, qn;
    int lf, rt;
    bool flag;
    int q[21];

    for (i = 0; i < 3; i++) {
        j = cmeshelem[N].p[i];
        lf = rt = -1;
        flag = false;
        for (eos = 0; eos < NumList[j]; eos++)
            if (ConList[j][eos] == N) break;

        // scan ccw
        for (k = 0, m = eos, qn = 0; k < NumList[j]; k++) {
            n = ConList[j][m];
            if (!IsSameMaterial(n, N)) break;
            for (nos = 0; nos < 3; nos++)
                if (cmeshelem[n].p[nos] == j) break;
            if (nos == 3) break;
            nos--;
            if (nos < 0) nos = 2;
            p = cmeshelem[n].p[nos];
            if (qn < 20) q[qn++] = p;
            if ((cmeshnode[j].Q != -2) && (cmeshnode[p].Q != -2)) { rt = p; break; }
            m++;
            if (m == NumList[j]) m = 0;
        }

        // scan cw
        for (k = 0, m = eos; k < NumList[j]; k++) {
            n = ConList[j][m];
            if (!IsSameMaterial(n, N)) break;
            for (nos = 0; nos < 3; nos++)
                if (cmeshelem[n].p[nos] == j) break;
            if (nos == 3) break;
            nos++;
            if (nos > 2) nos = 0;
            p = cmeshelem[n].p[nos];
            if (qn < 20) q[qn++] = p;
            if ((cmeshnode[j].Q != -2) && (cmeshnode[p].Q != -2)) { lf = p; break; }
            m--;
            if (m < 0) m = NumList[j] - 1;
        }

        // Catch boundary special cases
        if ((lf == rt) && (rt != -1) && (cmeshnode[j].Q != -2)) {
            jx[i] = cmeshelem[N].Jx; jy[i] = cmeshelem[N].Jy; flag = true;
        } else if ((rt != -1) && (cmeshnode[j].Q != -2) && (lf == -1)) {
            jx[i] = cmeshelem[N].Jx; jy[i] = cmeshelem[N].Jy; flag = true;
        } else if ((lf != -1) && (cmeshnode[j].Q != -2) && (rt == -1)) {
            jx[i] = cmeshelem[N].Jx; jy[i] = cmeshelem[N].Jy; flag = true;
        } else if ((lf == -1) && (rt == -1) && (cmeshnode[j].Q != -2)) {
            jx[i] = cmeshelem[N].Jx; jy[i] = cmeshelem[N].Jy; flag = true;
        } else if ((lf != -1) && (rt != -1) && (cmeshnode[j].Q != -2)) {
            CComplex xc, yc;
            xc = cmeshnode[lf].CC() - cmeshnode[j].CC();
            xc = xc / abs(xc);
            yc = cmeshnode[j].CC() - cmeshnode[rt].CC();
            yc = yc / abs(yc);
            if (fabs(arg(xc / yc)) > 10.0001 * PI / 180.0) {
                jx[i] = cmeshelem[N].Jx; jy[i] = cmeshelem[N].Jy; flag = true;
            }
        }

        if (!flag) {
            // Least-squares fit: plane through V neighbors to estimate grad(V)
            double xi = 0, yi = 0, ii = 0, xx = 0, xy = 0, yy = 0;
            CComplex iv(0,0), xv(0,0), yv(0,0);
            q[qn++] = j;

            for (k = 0; k < qn; k++) {
                double dx = cmeshnode[q[k]].x - cmeshnode[j].x;
                double dy = cmeshnode[q[k]].y - cmeshnode[j].y;
                CComplex dv = cmeshnode[j].V - cmeshnode[q[k]].V;
                ii += 1.0; xi += dx; yi += dy;
                xx += dx * dx; xy += dx * dy; yy += dy * dy;
                iv = iv + dv; xv = xv + dx * dv; yv = yv + dy * dv;
            }
            double lc = LengthConv();
            double det = (-(ii * xy * xy) + 2 * xi * xy * yi - xx * yi * yi -
                          xi * xi * yy + ii * xx * yy) * lc;

            if (det == 0) {
                jx[i] = cmeshelem[N].Jx;
                jy[i] = cmeshelem[N].Jy;
            } else {
                CComplex Ex = (iv * xy * yi - xv * yi * yi - ii * xy * yv +
                               xi * yi * yv - iv * xi * yy + ii * xv * yy) / det;
                CComplex Ey = (iv * xi * xy - ii * xv * xy + xi * xv * yi -
                               iv * xx * yi - xi * xi * yv + ii * xx * yv) / det;

                int blk = cmeshelem[N].blk;
                double aecf = AECF(N, cmeshnode[j].CC());
                if (blk >= 0 && blk < (int)blockproplist.size()) {
                    jx[i] = blockproplist[blk].Kx * Ex / aecf;
                    jy[i] = blockproplist[blk].Ky * Ey / aecf;
                } else {
                    jx[i] = jy[i] = CComplex(0, 0);
                }
            }
        }
    }
}

// ================================================================
// GetPointValues — CPointVals (complex, full detail)
// ================================================================

bool CurrResultsDoc::GetPointValues(double x, double y, CPointVals &u)
{
    int k = InTriangle(x, y);
    if (k < 0) return false;
    return GetPointValues(x, y, k, u);
}

bool CurrResultsDoc::GetPointValues(double x, double y, int k, CPointVals &u)
{
    int i, n[3];
    double a[3], b[3], c[3], da;

    for (i = 0; i < 3; i++) n[i] = cmeshelem[k].p[i];
    a[0] = cmeshnode[n[1]].x * cmeshnode[n[2]].y - cmeshnode[n[2]].x * cmeshnode[n[1]].y;
    a[1] = cmeshnode[n[2]].x * cmeshnode[n[0]].y - cmeshnode[n[0]].x * cmeshnode[n[2]].y;
    a[2] = cmeshnode[n[0]].x * cmeshnode[n[1]].y - cmeshnode[n[1]].x * cmeshnode[n[0]].y;
    b[0] = cmeshnode[n[1]].y - cmeshnode[n[2]].y;
    b[1] = cmeshnode[n[2]].y - cmeshnode[n[0]].y;
    b[2] = cmeshnode[n[0]].y - cmeshnode[n[1]].y;
    c[0] = cmeshnode[n[2]].x - cmeshnode[n[1]].x;
    c[1] = cmeshnode[n[0]].x - cmeshnode[n[2]].x;
    c[2] = cmeshnode[n[1]].x - cmeshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    // Interpolate voltage and current density (matching OF GetPointValues)
    int blk = cmeshelem[k].blk;
    double aecf = AECF(k, CComplex(x, y));

    u.V = CComplex(0, 0);
    u.Jx = u.Jy = CComplex(0, 0);
    for (i = 0; i < 3; i++) {
        double w = (a[i] + b[i] * x + c[i] * y) / da;
        u.V = u.V + cmeshnode[n[i]].V * w;
        if (Smooth) {
            u.Jx = u.Jx + cmeshelem[k].jx[i] * w;
            u.Jy = u.Jy + cmeshelem[k].jy[i] * w;
        }
    }
    if (!Smooth) {
        u.Jx = cmeshelem[k].Jx;
        u.Jy = cmeshelem[k].Jy;
    }

    // AC conductivity (with AECF correction, matching OF)
    if (blk >= 0 && blk < (int)blockproplist.size()) {
        u.Kx = blockproplist[blk].Kx / aecf;
        u.Ky = blockproplist[blk].Ky / aecf;
    }

    // Electric field intensity: E = J / K
    u.Ex = u.Jx / u.Kx;
    u.Ey = u.Jy / u.Ky;

    // Electrical permittivity (with AECF correction)
    if (blk >= 0 && blk < (int)blockproplist.size()) {
        u.ex = blockproplist[blk].ex / aecf;
        u.ey = blockproplist[blk].ey / aecf;
    }

    // DC conductivity (with AECF correction)
    if (blk >= 0 && blk < (int)blockproplist.size()) {
        u.ox = blockproplist[blk].ox / aecf;
        u.oy = blockproplist[blk].oy / aecf;
    }

    // Conduction current density: Jc = sigma * E
    u.Jcx = u.ox * u.Ex;
    u.Jcy = u.oy * u.Ey;

    // Electric flux density: D = epsilon * E
    CComplex Dx = u.Ex * u.ex * eo;
    CComplex Dy = u.Ey * u.ey * eo;

    // Displacement current density: Jd = j*omega*D
    u.Jdx = Dx * CComplex(0, 1) * 2.0 * PI * Frequency;
    u.Jdy = Dy * CComplex(0, 1) * 2.0 * PI * Frequency;

    return true;
}

// ================================================================
// GetPointJ — interpolated J at arbitrary point
// ================================================================

void CurrResultsDoc::GetPointJ(double x, double y,
                                CComplex &Jxout, CComplex &Jyout, CRElement &elm)
{
    if (!Smooth) {
        Jxout = elm.Jx;
        Jyout = elm.Jy;
        return;
    }

    int i, n[3];
    double da, a[3], b[3], c[3];

    for (i = 0; i < 3; i++) n[i] = elm.p[i];
    a[0] = cmeshnode[n[1]].x * cmeshnode[n[2]].y - cmeshnode[n[2]].x * cmeshnode[n[1]].y;
    a[1] = cmeshnode[n[2]].x * cmeshnode[n[0]].y - cmeshnode[n[0]].x * cmeshnode[n[2]].y;
    a[2] = cmeshnode[n[0]].x * cmeshnode[n[1]].y - cmeshnode[n[1]].x * cmeshnode[n[0]].y;
    b[0] = cmeshnode[n[1]].y - cmeshnode[n[2]].y;
    b[1] = cmeshnode[n[2]].y - cmeshnode[n[0]].y;
    b[2] = cmeshnode[n[0]].y - cmeshnode[n[1]].y;
    c[0] = cmeshnode[n[2]].x - cmeshnode[n[1]].x;
    c[1] = cmeshnode[n[0]].x - cmeshnode[n[2]].x;
    c[2] = cmeshnode[n[1]].x - cmeshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    Jxout = Jyout = CComplex(0, 0);
    for (i = 0; i < 3; i++) {
        double w = (a[i] + b[i] * x + c[i] * y) / da;
        Jxout = Jxout + elm.jx[i] * w;
        Jyout = Jyout + elm.jy[i] * w;
    }
}

// ================================================================
// EPointVals overloads for ScalarResultsWidget compatibility
// ================================================================

bool CurrResultsDoc::GetPointValues(double x, double y, EPointVals &u)
{
    CPointVals cv;
    if (!GetPointValues(x, y, cv)) return false;
    u.V = cv.V.re;  // or abs, depending on display mode
    switch (voltageDisplay) {
    case VM_Magnitude:  u.V = abs(cv.V); break;
    case VM_Real:       u.V = cv.V.re; break;
    case VM_Imaginary:  u.V = cv.V.im; break;
    }
    // Pack J magnitude into D for compatibility
    u.D = CComplex(abs(cv.Jx), abs(cv.Jy));
    u.E = CComplex(abs(cv.Ex), abs(cv.Ey));
    u.e = CComplex(abs(cv.Kx), abs(cv.Ky));
    u.nrg = 0;
    return true;
}

bool CurrResultsDoc::GetPointValues(double x, double y, int k, EPointVals &u)
{
    CPointVals cv;
    if (!GetPointValues(x, y, k, cv)) return false;
    u.V = cv.V.re;
    switch (voltageDisplay) {
    case VM_Magnitude:  u.V = abs(cv.V); break;
    case VM_Real:       u.V = cv.V.re; break;
    case VM_Imaginary:  u.V = cv.V.im; break;
    }
    u.D = CComplex(abs(cv.Jx), abs(cv.Jy));
    u.E = CComplex(abs(cv.Ex), abs(cv.Ey));
    u.e = CComplex(abs(cv.Kx), abs(cv.Ky));
    u.nrg = 0;
    return true;
}

// ================================================================
// InTriangle / InTriangleTest
// ================================================================

int CurrResultsDoc::InTriangle(double x, double y)
{
    static int k = 0;
    int j, hi, lo, sz;
    double z;

    sz = (int)cmeshelem.size();
    if (k < 0 || k >= sz) k = 0;

    if (InTriangleTest(x, y, k)) return k;

    hi = k; lo = k;
    for (j = 0; j < sz; j += 2) {
        hi++; if (hi >= sz) hi = 0;
        lo--; if (lo < 0) lo = sz - 1;

        z = sqr(cmeshelem[hi].ctr.re - x) + sqr(cmeshelem[hi].ctr.im - y);
        if (z <= cmeshelem[hi].rsqr)
            if (InTriangleTest(x, y, hi)) { k = hi; return k; }

        z = sqr(cmeshelem[lo].ctr.re - x) + sqr(cmeshelem[lo].ctr.im - y);
        if (z <= cmeshelem[lo].rsqr)
            if (InTriangleTest(x, y, lo)) { k = lo; return k; }
    }
    return -1;
}

bool CurrResultsDoc::InTriangleTest(double x, double y, int i) const
{
    if (i < 0 || i >= (int)cmeshelem.size()) return false;
    for (int j = 0; j < 3; j++) {
        int k2 = (j + 1) % 3;
        double z;
        if (cmeshelem[i].p[k2] > cmeshelem[i].p[j]) {
            z = (cmeshnode[cmeshelem[i].p[k2]].x - cmeshnode[cmeshelem[i].p[j]].x) *
                    (y - cmeshnode[cmeshelem[i].p[j]].y) -
                (cmeshnode[cmeshelem[i].p[k2]].y - cmeshnode[cmeshelem[i].p[j]].y) *
                    (x - cmeshnode[cmeshelem[i].p[j]].x);
            if (z < 0) return false;
        } else {
            z = (cmeshnode[cmeshelem[i].p[j]].x - cmeshnode[cmeshelem[i].p[k2]].x) *
                    (y - cmeshnode[cmeshelem[i].p[k2]].y) -
                (cmeshnode[cmeshelem[i].p[j]].y - cmeshnode[cmeshelem[i].p[k2]].y) *
                    (x - cmeshnode[cmeshelem[i].p[k2]].x);
            if (z > 0) return false;
        }
    }
    return true;
}

// ================================================================
// Ctr / ElmArea
// ================================================================

CComplex CurrResultsDoc::Ctr(int i) const
{
    CComplex c(0, 0);
    for (int j = 0; j < 3; j++) {
        c.re += cmeshnode[cmeshelem[i].p[j]].x / 3.0;
        c.im += cmeshnode[cmeshelem[i].p[j]].y / 3.0;
    }
    return c;
}

double CurrResultsDoc::ElmArea(int i) const
{
    int n[3];
    for (int j = 0; j < 3; j++) n[j] = cmeshelem[i].p[j];
    double b0 = cmeshnode[n[1]].y - cmeshnode[n[2]].y;
    double b1 = cmeshnode[n[2]].y - cmeshnode[n[0]].y;
    double c0 = cmeshnode[n[2]].x - cmeshnode[n[1]].x;
    double c1 = cmeshnode[n[0]].x - cmeshnode[n[2]].x;
    return (b0 * c1 - b1 * c0) / 2.0;
}

// ================================================================
// IsSameMaterial
// ================================================================

bool CurrResultsDoc::IsSameMaterial(int e1, int e2) const
{
    int b1 = cmeshelem[e1].blk;
    int b2 = cmeshelem[e2].blk;
    if (b1 < 0 || b1 >= (int)blockproplist.size()) return false;
    if (b2 < 0 || b2 >= (int)blockproplist.size()) return false;
    return (blockproplist[b1].ox == blockproplist[b2].ox) &&
           (blockproplist[b1].oy == blockproplist[b2].oy) &&
           (abs(blockproplist[b1].ex) == abs(blockproplist[b2].ex)) &&
           (abs(blockproplist[b1].ey) == abs(blockproplist[b2].ey));
}

// ================================================================
// AECF — axisymmetric external correction factor
// ================================================================

double CurrResultsDoc::AECF(int k) const
{
    if (!ProblemType) return 1.0;
    if (k < 0 || k >= (int)cmeshelem.size()) return 1.0;
    if (cmeshelem[k].lbl < 0 || cmeshelem[k].lbl >= (int)labellist.size()) return 1.0;
    if (!labellist[cmeshelem[k].lbl].IsExternal) return 1.0;
    double r = abs(cmeshelem[k].ctr - CComplex(0, extZo));
    return (r * r) / (extRo * extRi);
}

double CurrResultsDoc::AECF(int k, CComplex p) const
{
    if (!ProblemType) return 1.0;
    if (k < 0 || k >= (int)cmeshelem.size()) return 1.0;
    if (cmeshelem[k].lbl < 0 || cmeshelem[k].lbl >= (int)labellist.size()) return 1.0;
    if (!labellist[cmeshelem[k].lbl].IsExternal) return 1.0;
    double r = abs(p - CComplex(0, extZo));
    if (r == 0) return AECF(k);
    return (r * r) / (extRo * extRi);
}

// ================================================================
// HenrotteVector
// ================================================================

CComplex CurrResultsDoc::HenrotteVector(int k) const
{
    int n[3];
    double b[3], c[3], da;
    for (int i = 0; i < 3; i++) n[i] = cmeshelem[k].p[i];
    b[0] = cmeshnode[n[1]].y - cmeshnode[n[2]].y;
    b[1] = cmeshnode[n[2]].y - cmeshnode[n[0]].y;
    b[2] = cmeshnode[n[0]].y - cmeshnode[n[1]].y;
    c[0] = cmeshnode[n[2]].x - cmeshnode[n[1]].x;
    c[1] = cmeshnode[n[0]].x - cmeshnode[n[2]].x;
    c[2] = cmeshnode[n[1]].x - cmeshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    CComplex v(0, 0);
    double lc = LengthConv();
    for (int i = 0; i < 3; i++)
        v = v - meshnode[n[i]].msk * CComplex(b[i], c[i]) / (da * lc);

    return v;
}

// ================================================================
// BlockIntegral — current flow block integrals
// Types: 0=Real Power, 1=Reactive Power, 2=Apparent Power,
//        3=Time-Avg Stored Energy, 4=Cross-section area, 5=Volume,
//        6-11=Henrotte force/torque (planar only)
// ================================================================

CComplex CurrResultsDoc::BlockIntegral(int inttype)
{
    int i, k;
    CComplex c, y, z(0, 0), Ex, Ey, B1, B2, F1, F2;
    double a, R = 0, aecf;
    double r[3];
    double lc = LengthConv();

    // Henrotte types need the stress tensor mask
    if (inttype >= 6) {
        if (!MakeMask()) return CComplex(0, 0);
    }

    for (i = 0; i < (int)cmeshelem.size(); i++) {
        if (labellist[cmeshelem[i].lbl].IsSelected && inttype < 6) {
            a = ElmArea(i) * lc * lc;
            if (ProblemType == 1) {
                for (k = 0; k < 3; k++)
                    r[k] = cmeshnode[cmeshelem[i].p[k]].x * lc;
                R = (r[0] + r[1] + r[2]) / 3.0;
            }

            aecf = AECF(i);
            int blk = cmeshelem[i].blk;

            switch (inttype) {
            case 0: { // Real Power
                if (ProblemType == 1) a *= (2.0 * PI * R); else a *= Depth;
                c  = Re(cmeshelem[i].Jx * conj(cmeshelem[i].Jx) /
                        (blockproplist[blk].Kx / aecf));
                c += Re(cmeshelem[i].Jy * conj(cmeshelem[i].Jy) /
                        (blockproplist[blk].Ky / aecf));
                if (Frequency != 0) c = c / 2.0;
                z += a * c;
                break;
            }
            case 1: { // Reactive Power
                if (Frequency == 0) break;
                if (ProblemType == 1) a *= (2.0 * PI * R); else a *= Depth;
                double ex = eo * Re(blockproplist[blk].ex) / aecf;
                double ey = eo * Re(blockproplist[blk].ey) / aecf;
                Ex = cmeshelem[i].Jx / (blockproplist[blk].Kx / aecf);
                Ey = cmeshelem[i].Jy / (blockproplist[blk].Ky / aecf);
                z += a * (ex * Ex * Ex + ey * Ey * Ey) * PI * Frequency;
                break;
            }
            case 2: { // Apparent Power
                if (ProblemType == 1) a *= (2.0 * PI * R); else a *= Depth;
                Ex = cmeshelem[i].Jx / (blockproplist[blk].Kx / aecf);
                Ey = cmeshelem[i].Jy / (blockproplist[blk].Ky / aecf);
                c = (cmeshelem[i].Jx * Ex + cmeshelem[i].Jy * Ey);
                if (Frequency != 0) c = c / 2.0;
                z += a * c;
                break;
            }
            case 3: { // Time Average Stored Energy
                if (Frequency == 0) break;
                if (ProblemType == 1) a *= (2.0 * PI * R); else a *= Depth;
                c  = -0.25 * Im(cmeshelem[i].Jx * conj(cmeshelem[i].Jx) /
                                (blockproplist[blk].Kx / aecf));
                c -= 0.25 * Im(cmeshelem[i].Jy * conj(cmeshelem[i].Jy) /
                                (blockproplist[blk].Ky / aecf));
                c = c / fabs(2.0 * PI * Frequency);
                z += a * c;
                break;
            }
            case 4: // Cross-section area
                z += a;
                break;
            case 5: // Volume
                if (ProblemType == 1) a *= (2.0 * PI * R); else a *= Depth;
                z += a;
                break;
            default:
                break;
            }
        }

        // Henrotte force/torque — evaluated over ALL elements
        if (inttype >= 6) {
            a = ElmArea(i) * lc * lc;
            if (ProblemType == 1) {
                for (k = 0; k < 3; k++)
                    r[k] = cmeshnode[cmeshelem[i].p[k]].x * lc;
                R = (r[0] + r[1] + r[2]) / 3.0;
                a *= (2.0 * PI * R);
            } else {
                a *= Depth;
            }

            // D-field equivalent: B1 = Jx*eo/Kx, B2 = Jy*eo/Ky
            B1 = cmeshelem[i].Jx * eo / blockproplist[cmeshelem[i].blk].Kx;
            B2 = cmeshelem[i].Jy * eo / blockproplist[cmeshelem[i].blk].Ky;
            c = HenrotteVector(i);

            switch (inttype) {
            case 6: // x (or r) direction Henrotte force, SS part
                if (ProblemType != 0) break;
                y = ((B1 * conj(B1) - B2 * conj(B2)) * Re(c) +
                     2.0 * Re(B1 * conj(B2)) * Im(c)) / (2.0 * eo);
                if (Frequency != 0) y = y / 2.0;
                y = y * AECF(i);
                z += a * y;
                break;

            case 7: // y (or z) direction Henrotte force, SS part
                y = ((B2 * conj(B2) - B1 * conj(B1)) * Im(c) +
                     2.0 * Re(B1 * conj(B2)) * Re(c)) / (2.0 * eo);
                y = y * AECF(i);
                if (Frequency != 0) y = y / 2.0;
                z += a * y;
                break;

            case 8: // x (or r) direction Henrotte force, 2x part
                if (ProblemType != 0) break;
                z += a * (((B1 * B1 - B2 * B2) * Re(c) +
                           2.0 * B1 * B2 * Im(c)) / (4.0 * eo)) * AECF(i);
                break;

            case 9: // y (or z) direction Henrotte force, 2x part
                z += a * (((B2 * B2 - B1 * B1) * Im(c) +
                           2.0 * B1 * B2 * Re(c)) / (4.0 * eo)) * AECF(i);
                break;

            case 10: { // Henrotte torque, SS part
                if (ProblemType != 0) break;
                F1 = (B1 * conj(B1) - B2 * conj(B2)) * Re(c) +
                     2.0 * Re(B1 * conj(B2)) * Im(c);
                F1 = F1 / (2.0 * eo);
                F2 = (B2 * conj(B2) - B1 * conj(B1)) * Im(c) +
                     2.0 * Re(B1 * conj(B2)) * Re(c);
                F2 = F2 / (2.0 * eo);

                CComplex cc(0, 0);
                for (k = 0; k < 3; k++)
                    cc = cc + cmeshnode[cmeshelem[i].p[k]].CC() * lc / 3.0;

                y = Re(cc) * F2 - Im(cc) * F1;
                if (Frequency != 0) y = y / 2.0;
                y = y * AECF(i);
                z += a * y;
                break;
            }
            case 11: { // Henrotte torque, 2x part
                if (ProblemType != 0) break;
                F1 = ((B1 * B1 - B2 * B2) * Re(c) +
                       2.0 * B1 * B2 * Im(c)) / (4.0 * eo);
                F2 = ((B2 * B2 - B1 * B1) * Im(c) +
                       2.0 * B1 * B2 * Re(c)) / (4.0 * eo);

                CComplex cc(0, 0);
                for (k = 0; k < 3; k++)
                    cc = cc + cmeshnode[cmeshelem[i].p[k]].CC() * lc / 3.0;

                z += a * (Re(cc) * F2 - Im(cc) * F1) * AECF(i);
                break;
            }
            default:
                break;
            }
        }
    }

    return z;
}

bool CurrResultsDoc::BlockIntegralIsComplex(int type) const {
    // Genuinely complex (phasor) block-integral types in an AC solve: the
    // oscillating (2x) Henrotte force/torque components (8, 9, 11). All others —
    // real/reactive/apparent power, time-average energy, area, volume, and the
    // steady-state force/torque parts — are real. See co_blockintegral_cases.xlsx.
    switch (type) {
        case 8: case 9: case 11:
            return true;
        default:
            return false;
    }
}

// ================================================================
// LineIntegral — current flow line integrals (complex results)
// Ported from CVIEWDOC.CPP CcviewDoc::LineIntegral
// Types: 0=E.t, 1=J.n, 2=Contour length, 3=Average Voltage
// ================================================================

void CurrResultsDoc::LineIntegral(int inttype, CComplex *z)
{
    double lc = LengthConv();

    z[0] = z[1] = CComplex(0, 0);

    // inttype==0 => E.t (voltage drop)
    if (inttype == 0) {
        CPointVals u;
        int k2 = (int)contour.size();
        GetPointValues(contour[0].re, contour[0].im, u);
        z[0] = u.V;
        GetPointValues(contour[k2 - 1].re, contour[k2 - 1].im, u);
        z[0] = z[0] - u.V;
    }

    // inttype==1 => J.n (total current through contour)
    if (inttype == 1) {
        CComplex n, t, pt, Fn;
        CPointVals v;
        double dz, uu, d;
        int i, j, k2, m, elm;
        int NumPlotPoints = d_LineIntegralPoints;
        bool flg;

        z[0] = z[1] = z[2] = z[3] = CComplex(0, 0);
        for (k2 = 1; k2 < (int)contour.size(); k2++) {
            dz = abs(contour[k2] - contour[k2 - 1]) / (double)NumPlotPoints;
            for (i = 0, elm = -1; i < NumPlotPoints; i++) {
                uu = ((double)i + 0.5) / (double)NumPlotPoints;
                pt = contour[k2 - 1] + uu * (contour[k2] - contour[k2 - 1]);
                t = contour[k2] - contour[k2 - 1];
                t = t / abs(t);
                n = CComplex(-t.im, t.re); // I*t
                pt = pt + n * 1.e-06;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    flg = false;
                    for (j = 0; j < 3; j++)
                        for (m = 0; m < NumList[cmeshelem[elm].p[j]]; m++) {
                            elm = ConList[cmeshelem[elm].p[j]][m];
                            if (InTriangleTest(pt.re, pt.im, elm)) {
                                flg = true; m = 100; j = 3;
                            }
                        }
                    if (!flg) elm = InTriangle(pt.re, pt.im);
                }
                if (elm >= 0 && GetPointValues(pt.re, pt.im, elm, v)) {
                    // J.n: project J vector onto normal direction
                    // Match OF formula: (Jx.re + I*Jy.re)/n + I*(Jx.im + I*Jy.im)/n
                    Fn = (v.Jx.re + CComplex(0, 1) * v.Jy.re) / (n.re + CComplex(0, 1) * n.im)
                       + CComplex(0, 1) * (v.Jx.im + CComplex(0, 1) * v.Jy.im) / (n.re + CComplex(0, 1) * n.im);

                    if (ProblemType == 1)
                        d = 2.0 * PI * pt.re * sqr(lc);
                    else
                        d = Depth * lc;
                    z[0] = z[0] + Fn * dz * d;
                    z[1] = z[1] + dz * d;
                }
            }
        }
        if (abs(z[1]) != 0) z[1] = z[0] / z[1]; // average J.n
    }

    // inttype==2 => Contour length
    if (inttype == 2) {
        int k2 = (int)contour.size();
        for (int i = 0; i < k2 - 1; i++)
            z[0] = z[0] + abs(contour[i + 1] - contour[i]);
        z[0] = z[0] * lc;

        if (ProblemType == 1) {
            for (int i = 0; i < k2 - 1; i++)
                z[1] = z[1] + PI * (contour[i].re + contour[i + 1].re) *
                        abs(contour[i + 1] - contour[i]);
            z[1] = z[1] * lc * lc;
        } else {
            z[1] = z[0] * Depth;
        }
    }

    // inttype==3 => Average Voltage
    if (inttype == 3) {
        CComplex pt;
        CPointVals v;
        double dz, uu, d;
        int i, j, k2, m, elm;
        int NumPlotPoints = d_LineIntegralPoints;
        bool flg;

        for (k2 = 1; k2 < (int)contour.size(); k2++) {
            dz = abs(contour[k2] - contour[k2 - 1]) / (double)NumPlotPoints;
            for (i = 0, elm = -1; i < NumPlotPoints; i++) {
                uu = ((double)i + 0.5) / (double)NumPlotPoints;
                pt = contour[k2 - 1] + uu * (contour[k2] - contour[k2 - 1]);

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    flg = false;
                    for (j = 0; j < 3; j++)
                        for (m = 0; m < NumList[cmeshelem[elm].p[j]]; m++) {
                            elm = ConList[cmeshelem[elm].p[j]][m];
                            if (InTriangleTest(pt.re, pt.im, elm)) {
                                flg = true; m = 100; j = 3;
                            }
                        }
                    if (!flg) elm = InTriangle(pt.re, pt.im);
                }
                if (elm >= 0 && GetPointValues(pt.re, pt.im, elm, v)) {
                    if (ProblemType == 1)
                        d = 2.0 * PI * pt.re * sqr(lc);
                    else
                        d = Depth * lc;
                    z[0] = z[0] + v.V * dz * d;
                    z[1] = z[1] + dz * d;
                }
            }
        }
        if (abs(z[1]) != 0) z[0] = z[0] / z[1]; // average voltage
    }
}

// ================================================================
// BendContour
// ================================================================

void CurrResultsDoc::BendContour(double angle, double anglestep)
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
// GetLineValues — line plot data with complex support
// For AC (Frequency != 0), vals has 3 entries: |mag|, Re, Im
// For DC, vals has 1 entry
// Plot types: 0=V, 1=|J|, 2=J.n, 3=J.t, 4=|E|, 5=E.n, 6=E.t,
//             7=|Jc|, 8=Jc.n, 9=Jc.t, 10=|Jd|, 11=Jd.n, 12=Jd.t
// ================================================================

void CurrResultsDoc::GetLineValues(std::vector<XYPlotPoint> &pts,
                                    int PlotType, int NumPlotPoints)
{
    std::vector<double> q(contour.size(), 0);
    double z, uu, dz;
    CComplex pt, n, t;
    int i, j, k, m, elm;
    CPointVals v;
    bool flg;

    int ncols = (Frequency != 0) ? 3 : 1;
    // Magnitude-only types always 1 column
    if (PlotType == 1 || PlotType == 4 || PlotType == 7 || PlotType == 10)
        ncols = 1;

    for (i = 1, z = 0; i < (int)contour.size(); i++) {
        z += abs(contour[i] - contour[i - 1]);
        q[i] = z;
    }
    dz = z / (NumPlotPoints - 1);

    pts.resize(NumPlotPoints);
    for (auto &p : pts) p.vals.resize(ncols, 0);

    for (i = 0, k = 1, z = 0, elm = -1; i < NumPlotPoints; i++, z += dz) {
        while (z > q[k] && k < (int)contour.size() - 1) k++;
        uu = (z - q[k - 1]) / (q[k] - q[k - 1]);
        pt = contour[k - 1] + uu * (contour[k] - contour[k - 1]);
        t = contour[k] - contour[k - 1];
        t = t / abs(t);
        n = CComplex(-t.im, t.re);
        pt = pt + n * 1.e-06;

        if (elm < 0)
            elm = InTriangle(pt.re, pt.im);
        else if (!InTriangleTest(pt.re, pt.im, elm)) {
            flg = false;
            for (j = 0; j < 3; j++)
                for (m = 0; m < NumList[cmeshelem[elm].p[j]]; m++) {
                    elm = ConList[cmeshelem[elm].p[j]][m];
                    if (InTriangleTest(pt.re, pt.im, elm)) {
                        flg = true; m = 100; j = 3;
                    }
                }
            if (!flg) elm = InTriangle(pt.re, pt.im);
        }

        pts[i].x = z;
        if (elm < 0 || !GetPointValues(pt.re, pt.im, elm, v)) continue;

        // Helper: compute complex scalar from J/E/Jc/Jd dot normal or tangent
        auto dotN = [&](CComplex fx, CComplex fy) -> CComplex { return fx * n.re + fy * n.im; };
        auto dotT = [&](CComplex fx, CComplex fy) -> CComplex { return fx * t.re + fy * t.im; };
        auto storeMagReIm = [&](CComplex val) {
            if (ncols == 3) {
                pts[i].vals[0] = abs(val);
                pts[i].vals[1] = val.re;
                pts[i].vals[2] = val.im;
            } else {
                pts[i].vals[0] = abs(val);
            }
        };
        auto storeMag2 = [&](CComplex fx, CComplex fy) {
            pts[i].vals[0] = sqrt(abs(fx) * abs(fx) + abs(fy) * abs(fy));
        };

        switch (PlotType) {
        case 0: // V
            storeMagReIm(v.V);
            break;
        case 1: storeMag2(v.Jx, v.Jy); break;  // |J|
        case 2: storeMagReIm(dotN(v.Jx, v.Jy)); break;  // J.n
        case 3: storeMagReIm(dotT(v.Jx, v.Jy)); break;  // J.t
        case 4: storeMag2(v.Ex, v.Ey); break;  // |E|
        case 5: storeMagReIm(dotN(v.Ex, v.Ey)); break;  // E.n
        case 6: storeMagReIm(dotT(v.Ex, v.Ey)); break;  // E.t
        case 7: storeMag2(v.Jcx, v.Jcy); break;  // |Jc|
        case 8: storeMagReIm(dotN(v.Jcx, v.Jcy)); break;  // Jc.n
        case 9: storeMagReIm(dotT(v.Jcx, v.Jcy)); break;  // Jc.t
        case 10: storeMag2(v.Jdx, v.Jdy); break;  // |Jd|
        case 11: storeMagReIm(dotN(v.Jdx, v.Jdy)); break;  // Jd.n
        case 12: storeMagReIm(dotT(v.Jdx, v.Jdy)); break;  // Jd.t
        default: break;
        }
    }
}

// ================================================================
// BuildConnectivity
// ================================================================

void CurrResultsDoc::BuildConnectivity()
{
    int nn = (int)cmeshnode.size();
    int ne = (int)cmeshelem.size();

    NumList.assign(nn, 0);
    ConList.clear();
    ConList.resize(nn);

    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int idx = cmeshelem[i].p[j];
            if (idx >= 0 && idx < nn) NumList[idx]++;
        }

    for (int i = 0; i < nn; i++)
        ConList[i].reserve(NumList[i]);
    NumList.assign(nn, 0);

    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int k = cmeshelem[i].p[j];
            if (k >= 0 && k < nn) {
                ConList[k].push_back(i);
                NumList[k]++;
            }
        }

    // Sort each connection list in counter-clockwise order
    for (int i = 0; i < nn; i++) {
        CComplex nodeCC = cmeshnode[i].CC();
        int sz = NumList[i];
        for (int jj = 0; jj < sz; jj++) {
            for (int kk = 0; kk < sz - jj - 1; kk++) {
                CComplex u0 = cmeshelem[ConList[i][kk]].ctr - nodeCC;
                CComplex u1 = cmeshelem[ConList[i][kk + 1]].ctr - nodeCC;
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
// ================================================================

void CurrResultsDoc::FindBoundaryEdges()
{
    static int plus1mod3[3] = {1, 2, 0};
    static int minus1mod3[3] = {2, 0, 1};

    int NumEls = (int)cmeshelem.size();

    for (int i = 0; i < NumEls; i++)
        for (int j = 0; j < 3; j++) {
            cmeshelem[i].n[j] = 0;
            meshelem[i].n[j] = 0;
        }

    for (int i = 0; i < NumEls; i++) {
        for (int j = 0; j < 3; j++) {
            if (cmeshelem[i].n[j] == 0) {
                int orgi = cmeshelem[i].p[plus1mod3[j]];
                int desti = cmeshelem[i].p[minus1mod3[j]];
                bool done = false;
                for (int ni = 0; ni < NumList[orgi]; ni++) {
                    int ei = ConList[orgi][ni];
                    if (ei == i) continue;
                    if (cmeshelem[ei].p[0] == desti ||
                        cmeshelem[ei].p[1] == desti ||
                        cmeshelem[ei].p[2] == desti) {
                        done = true; break;
                    }
                }
                if (!done) {
                    cmeshelem[i].n[j] = 1;
                    meshelem[i].n[j] = 1;
                }
            }
        }
    }
}

// ================================================================
// IsKosher — for axisymmetric r=0 mask boundary handling
// Ported from femm42src/femm/cv_makemask.cpp
// ================================================================

bool CurrResultsDoc::IsKosher(int k) const
{
    if (ProblemType == 0 || meshnode[k].x > 1.e-6) return true;

    int score = 0;
    for (int i = 0; i < NumList[k]; i++) {
        for (int j = 0; j < 3; j++) {
            int n = cmeshelem[ConList[k][i]].p[j];
            if (n != k && meshnode[n].x < 1.e-6) {
                score++;
                if (score > 1) return false;
            }
        }
    }
    return true;
}

// ================================================================
// MakeMask — solve Laplace equation for weighted stress tensor mask
// Ported from femm42src/femm/cv_makemask.cpp
// ================================================================

bool CurrResultsDoc::MakeMask()
{
    // Ported from femm42src/femm/cv_makemask.cpp
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

    // If axisymmetric, does the selection lie along r=0?
    if (ProblemType == 1) {
        for (i = 0; i < NumEls; i++)
            if (labellist[meshelem[i].lbl].IsSelected) {
                for (j = 0; j < 3; j++)
                    if (meshnode[meshelem[i].p[j]].x < 1.e-6) {
                        bOnAxis = true; break;
                    }
                if (bOnAxis) break;
            }

        if (!bOnAxis)
            for (i = 0; i < NumNodes; i++)
                if (meshnode[i].IsSelected && meshnode[i].x < 1.e-6) {
                    bOnAxis = true; break;
                }
    }

    // Sort materials: air (ex==1 && ey==1) vs non-air
    std::vector<int> matflag(blockproplist.size(), 0);
    for (i = 0; i < (int)blockproplist.size(); i++) {
        k = 0;
        if (abs(blockproplist[i].ex) != 1 || abs(blockproplist[i].ey) != 1) k = 1;
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

    // Exterior edges to zero (with IsKosher check for axisymmetric)
    for (i = 0; i < NumEls; i++)
        for (j = 0; j < 3; j++)
            if (meshelem[i].n[j] == 1) {
                k = meshelem[i].p[plus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
                k = meshelem[i].p[minus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
            }

    // Selected blocks to 1; non-air non-selected blocks to 0
    for (i = 0; i < NumEls; i++) {
        if (labellist[meshelem[i].lbl].IsSelected)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 1;
        else if (lblflag[meshelem[i].lbl] != 0)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 0;
    }

    // Nodes with point currents not in selected region should be zero
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

    // Build element matrices
    for (i = 0; i < NumEls; i++) {
        for (j = 0; j < 3; j++) { for (k = 0; k < 3; k++) Me[j][k] = 0; be[j] = 0; }
        for (k = 0; k < 3; k++) n[k] = meshelem[i].p[k];
        p[0] = meshnode[n[1]].y - meshnode[n[2]].y;
        p[1] = meshnode[n[2]].y - meshnode[n[0]].y;
        p[2] = meshnode[n[0]].y - meshnode[n[1]].y;
        q[0] = meshnode[n[2]].x - meshnode[n[1]].x;
        q[1] = meshnode[n[0]].x - meshnode[n[2]].x;
        q[2] = meshnode[n[1]].x - meshnode[n[0]].x;
        a = (p[0] * q[1] - p[1] * q[0]) / 2.0;

        // Validity check: non-air non-selected block must have all nodes at zero
        if (!labellist[meshelem[i].lbl].IsSelected && lblflag[meshelem[i].lbl]) {
            int cnt = 0;
            for (j = 0; j < 3; j++)
                if (L.V[n[j]] == 0) cnt++;
            if (cnt < 3) return false; // invalid selection
        }

        // Weight by region mesh size
        v = labellist[meshelem[i].lbl].MaxArea;
        if (v <= 0) v = sqrt(a);
        else v = sqrt(v);

        for (j = 0; j < 3; j++)
            for (k = 0; k < 3; k++)
                Me[j][k] += v * (p[j] * p[k] + q[j] * q[k]) / a;

        // Process prescribed nodal values
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

        for (j = 0; j < 3; j++) {
            for (k = j; k < 3; k++)
                if (Me[j][k] != 0)
                    L.Put(L.Get(n[j], n[k]) - Me[j][k], n[j], n[k]);
            L.b[n[j]] -= be[j];
        }
    }

    CFknDlg dummyView(true);   // quiet: don't leak PCGSolve progress to the terminal
    if (progressCallback)
        dummyView.m_prog1.onProgress = progressCallback;
    L.TheView = &dummyView;
    if (L.PCGSolve(0) == 0) return false;

    for (i = 0; i < NumNodes; i++) {
        meshnode[i].msk = (L.V[i] > 0.5) ? 1 : 0;
        cmeshnode[i].msk = meshnode[i].msk;
    }

    bHasMask = true;
    return true;
}

// ================================================================
// Geometry query helpers
// ================================================================

int CurrResultsDoc::ClosestInputNode(double x, double y) const
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

double CurrResultsDoc::ShortestDistanceFromSegment(double p, double q, int segm) const
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

void CurrResultsDoc::GetInputCircle(int arcIdx, CComplex &c, double &R) const
{
    CComplex a0(nodelist[arclist[arcIdx].n0].x, nodelist[arclist[arcIdx].n0].y);
    CComplex a1(nodelist[arclist[arcIdx].n1].x, nodelist[arclist[arcIdx].n1].y);
    double d = abs(a1 - a0);
    CComplex t = (a1 - a0) / d;
    double tta = arclist[arcIdx].ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    c = a0 + (d / 2.0 + CComplex(0, sqrt(std::max(0.0, R * R - d * d / 4.0)))) * t;
}

void CurrResultsDoc::GetCircle(const FArcSegment &arc, CComplex &c, double &R) const
{
    CComplex a0(nodelist[arc.n0].x, nodelist[arc.n0].y);
    CComplex a1(nodelist[arc.n1].x, nodelist[arc.n1].y);
    double d = abs(a1 - a0);
    CComplex t = (a1 - a0) / d;
    double tta = arc.ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    c = a0 + (d / 2.0 + CComplex(0, sqrt(std::max(0.0, R * R - d * d / 4.0)))) * t;
}

double CurrResultsDoc::ShortestDistanceFromInputArc(CComplex p, int arcIdx) const
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
