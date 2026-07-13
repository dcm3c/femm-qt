// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// mag_document.cpp — Magnetics document .fem file I/O
// Ported from femm42src/femm/FemmeDoc.cpp (OnSaveDocument / OnOpenDocument)

#include "mag_document.h"
#include "utf8_io.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>

MagDocument::MagDocument()
{
    NewDocument();
}

void MagDocument::NewDocument()
{
    Frequency = 0.0;
    Precision = 1.e-8;
    MinAngle = 30.0;
    Depth = 1.0;
    LengthUnits = 1;  // mm
    ProblemType = 0;   // planar
    Coords = 0;        // cartesian
    ACSolver = 0;
    SmartMesh = 1;
    PrevType = 0;
    PrevSoln.clear();
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

    filePath.clear();
}

// --- Helper: strip key from "[Key] = value" line ---
static char *StripKey(char *s)
{
    char *p = strchr(s, '=');
    if (p) return p + 1;
    return s;
}

// --- Helper: extract quoted string ---
static std::string ExtractQuoted(char *v)
{
    // Find opening quote
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;

    // Find closing quote
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

// ================================================================
// OpenDocument — read .fem file
// ================================================================

bool MagDocument::OpenDocument(const std::string &pathname)
{
    FILE *fp = femm_fopen(pathname, "rt");
    if (!fp) return false;

    NewDocument();

    char s[1024], q[1024];
    char *v;
    FPointProp PProp;
    FBoundaryProp BProp;
    FMaterialProp MProp;
    FCircuit CProp;
    FNode node;
    FSegment segm;
    FArcSegment asegm;
    FBlockLabel blk;

    // Hook to catch old files where depth wasn't defined
    Depth = -1;

    while (fgets(s, 1024, fp) != NULL)
    {
        if (sscanf(s, "%s", q) == EOF) q[0] = 0;

        // Format version
        if (_strnicmp(q, "[format]", 8) == 0) {
            q[0] = 0;
        }

        // Frequency
        if (_strnicmp(q, "[frequency]", 11) == 0) {
            v = StripKey(s);
            sscanf(v, "%lf", &Frequency);
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

        // AC Solver
        if (_strnicmp(q, "[acsolver]", 8) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &ACSolver);
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
            ProblemNote = ExtractQuoted(v);
            // Convert \n sequences to actual newlines
            std::string &note = ProblemNote;
            for (size_t i = 0; i + 1 < note.size(); i++) {
                if (note[i] == '\\' && note[i + 1] == 'n') {
                    note[i] = '\r';
                    note[i + 1] = '\n';
                }
            }
            q[0] = 0;
        }

        // Previous solution
        if (_strnicmp(q, "[prevsoln]", 10) == 0) {
            v = StripKey(s);
            PrevSoln = ExtractQuoted(v);
            if (PrevSoln.empty()) PrevType = 0;
            q[0] = 0;
        }

        if (_strnicmp(q, "[prevtype]", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &PrevType);
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

        // ---- Point Properties ----
        if (_strnicmp(q, "<beginpoint>", 11) == 0) {
            PProp = FPointProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<pointname>", 11) == 0) {
            v = StripKey(s);
            PProp.PointName = ExtractQuoted(v);
            q[0] = 0;
        }
        if (_strnicmp(q, "<I_re>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.Jp.re); q[0] = 0;
        }
        if (_strnicmp(q, "<I_im>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.Jp.im); q[0] = 0;
        }
        if (_strnicmp(q, "<A_re>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.Ap.re); q[0] = 0;
        }
        if (_strnicmp(q, "<A_im>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &PProp.Ap.im); q[0] = 0;
        }
        if (_strnicmp(q, "<endpoint>", 9) == 0) {
            nodeproplist.push_back(PProp);
            q[0] = 0;
        }

        // ---- Boundary Properties ----
        if (_strnicmp(q, "<beginbdry>", 10) == 0) {
            BProp = FBoundaryProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<bdryname>", 10) == 0) {
            v = StripKey(s); BProp.BdryName = ExtractQuoted(v); q[0] = 0;
        }
        if (_strnicmp(q, "<bdrytype>", 10) == 0) {
            v = StripKey(s); sscanf(v, "%i", &BProp.BdryFormat); q[0] = 0;
        }
        if (_strnicmp(q, "<A_0>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.A0); q[0] = 0;
        }
        if (_strnicmp(q, "<A_1>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.A1); q[0] = 0;
        }
        if (_strnicmp(q, "<A_2>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.A2); q[0] = 0;
        }
        if (_strnicmp(q, "<Phi>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.phi); q[0] = 0;
        }
        if (_strnicmp(q, "<c0>", 4) == 0 && _strnicmp(q, "<c0i>", 5) != 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c0.re); q[0] = 0;
        }
        if (_strnicmp(q, "<c0i>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c0.im); q[0] = 0;
        }
        if (_strnicmp(q, "<c1>", 4) == 0 && _strnicmp(q, "<c1i>", 5) != 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c1.re); q[0] = 0;
        }
        if (_strnicmp(q, "<c1i>", 5) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.c1.im); q[0] = 0;
        }
        if (_strnicmp(q, "<Mu_ssd>", 8) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.Mu); q[0] = 0;
        }
        if (_strnicmp(q, "<Sigma_ssd>", 11) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.Sig); q[0] = 0;
        }
        if (_strnicmp(q, "<innerangle>", 12) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.InnerAngle); q[0] = 0;
        }
        if (_strnicmp(q, "<outerangle>", 12) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &BProp.OuterAngle); q[0] = 0;
        }
        if (_strnicmp(q, "<endbdry>", 8) == 0) {
            lineproplist.push_back(BProp);
            q[0] = 0;
        }

        // ---- Block (Material) Properties ----
        if (_strnicmp(q, "<beginblock>", 11) == 0) {
            MProp = FMaterialProp();
            q[0] = 0;
        }
        if (_strnicmp(q, "<blockname>", 11) == 0) {
            v = StripKey(s); MProp.BlockName = ExtractQuoted(v); q[0] = 0;
        }
        if (_strnicmp(q, "<Mu_x>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.mu_x); q[0] = 0;
        }
        if (_strnicmp(q, "<Mu_y>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.mu_y); q[0] = 0;
        }
        if (_strnicmp(q, "<H_c>", 5) == 0 && _strnicmp(q, "<H_cAngle>", 10) != 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.H_c); q[0] = 0;
        }
        if (_strnicmp(q, "<H_cAngle>", 10) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_m); q[0] = 0;
        }
        if (_strnicmp(q, "<J_re>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.re); q[0] = 0;
        }
        if (_strnicmp(q, "<J_im>", 6) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.im); q[0] = 0;
        }
        if (_strnicmp(q, "<Sigma>", 7) == 0 && _strnicmp(q, "<Sigma_ssd>", 11) != 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Cduct); q[0] = 0;
        }
        if (_strnicmp(q, "<d_lam>", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Lam_d); q[0] = 0;
        }
        if (_strnicmp(q, "<Phi_h>", 7) == 0 && _strnicmp(q, "<Phi_hx>", 8) != 0 && _strnicmp(q, "<Phi_hy>", 8) != 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hn); q[0] = 0;
        }
        if (_strnicmp(q, "<Phi_hx>", 8) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hx); q[0] = 0;
        }
        if (_strnicmp(q, "<Phi_hy>", 8) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hy); q[0] = 0;
        }
        if (_strnicmp(q, "<LamType>", 9) == 0) {
            v = StripKey(s); sscanf(v, "%i", &MProp.LamType); q[0] = 0;
        }
        if (_strnicmp(q, "<LamFill>", 9) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.LamFill); q[0] = 0;
        }
        if (_strnicmp(q, "<NStrands>", 10) == 0) {
            v = StripKey(s); sscanf(v, "%i", &MProp.NStrands); q[0] = 0;
        }
        if (_strnicmp(q, "<WireD>", 7) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &MProp.WireD); q[0] = 0;
        }
        if (_strnicmp(q, "<BHPoints>", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &MProp.BHpoints);
            MProp.BHdata.resize(MProp.BHpoints);
            for (int j = 0; j < MProp.BHpoints; j++) {
                fgets(s, 1024, fp);
                sscanf(s, "%lf %lf", &MProp.BHdata[j].re, &MProp.BHdata[j].im);
            }
            q[0] = 0;
        }
        if (_strnicmp(q, "<endblock>", 9) == 0) {
            blockproplist.push_back(MProp);
            q[0] = 0;
        }

        // ---- Circuit Properties ----
        if (_strnicmp(q, "<begincircuit>", 13) == 0) {
            CProp = FCircuit();
            q[0] = 0;
        }
        if (_strnicmp(q, "<circuitname>", 13) == 0) {
            v = StripKey(s); CProp.CircName = ExtractQuoted(v); q[0] = 0;
        }
        if (_strnicmp(q, "<TotalAmps_re>", 14) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &CProp.Amps.re); q[0] = 0;
        }
        if (_strnicmp(q, "<TotalAmps_im>", 14) == 0) {
            v = StripKey(s); sscanf(v, "%lf", &CProp.Amps.im); q[0] = 0;
        }
        if (_strnicmp(q, "<CircuitType>", 13) == 0) {
            v = StripKey(s); sscanf(v, "%i", &CProp.CircType); q[0] = 0;
        }
        if (_strnicmp(q, "<endcircuit>", 11) == 0) {
            circproplist.push_back(CProp);
            q[0] = 0;
        }

        // ---- Geometry: Nodes ----
        int numItems;
        if (_strnicmp(q, "[numpoints]", 11) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &numItems);
            for (int i = 0; i < numItems; i++) {
                fgets(s, 1024, fp);
                node = FNode();
                int propIdx = 0;
                double ptLfs = -1.0;
                sscanf(s, "%lf %lf %i %i %lf", &node.x, &node.y, &propIdx, &node.InGroup, &ptLfs);
                node.lfs = ptLfs;
                if (propIdx > 0 && propIdx <= (int)nodeproplist.size())
                    node.BoundaryMarker = nodeproplist[propIdx - 1].PointName;
                nodelist.push_back(node);
            }
            q[0] = 0;
        }

        // ---- Geometry: Segments ----
        if (_strnicmp(q, "[numsegments]", 13) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &numItems);
            for (int i = 0; i < numItems; i++) {
                fgets(s, 1024, fp);
                segm = FSegment();
                int propIdx = 0;
                double msl;
                int hidden = 0;
                sscanf(s, "%i %i %lf %i %i %i",
                    &segm.n0, &segm.n1, &msl, &propIdx, &hidden, &segm.InGroup);
                segm.MaxSideLength = msl;
                segm.Hidden = (hidden != 0);
                if (propIdx > 0 && propIdx <= (int)lineproplist.size())
                    segm.BoundaryMarker = lineproplist[propIdx - 1].BdryName;
                linelist.push_back(segm);
            }
            q[0] = 0;
        }

        // ---- Geometry: Arc Segments ----
        if (_strnicmp(q, "[numarcsegments]", 16) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &numItems);
            for (int i = 0; i < numItems; i++) {
                fgets(s, 1024, fp);
                asegm = FArcSegment();
                int propIdx = 0;
                int hidden = 0;
                sscanf(s, "%i %i %lf %lf %i %i %i %lf",
                    &asegm.n0, &asegm.n1, &asegm.ArcLength, &asegm.MaxSideLength,
                    &propIdx, &hidden, &asegm.InGroup, &asegm.mySideLength);
                asegm.Hidden = (hidden != 0);
                if (propIdx > 0 && propIdx <= (int)lineproplist.size())
                    asegm.BoundaryMarker = lineproplist[propIdx - 1].BdryName;
                arclist.push_back(asegm);
            }
            q[0] = 0;
        }

        // ---- Geometry: Holes ----
        if (_strnicmp(q, "[numholes]", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &numItems);
            for (int i = 0; i < numItems; i++) {
                fgets(s, 1024, fp);
                blk = FBlockLabel();
                sscanf(s, "%lf %lf %i", &blk.x, &blk.y, &blk.InGroup);
                blk.BlockType = "<No Mesh>";
                blocklist.push_back(blk);
            }
            q[0] = 0;
        }

        // ---- Geometry: Block Labels ----
        if (_strnicmp(q, "[numblocklabels]", 16) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &numItems);
            for (int i = 0; i < numItems; i++) {
                fgets(s, 1024, fp);
                blk = FBlockLabel();
                int blockIdx = 0, circIdx = 0, extFlags = 0;
                double meshSize;
                char magDirFctn[256] = "";
                int nRead = sscanf(s, "%lf %lf %i %lf %i %lf %i %i %i %255[^\n]",
                    &blk.x, &blk.y, &blockIdx, &meshSize,
                    &circIdx, &blk.MagDir, &blk.InGroup, &blk.Turns,
                    &extFlags, magDirFctn);

                if (meshSize > 0)
                    blk.MaxArea = PI * meshSize * meshSize / 4.0;
                else
                    blk.MaxArea = 0;

                if (blockIdx > 0 && blockIdx <= (int)blockproplist.size())
                    blk.BlockType = blockproplist[blockIdx - 1].BlockName;
                if (circIdx > 0 && circIdx <= (int)circproplist.size())
                    blk.InCircuit = circproplist[circIdx - 1].CircName;

                blk.IsExternal = (extFlags & 1) != 0;
                blk.IsDefault = (extFlags & 2) != 0;

                if (nRead >= 10 && magDirFctn[0] != 0) {
                    std::string fctn(magDirFctn);
                    // Strip quotes
                    if (fctn.size() >= 2 && fctn.front() == '"') {
                        fctn = fctn.substr(1);
                        auto pos = fctn.rfind('"');
                        if (pos != std::string::npos)
                            fctn = fctn.substr(0, pos);
                    }
                    blk.MagDirFctn = fctn;
                }

                blocklist.push_back(blk);
            }
            q[0] = 0;
        }
    }

    fclose(fp);

    // Handle old files where depth wasn't defined
    if (Depth < 0) Depth = 1.0;

    filePath = pathname;
    return true;
}

// ================================================================
// SaveDocument — write .fem file
// ================================================================

bool MagDocument::SaveDocument(const std::string &pathname)
{
    FILE *fp = femm_fopen(pathname, "wt");
    if (!fp) return false;

    int i, j, t;

    fprintf(fp, "[Format]      =  4.0\n");
    fprintf(fp, "[Frequency]   =  %.17g\n", Frequency);
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

    if (ProblemType == 0)
        fprintf(fp, "[ProblemType] =  planar\n");
    else {
        fprintf(fp, "[ProblemType] =  axisymmetric\n");
        if (extRo != 0 && extRi != 0) {
            fprintf(fp, "[extZo] = %.17g\n", extZo);
            fprintf(fp, "[extRo] = %.17g\n", extRo);
            fprintf(fp, "[extRi] = %.17g\n", extRi);
        }
    }

    if (Coords == 0)
        fprintf(fp, "[Coordinates] =  cartesian\n");
    else
        fprintf(fp, "[Coordinates] =  polar\n");

    // Comment — escape newlines
    std::string note = ProblemNote;
    for (size_t idx = 0; idx < note.size(); idx++) {
        if (note[idx] == '\r') note[idx] = '\\';
        if (note[idx] == '\n') note[idx] = 'n';
    }

    fprintf(fp, "[ACSolver]    =  %i\n", ACSolver);
    fprintf(fp, "[PrevType]    =  %i\n", PrevType);
    fprintf(fp, "[PrevSoln]    =  \"%s\"\n", PrevSoln.c_str());
    fprintf(fp, "[Comment]     =  \"%s\"\n", note.c_str());

    // Point properties
    fprintf(fp, "[PointProps]   = %i\n", (int)nodeproplist.size());
    for (i = 0; i < (int)nodeproplist.size(); i++) {
        fprintf(fp, "  <BeginPoint>\n");
        fprintf(fp, "    <PointName> = \"%s\"\n", nodeproplist[i].PointName.c_str());
        fprintf(fp, "    <I_re> = %.17g\n", nodeproplist[i].Jp.re);
        fprintf(fp, "    <I_im> = %.17g\n", nodeproplist[i].Jp.im);
        fprintf(fp, "    <A_re> = %.17g\n", nodeproplist[i].Ap.re);
        fprintf(fp, "    <A_im> = %.17g\n", nodeproplist[i].Ap.im);
        fprintf(fp, "  <EndPoint>\n");
    }

    // Boundary properties
    fprintf(fp, "[BdryProps]   = %i\n", (int)lineproplist.size());
    for (i = 0; i < (int)lineproplist.size(); i++) {
        fprintf(fp, "  <BeginBdry>\n");
        fprintf(fp, "    <BdryName> = \"%s\"\n", lineproplist[i].BdryName.c_str());
        fprintf(fp, "    <BdryType> = %i\n", lineproplist[i].BdryFormat);
        fprintf(fp, "    <A_0> = %.17g\n", lineproplist[i].A0);
        fprintf(fp, "    <A_1> = %.17g\n", lineproplist[i].A1);
        fprintf(fp, "    <A_2> = %.17g\n", lineproplist[i].A2);
        fprintf(fp, "    <Phi> = %.17g\n", lineproplist[i].phi);
        fprintf(fp, "    <c0> = %.17g\n", lineproplist[i].c0.re);
        fprintf(fp, "    <c0i> = %.17g\n", lineproplist[i].c0.im);
        fprintf(fp, "    <c1> = %.17g\n", lineproplist[i].c1.re);
        fprintf(fp, "    <c1i> = %.17g\n", lineproplist[i].c1.im);
        fprintf(fp, "    <Mu_ssd> = %.17g\n", lineproplist[i].Mu);
        fprintf(fp, "    <Sigma_ssd> = %.17g\n", lineproplist[i].Sig);
        fprintf(fp, "    <innerangle> = %.17g\n", lineproplist[i].InnerAngle);
        fprintf(fp, "    <outerangle> = %.17g\n", lineproplist[i].OuterAngle);
        fprintf(fp, "  <EndBdry>\n");
    }

    // Block (material) properties
    fprintf(fp, "[BlockProps]  = %i\n", (int)blockproplist.size());
    for (i = 0; i < (int)blockproplist.size(); i++) {
        fprintf(fp, "  <BeginBlock>\n");
        fprintf(fp, "    <BlockName> = \"%s\"\n", blockproplist[i].BlockName.c_str());
        fprintf(fp, "    <Mu_x> = %.17g\n", blockproplist[i].mu_x);
        fprintf(fp, "    <Mu_y> = %.17g\n", blockproplist[i].mu_y);
        fprintf(fp, "    <H_c> = %.17g\n", blockproplist[i].H_c);
        fprintf(fp, "    <H_cAngle> = %.17g\n", blockproplist[i].Theta_m);
        fprintf(fp, "    <J_re> = %.17g\n", blockproplist[i].Jsrc.re);
        fprintf(fp, "    <J_im> = %.17g\n", blockproplist[i].Jsrc.im);
        fprintf(fp, "    <Sigma> = %.17g\n", blockproplist[i].Cduct);
        fprintf(fp, "    <d_lam> = %.17g\n", blockproplist[i].Lam_d);
        fprintf(fp, "    <Phi_h> = %.17g\n", blockproplist[i].Theta_hn);
        fprintf(fp, "    <Phi_hx> = %.17g\n", blockproplist[i].Theta_hx);
        fprintf(fp, "    <Phi_hy> = %.17g\n", blockproplist[i].Theta_hy);
        fprintf(fp, "    <LamType> = %i\n", blockproplist[i].LamType);
        fprintf(fp, "    <LamFill> = %.17g\n", blockproplist[i].LamFill);
        fprintf(fp, "    <NStrands> = %i\n", blockproplist[i].NStrands);
        fprintf(fp, "    <WireD> = %.17g\n", blockproplist[i].WireD);
        fprintf(fp, "    <BHPoints> = %i\n", blockproplist[i].BHpoints);
        for (j = 0; j < blockproplist[i].BHpoints; j++)
            fprintf(fp, "      %.17g\t%.17g\n",
                blockproplist[i].BHdata[j].re, blockproplist[i].BHdata[j].im);
        fprintf(fp, "  <EndBlock>\n");
    }

    // Circuit properties
    fprintf(fp, "[CircuitProps]  = %i\n", (int)circproplist.size());
    for (i = 0; i < (int)circproplist.size(); i++) {
        fprintf(fp, "  <BeginCircuit>\n");
        fprintf(fp, "    <CircuitName> = \"%s\"\n", circproplist[i].CircName.c_str());
        fprintf(fp, "    <TotalAmps_re> = %.17g\n", circproplist[i].Amps.re);
        fprintf(fp, "    <TotalAmps_im> = %.17g\n", circproplist[i].Amps.im);
        fprintf(fp, "    <CircuitType> = %i\n", circproplist[i].CircType);
        fprintf(fp, "  <EndCircuit>\n");
    }

    // Nodes
    fprintf(fp, "[NumPoints] = %i\n", (int)nodelist.size());
    for (i = 0; i < (int)nodelist.size(); i++) {
        t = 0;
        for (j = 0; j < (int)nodeproplist.size(); j++)
            if (nodeproplist[j].PointName == nodelist[i].BoundaryMarker) t = j + 1;
        if (nodelist[i].lfs > 0)
            fprintf(fp, "%.17g\t%.17g\t%i\t%i\t%.17g\n",
                nodelist[i].x, nodelist[i].y, t, nodelist[i].InGroup, nodelist[i].lfs);
        else
            fprintf(fp, "%.17g\t%.17g\t%i\t%i\n",
                nodelist[i].x, nodelist[i].y, t, nodelist[i].InGroup);
    }

    // Segments
    fprintf(fp, "[NumSegments] = %i\n", (int)linelist.size());
    for (i = 0; i < (int)linelist.size(); i++) {
        t = 0;
        for (j = 0; j < (int)lineproplist.size(); j++)
            if (lineproplist[j].BdryName == linelist[i].BoundaryMarker) t = j + 1;
        fprintf(fp, "%i\t%i\t", linelist[i].n0, linelist[i].n1);
        if (linelist[i].MaxSideLength < 0) fprintf(fp, "-1\t");
        else fprintf(fp, "%.17g\t", linelist[i].MaxSideLength);
        fprintf(fp, "%i\t%i\t%i\n", t, linelist[i].Hidden ? 1 : 0, linelist[i].InGroup);
    }

    // Arc segments
    fprintf(fp, "[NumArcSegments] = %i\n", (int)arclist.size());
    for (i = 0; i < (int)arclist.size(); i++) {
        t = 0;
        for (j = 0; j < (int)lineproplist.size(); j++)
            if (lineproplist[j].BdryName == arclist[i].BoundaryMarker) t = j + 1;
        fprintf(fp, "%i\t%i\t%.17g\t%.17g\t%i\t%i\t%i\t%.17g\n",
            arclist[i].n0, arclist[i].n1,
            arclist[i].ArcLength, arclist[i].MaxSideLength,
            t, arclist[i].Hidden ? 1 : 0, arclist[i].InGroup,
            arclist[i].mySideLength);
    }

    // Holes (block labels with BlockType == "<No Mesh>")
    int numHoles = 0;
    for (i = 0; i < (int)blocklist.size(); i++)
        if (blocklist[i].BlockType == "<No Mesh>") numHoles++;
    fprintf(fp, "[NumHoles] = %i\n", numHoles);
    for (i = 0; i < (int)blocklist.size(); i++)
        if (blocklist[i].BlockType == "<No Mesh>")
            fprintf(fp, "%.17g\t%.17g\t%i\n",
                blocklist[i].x, blocklist[i].y, blocklist[i].InGroup);

    // Block labels (non-hole)
    fprintf(fp, "[NumBlockLabels] = %i\n", (int)blocklist.size() - numHoles);
    for (i = 0; i < (int)blocklist.size(); i++) {
        if (blocklist[i].BlockType == "<No Mesh>") continue;

        fprintf(fp, "%.17g\t%.17g\t", blocklist[i].x, blocklist[i].y);

        t = 0;
        for (j = 0; j < (int)blockproplist.size(); j++)
            if (blockproplist[j].BlockName == blocklist[i].BlockType) t = j + 1;
        fprintf(fp, "%i\t", t);

        if (blocklist[i].MaxArea > 0)
            fprintf(fp, "%.17g\t", sqrt(4.0 * blocklist[i].MaxArea / PI));
        else
            fprintf(fp, "-1\t");

        t = 0;
        for (j = 0; j < (int)circproplist.size(); j++)
            if (circproplist[j].CircName == blocklist[i].InCircuit) t = j + 1;

        int extFlags = (blocklist[i].IsExternal ? 1 : 0) + (blocklist[i].IsDefault ? 2 : 0);
        fprintf(fp, "%i\t%.17g\t%i\t%i\t%i",
            t, blocklist[i].MagDir, blocklist[i].InGroup,
            blocklist[i].Turns, extFlags);

        if (!blocklist[i].MagDirFctn.empty())
            fprintf(fp, "\t\"%s\"", blocklist[i].MagDirFctn.c_str());
        fprintf(fp, "\n");
    }

    fclose(fp);
    return true;
}
