// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_document.cpp — Electrostatics document .fee file I/O
// Ported from femm42src/femm/beladrawDoc.cpp (OnSaveDocument / OnOpenDocument)

#include "elec_document.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

#ifndef _strnicmp
#ifdef _WIN32
// MSVC has _strnicmp natively
#else
#define _strnicmp strncasecmp
#endif
#endif

// Helper: strip keyword from "key = value" lines (returns pointer to value part)
static char* StripKey(char *c)
{
    char *d = strchr(c, '=');
    if (d) return d + 1;
    return c;
}

static char *ParseDbl(char *t, double *f)
{
    if (!t) { *f = 0; return nullptr; }
    while (*t && (*t == ' ' || *t == '\t')) t++;
    *f = strtod(t, &t);
    return t;
}

static char *ParseInt(char *t, int *f)
{
    if (!t) { *f = 0; return nullptr; }
    while (*t && (*t == ' ' || *t == '\t')) t++;
    *f = (int)strtol(t, &t, 10);
    return t;
}

// Extract a quoted string from a value like:  "some name"
static std::string ParseQuotedString(char *v)
{
    if (!v) return "";
    int k = (int)strlen(v);
    // find opening quote
    for (int i = 0; i < k; i++)
        if (v[i] == '\"') { v = v + i + 1; break; }
    k = (int)strlen(v);
    // find closing quote
    if (k > 0)
        for (int i = k - 1; i >= 0; i--)
            if (v[i] == '\"') { v[i] = 0; break; }
    return std::string(v);
}

ElecDocument::ElecDocument()
{
    NewDocument();
}

void ElecDocument::NewDocument()
{
    Precision = 1.e-8;
    MinAngle = 30.0;
    Depth = 1.0;
    LengthUnits = 1;  // mm
    ProblemType = 0;   // planar
    Coords = 0;        // cartesian
    SmartMesh = 1;
    ProblemNote.clear();
    extRo = extRi = extZo = 0.0;

    nodelist.clear();
    linelist.clear();
    arclist.clear();
    blocklist.clear();
    blockproplist.clear();
    lineproplist.clear();
    nodeproplist.clear();
    circproplist.clear();
    clearUndoHistory();
}

// ================================================================
// Open — read a .fee file
// Ported from beladrawDoc::OnOpenDocument
// ================================================================

bool ElecDocument::OpenDocument(const std::string &pathname)
{
    NewDocument();

    FILE *fp = fopen(pathname.c_str(), "rt");
    if (!fp) return false;

    char s[1024], q[1024];
    char *v;
    int i, k, t;

    EPointProp PProp;
    EBoundaryProp BProp;
    EMaterialProp MProp;
    ECircuit CProp;
    FNode node;
    FSegment segm;
    FArcSegment asegm;
    FBlockLabel blk;

    // hook to catch old files where depth wasn't defined
    Depth = -1;

    while (fgets(s, 1024, fp) != nullptr)
    {
        if (sscanf(s, "%s", q) == EOF) q[0] = 0;

        // File format version — recognized and consumed, but not
        // currently used (femm-qt reads only its own current format).
        if (_strnicmp(q, "[format]", 8) == 0) {
            q[0] = 0;
        }

        // Depth
        if (_strnicmp(q, "[depth]", 7) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &Depth);
            q[0] = 0;
        }

        // Precision
        if (_strnicmp(q, "[precision]", 11) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &Precision);
            q[0] = 0;
        }

        // Min angle
        if (_strnicmp(q, "[minangle]", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &MinAngle);
            q[0] = 0;
        }

        // Smart mesh
        if (_strnicmp(q, "[dosmartmesh]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &SmartMesh);
            q[0] = 0;
        }

        // Length units
        if (_strnicmp(q, "[lengthunits]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (_strnicmp(q, "inches", 6) == 0) LengthUnits = 0;
            else if (_strnicmp(q, "millimeters", 11) == 0) LengthUnits = 1;
            else if (_strnicmp(q, "centimeters", 1) == 0) LengthUnits = 2;
            else if (_strnicmp(q, "mils", 4) == 0) LengthUnits = 4;
            else if (_strnicmp(q, "microns", 6) == 0) LengthUnits = 5;
            else if (_strnicmp(q, "meters", 6) == 0) LengthUnits = 3;
            q[0] = 0;
        }

        // Problem type
        if (_strnicmp(q, "[problemtype]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (_strnicmp(q, "planar", 6) == 0) ProblemType = 0;
            if (_strnicmp(q, "axisymmetric", 3) == 0) ProblemType = 1;
            q[0] = 0;
        }

        // Coordinates
        if (_strnicmp(q, "[coordinates]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%s", q);
            if (_strnicmp(q, "cartesian", 4) == 0) Coords = 0;
            if (_strnicmp(q, "polar", 5) == 0) Coords = 1;
            q[0] = 0;
        }

        // Comment
        if (_strnicmp(q, "[comment]", 9) == 0) {
            v = StripKey(s);
            // convert \n literals to actual newlines
            k = (int)strlen(v);
            for (i = 0; i < k - 1; i++)
                if (v[i] == '\\' && v[i + 1] == 'n') {
                    v[i] = '\r'; v[i + 1] = '\n';
                }
            ProblemNote = ParseQuotedString(v);
            q[0] = 0;
        }

        // External region
        if (_strnicmp(q, "[extzo]", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &extZo); q[0] = 0;
        }
        if (_strnicmp(q, "[extro]", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &extRo); q[0] = 0;
        }
        if (_strnicmp(q, "[extri]", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &extRi); q[0] = 0;
        }

        // ----- Point properties -----
        if (_strnicmp(q, "<beginpoint>", 11) == 0) {
            PProp = EPointProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<pointname>", 11) == 0) {
            PProp.PointName = ParseQuotedString(StripKey(s));
            q[0] = 0;
        }
        if (_strnicmp(q, "<vp>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.V); q[0] = 0;
        }
        if (_strnicmp(q, "<qp>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.qp); q[0] = 0;
        }
        if (_strnicmp(q, "<endpoint>", 9) == 0) {
            nodeproplist.push_back(PProp);
            q[0] = 0;
        }

        // ----- Boundary properties -----
        if (_strnicmp(q, "<beginbdry>", 11) == 0) {
            BProp = EBoundaryProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<bdryname>", 10) == 0) {
            BProp.BdryName = ParseQuotedString(StripKey(s));
            q[0] = 0;
        }
        if (_strnicmp(q, "<bdrytype>", 10) == 0) {
            v = StripKey(s); sscanf(v, "%i", &BProp.BdryFormat); q[0] = 0;
        }
        if (_strnicmp(q, "<vs>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.V); q[0] = 0;
        }
        if (_strnicmp(q, "<qs>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.qs); q[0] = 0;
        }
        if (_strnicmp(q, "<c0>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c0); q[0] = 0;
        }
        if (_strnicmp(q, "<c1>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c1); q[0] = 0;
        }
        if (_strnicmp(q, "<endbdry>", 9) == 0) {
            lineproplist.push_back(BProp);
            q[0] = 0;
        }

        // ----- Block (material) properties -----
        if (_strnicmp(q, "<beginblock>", 12) == 0) {
            MProp = EMaterialProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<blockname>", 10) == 0) {
            MProp.BlockName = ParseQuotedString(StripKey(s));
            q[0] = 0;
        }
        if (_strnicmp(q, "<ex>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.ex); q[0] = 0;
        }
        if (_strnicmp(q, "<ey>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.ey); q[0] = 0;
        }
        if (_strnicmp(q, "<qv>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.qv); q[0] = 0;
        }
        if (_strnicmp(q, "<endblock>", 9) == 0) {
            blockproplist.push_back(MProp);
            q[0] = 0;
        }

        // ----- Conductor properties -----
        if (_strnicmp(q, "<beginconductor>", 16) == 0) {
            CProp = ECircuit();
            q[0] = 0;
        }
        if (_strnicmp(q, "<conductorname>", 15) == 0) {
            CProp.CircName = ParseQuotedString(StripKey(s));
            q[0] = 0;
        }
        if (_strnicmp(q, "<vc>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &CProp.V); q[0] = 0;
        }
        if (_strnicmp(q, "<qc>", 4) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &CProp.q); q[0] = 0;
        }
        if (_strnicmp(q, "<conductortype>", 15) == 0) {
            v = StripKey(s); sscanf(v, "%i", &CProp.CircType); q[0] = 0;
        }
        if (_strnicmp(q, "<endconductor>", 14) == 0) {
            circproplist.push_back(CProp);
            q[0] = 0;
        }

        // ----- Points (nodes) -----
        if (_strnicmp(q, "[numpoints]", 11) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                node = FNode();
                v = ParseDbl(s, &node.x);
                v = ParseDbl(v, &node.y);
                v = ParseInt(v, &t);
                if (t == 0) node.BoundaryMarker = "";
                else if (t <= (int)nodeproplist.size())
                    node.BoundaryMarker = nodeproplist[t - 1].PointName;
                v = ParseInt(v, &node.InGroup);
                v = ParseInt(v, &t);
                if (t == 0) node.InConductor = "<None>";
                else if (t <= (int)circproplist.size())
                    node.InConductor = circproplist[t - 1].CircName;
                // Optional 6th column: local feature size
                double ptLfs = -1;
                if (v) ParseDbl(v, &ptLfs);
                if (ptLfs > 0) node.lfs = ptLfs;
                nodelist.push_back(node);
            }
            q[0] = 0;
        }

        // ----- Segments -----
        if (_strnicmp(q, "[numsegments]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                segm = FSegment();
                segm.MaxSideLength = 0;
                segm.Hidden = false;
                segm.InGroup = 0;
                t = 0;

                v = ParseInt(s, &segm.n0);
                v = ParseInt(v, &segm.n1);
                v = ParseDbl(v, &segm.MaxSideLength);
                v = ParseInt(v, &t);
                if (t == 0) segm.BoundaryMarker = "";
                else if (t <= (int)lineproplist.size())
                    segm.BoundaryMarker = lineproplist[t - 1].BdryName;
                int hidden = 0;
                v = ParseInt(v, &hidden);
                segm.Hidden = (hidden != 0);
                v = ParseInt(v, &segm.InGroup);
                v = ParseInt(v, &t);
                if (t == 0) segm.InConductor = "<None>";
                else if (t <= (int)circproplist.size())
                    segm.InConductor = circproplist[t - 1].CircName;

                linelist.push_back(segm);
            }
            q[0] = 0;
        }

        // ----- Arc segments -----
        if (_strnicmp(q, "[numarcsegments]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                asegm = FArcSegment();
                asegm.Hidden = false;
                asegm.MaxSideLength = -1.0;
                asegm.InGroup = 0;
                t = 0;

                v = ParseInt(s, &asegm.n0);
                v = ParseInt(v, &asegm.n1);
                v = ParseDbl(v, &asegm.ArcLength);
                v = ParseDbl(v, &asegm.MaxSideLength);
                v = ParseInt(v, &t);
                if (t == 0) asegm.BoundaryMarker = "";
                else if (t <= (int)lineproplist.size())
                    asegm.BoundaryMarker = lineproplist[t - 1].BdryName;
                int hidden = 0;
                v = ParseInt(v, &hidden);
                asegm.Hidden = (hidden != 0);
                v = ParseInt(v, &asegm.InGroup);
                v = ParseInt(v, &t);
                if (t == 0) asegm.InConductor = "<None>";
                else if (t <= (int)circproplist.size())
                    asegm.InConductor = circproplist[t - 1].CircName;
                asegm.mySideLength = asegm.MaxSideLength;
                arclist.push_back(asegm);
            }
            q[0] = 0;
        }

        // ----- Holes -----
        if (_strnicmp(q, "[numholes]", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            if (k > 0) {
                blk = FBlockLabel();
                blk.BlockType = "<No Mesh>";
                blk.MaxArea = 0;
                for (i = 0; i < k; i++) {
                    fgets(s, 1024, fp);
                    v = ParseDbl(s, &blk.x);
                    v = ParseDbl(v, &blk.y);
                    v = ParseInt(v, &blk.InGroup);
                    blocklist.push_back(blk);
                }
            }
            q[0] = 0;
        }

        // ----- Block labels -----
        if (_strnicmp(q, "[numblocklabels]", 16) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &k);
            for (i = 0; i < k; i++) {
                fgets(s, 1024, fp);
                blk = FBlockLabel();
                blk.MaxArea = 0;
                blk.InGroup = 0;
                blk.IsExternal = false;
                blk.IsDefault = false;
                t = 0;

                v = ParseDbl(s, &blk.x);
                v = ParseDbl(v, &blk.y);
                v = ParseInt(v, &t);
                if (t == 0) blk.BlockType = "<None>";
                else if (t <= (int)blockproplist.size())
                    blk.BlockType = blockproplist[t - 1].BlockName;
                v = ParseDbl(v, &blk.MaxArea);
                if (blk.MaxArea < 0) blk.MaxArea = 0;
                else blk.MaxArea = PI * blk.MaxArea * blk.MaxArea / 4.0;
                v = ParseInt(v, &blk.InGroup);
                int ext = 0;
                v = ParseInt(v, &ext);
                blk.IsDefault = (ext & 2) != 0;
                blk.IsExternal = (ext & 1) != 0;

                blocklist.push_back(blk);
            }
            q[0] = 0;
        }
    }

    fclose(fp);

    // Legacy depth handling (v3.2 files)
    if (Depth == -1) {
        switch (LengthUnits) {
            case 1:  Depth = 1000.0; break;       // mm
            case 2:  Depth = 100.0; break;         // cm
            case 3:  Depth = 1.0; break;           // m
            case 4:  Depth = 1000.0 / 0.0254; break; // mils
            case 5:  Depth = 1.e6; break;          // microns
            default: Depth = 1.0 / 0.0254; break;  // inches
        }
    }

    filePath = pathname;
    return true;
}

// ================================================================
// Save — write a .fee file
// Ported from beladrawDoc::OnSaveDocument
// ================================================================

bool ElecDocument::SaveDocument(const std::string &pathname)
{
    FILE *fp = fopen(pathname.c_str(), "wt");
    if (!fp) return false;

    int i, j, t;

    fprintf(fp, "[Format]      =  1\n");
    fprintf(fp, "[Precision]   =  %.17g\n", Precision);
    fprintf(fp, "[MinAngle]    =  %.17g\n", MinAngle);
    fprintf(fp, "[DoSmartMesh] =  %i\n", SmartMesh);
    fprintf(fp, "[Depth]       =  %.17g\n", Depth);

    fprintf(fp, "[LengthUnits] =  ");
    switch (LengthUnits) {
        case 1:  fprintf(fp, "millimeters\n"); break;
        case 2:  fprintf(fp, "centimeters\n"); break;
        case 3:  fprintf(fp, "meters\n"); break;
        case 4:  fprintf(fp, "mils\n"); break;
        case 5:  fprintf(fp, "microns\n"); break;
        default: fprintf(fp, "inches\n"); break;
    }

    if (ProblemType == 0) fprintf(fp, "[ProblemType] =  planar\n");
    else {
        fprintf(fp, "[ProblemType] =  axisymmetric\n");
        if (extRo != 0 && extRi != 0) {
            fprintf(fp, "[extZo] = %.17g\n", extZo);
            fprintf(fp, "[extRo] = %.17g\n", extRo);
            fprintf(fp, "[extRi] = %.17g\n", extRi);
        }
    }

    if (Coords == 0) fprintf(fp, "[Coordinates] =  cartesian\n");
    else fprintf(fp, "[Coordinates] =  polar\n");

    // Comment — convert newlines to \n literal
    std::string comment = ProblemNote;
    for (size_t ci = 0; ci + 1 < comment.size(); ci++) {
        if (comment[ci] == '\r' && comment[ci + 1] == '\n') {
            comment[ci] = '\\'; comment[ci + 1] = 'n';
        }
    }
    fprintf(fp, "[Comment]     =  \"%s\"\n", comment.c_str());

    // Point properties
    fprintf(fp, "[PointProps]   = %i\n", (int)nodeproplist.size());
    for (i = 0; i < (int)nodeproplist.size(); i++) {
        fprintf(fp, "  <BeginPoint>\n");
        fprintf(fp, "    <PointName> = \"%s\"\n", nodeproplist[i].PointName.c_str());
        fprintf(fp, "    <Vp> = %.17g\n", nodeproplist[i].V);
        fprintf(fp, "    <qp> = %.17g\n", nodeproplist[i].qp);
        fprintf(fp, "  <EndPoint>\n");
    }

    // Boundary properties
    fprintf(fp, "[BdryProps]   = %i\n", (int)lineproplist.size());
    for (i = 0; i < (int)lineproplist.size(); i++) {
        fprintf(fp, "  <BeginBdry>\n");
        fprintf(fp, "    <BdryName> = \"%s\"\n", lineproplist[i].BdryName.c_str());
        fprintf(fp, "    <BdryType> = %i\n", lineproplist[i].BdryFormat);
        fprintf(fp, "    <Vs> = %.17g\n", lineproplist[i].V);
        fprintf(fp, "    <qs> = %.17g\n", lineproplist[i].qs);
        fprintf(fp, "    <c0> = %.17g\n", lineproplist[i].c0);
        fprintf(fp, "    <c1> = %.17g\n", lineproplist[i].c1);
        fprintf(fp, "  <EndBdry>\n");
    }

    // Block (material) properties
    fprintf(fp, "[BlockProps]  = %i\n", (int)blockproplist.size());
    for (i = 0; i < (int)blockproplist.size(); i++) {
        fprintf(fp, "  <BeginBlock>\n");
        fprintf(fp, "    <BlockName> = \"%s\"\n", blockproplist[i].BlockName.c_str());
        fprintf(fp, "    <ex> = %.17g\n", blockproplist[i].ex);
        fprintf(fp, "    <ey> = %.17g\n", blockproplist[i].ey);
        fprintf(fp, "    <qv> = %.17g\n", blockproplist[i].qv);
        fprintf(fp, "  <EndBlock>\n");
    }

    // Conductor properties
    fprintf(fp, "[ConductorProps]  = %i\n", (int)circproplist.size());
    for (i = 0; i < (int)circproplist.size(); i++) {
        fprintf(fp, "  <BeginConductor>\n");
        fprintf(fp, "    <ConductorName> = \"%s\"\n", circproplist[i].CircName.c_str());
        fprintf(fp, "    <Vc> = %.17g\n", circproplist[i].V);
        fprintf(fp, "    <qc> = %.17g\n", circproplist[i].q);
        fprintf(fp, "    <ConductorType> = %i\n", circproplist[i].CircType);
        fprintf(fp, "  <EndConductor>\n");
    }

    // Nodes
    fprintf(fp, "[NumPoints] = %i\n", (int)nodelist.size());
    for (i = 0; i < (int)nodelist.size(); i++) {
        // find point property index
        for (j = 0, t = 0; j < (int)nodeproplist.size(); j++)
            if (nodeproplist[j].PointName == nodelist[i].BoundaryMarker) t = j + 1;
        fprintf(fp, "%.17g\t%.17g\t%i\t%i\t", nodelist[i].x, nodelist[i].y,
                t, nodelist[i].InGroup);
        // find conductor index
        for (j = 0, t = 0; j < (int)circproplist.size(); j++)
            if (circproplist[j].CircName == nodelist[i].InConductor) t = j + 1;
        fprintf(fp, "%i\t", t);
        // local feature size (-1 = auto)
        fprintf(fp, "%.17g\n", nodelist[i].lfs);
    }

    // Segments
    fprintf(fp, "[NumSegments] = %i\n", (int)linelist.size());
    for (i = 0; i < (int)linelist.size(); i++) {
        for (j = 0, t = 0; j < (int)lineproplist.size(); j++)
            if (lineproplist[j].BdryName == linelist[i].BoundaryMarker) t = j + 1;
        fprintf(fp, "%i\t%i\t", linelist[i].n0, linelist[i].n1);
        if (linelist[i].MaxSideLength < 0) fprintf(fp, "-1\t");
        else fprintf(fp, "%.17g\t", linelist[i].MaxSideLength);
        fprintf(fp, "%i\t%i\t%i\t", t, linelist[i].Hidden ? 1 : 0, linelist[i].InGroup);
        for (j = 0, t = 0; j < (int)circproplist.size(); j++)
            if (circproplist[j].CircName == linelist[i].InConductor) t = j + 1;
        fprintf(fp, "%i\n", t);
    }

    // Arc segments
    fprintf(fp, "[NumArcSegments] = %i\n", (int)arclist.size());
    for (i = 0; i < (int)arclist.size(); i++) {
        for (j = 0, t = 0; j < (int)lineproplist.size(); j++)
            if (lineproplist[j].BdryName == arclist[i].BoundaryMarker) t = j + 1;
        fprintf(fp, "%i\t%i\t%.17g\t%.17g\t%i\t%i\t%i\t",
                arclist[i].n0, arclist[i].n1,
                arclist[i].ArcLength, arclist[i].MaxSideLength,
                t, arclist[i].Hidden ? 1 : 0, arclist[i].InGroup);
        for (j = 0, t = 0; j < (int)circproplist.size(); j++)
            if (circproplist[j].CircName == arclist[i].InConductor) t = j + 1;
        fprintf(fp, "%i\t%.17g\n", t, arclist[i].mySideLength);
    }

    // Holes
    int numHoles = 0;
    for (i = 0; i < (int)blocklist.size(); i++)
        if (blocklist[i].BlockType == "<No Mesh>") numHoles++;
    fprintf(fp, "[NumHoles] = %i\n", numHoles);
    for (i = 0; i < (int)blocklist.size(); i++)
        if (blocklist[i].BlockType == "<No Mesh>")
            fprintf(fp, "%.17g\t%.17g\t%i\n", blocklist[i].x, blocklist[i].y,
                    blocklist[i].InGroup);

    // Block labels
    fprintf(fp, "[NumBlockLabels] = %i\n", (int)blocklist.size() - numHoles);
    for (i = 0; i < (int)blocklist.size(); i++) {
        if (blocklist[i].BlockType == "<No Mesh>") continue;
        fprintf(fp, "%.17g\t%.17g\t", blocklist[i].x, blocklist[i].y);
        for (j = 0, t = 0; j < (int)blockproplist.size(); j++)
            if (blockproplist[j].BlockName == blocklist[i].BlockType) t = j + 1;
        fprintf(fp, "%i\t", t);
        if (blocklist[i].MaxArea > 0)
            fprintf(fp, "%.17g\t", sqrt(4.0 * blocklist[i].MaxArea / PI));
        else fprintf(fp, "-1\t");
        fprintf(fp, "%i\t%i\n", blocklist[i].InGroup,
                (blocklist[i].IsExternal ? 1 : 0) + (blocklist[i].IsDefault ? 2 : 0));
    }

    fclose(fp);
    return true;
}
