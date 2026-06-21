// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// load_tangle_mesh.cpp — Bridge between tangle's Mesh and fkn's solver arrays.
// Replaces LoadMesh() file I/O with direct in-memory transfer.

#include "StdAfx.h"
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "mesh.h"
#include "femmedoccore.h"

// Tangle mesh structures (Point, Triangle, Segment, Mesh, etc.)
#include <unordered_map>
#include "tangle_mesh.h"

// LoadMeshFromTangle: populate fkn's solver arrays directly from tangle's Mesh.
// This does exactly what LoadMesh() does but reads from memory instead of files.
//
// Mapping:
//   tangle Point       → fkn CNode (x,y scaled by length units, bc from marker)
//   tangle Triangle    → fkn CElement (p[3], lbl from region_attrib, blk from labellist)
//   tangle edge+marker → fkn CElement.e[3] (boundary condition on edges)
//   tangle PBCNodePair → fkn CCommonPoint (x=node_a, y=node_b, t=type)
//   tangle AGEDef      → fkn CAirGapElement (with quad-point ring data)
int LoadMeshFromTangle(CFemmeDocCore& Doc, const Mesh& tmesh)
{
    int i, j, k;
    double c[] = {2.54, 0.1, 1., 100., 0.00254, 1.e-04};

    // --- Nodes ---
    Doc.NumNodes = (int)tmesh.vertices.size();
    Doc.meshnode = (CNode*)calloc(Doc.NumNodes, sizeof(CNode));
    for (i = 0; i < Doc.NumNodes; i++) {
        // Decode marker: tangle writes (bmark+1) for j+2 encoding
        // Solver expects: if marker > 1, bc = marker-2; else bc = -1
        int marker = tmesh.vertices[i].marker;
        Doc.meshnode[i].bc = (marker > 1) ? (marker - 2) : -1;
        Doc.meshnode[i].x = tmesh.vertices[i].x * c[Doc.LengthUnits];
        Doc.meshnode[i].y = tmesh.vertices[i].y * c[Doc.LengthUnits];
    }

    // --- PBC pairs ---
    Doc.NumPBCs = (int)tmesh.pbc_pairs.size();
    if (Doc.NumPBCs > 0) {
        Doc.pbclist = (CCommonPoint*)calloc(Doc.NumPBCs, sizeof(CCommonPoint));
        for (i = 0; i < Doc.NumPBCs; i++) {
            Doc.pbclist[i].x = tmesh.pbc_pairs[i].node_a;
            Doc.pbclist[i].y = tmesh.pbc_pairs[i].node_b;
            Doc.pbclist[i].t = tmesh.pbc_pairs[i].type;
        }
    }

    // --- AGE definitions ---
    Doc.NumAGEs = (int)tmesh.age_defs.size();
    if (Doc.NumAGEs > 0) {
        Doc.agelist = (CAirGapElement*)calloc(Doc.NumAGEs, sizeof(CAirGapElement));
        for (i = 0; i < Doc.NumAGEs; i++) {
            auto& tage = tmesh.age_defs[i];

            // Copy name
            strncpy(Doc.agelist[i].BdryName, tage.name.c_str(), 79);
            Doc.agelist[i].BdryName[79] = 0;
            Doc.agelist[i].BdryFormat = tage.format;
            Doc.agelist[i].InnerAngle = tage.innerAngle;
            Doc.agelist[i].OuterAngle = tage.outerAngle;
            Doc.agelist[i].ri = tage.ri;
            Doc.agelist[i].ro = tage.ro;
            Doc.agelist[i].totalArcLength = tage.totalArcLength;
            Doc.agelist[i].agc.re = tage.cx;
            Doc.agelist[i].agc.im = tage.cy;

            // Build quad-point ring data (same algorithm as writePbcFile)
            int n = tage.n;
            if (n < 1) { Doc.agelist[i].totalArcElements = 0; continue; }
            double dtta = tage.totalArcLength / n;
            int n0 = (int)(360.0 / dtta + 0.5);
            int n1 = (int)(360.0 / tage.totalArcLength + 0.5);

            Doc.agelist[i].totalArcElements = n;

            struct RingPt { int node; double w; double sign; };
            std::vector<RingPt> innerRing(n0), outerRing(n0);
            int kk = 0;
            for (int jj = 0; jj < n1; jj++) {
                double dL = 1.0;
                if (tage.format == 1 && (jj % 2 != 0)) dL = -1.0;
                double iRotRad = (jj * tage.totalArcLength + tage.innerAngle) * PI / 180.0;
                double oRotRad = (jj * tage.totalArcLength + tage.outerAngle) * PI / 180.0;
                double cosI = cos(iRotRad), sinI = sin(iRotRad);
                double cosO = cos(oRotRad), sinO = sin(oRotRad);
                for (int ii = 0; ii < n; ii++) {
                    double dx = tmesh.vertices[tage.innerNodes[ii]].x - tage.cx;
                    double dy = tmesh.vertices[tage.innerNodes[ii]].y - tage.cy;
                    double rx = cosI * dx - sinI * dy, ry = sinI * dx + cosI * dy;
                    double ang = atan2(ry, rx) * 180.0 / PI;
                    if (ang < 0) ang += 360.0;
                    innerRing[kk].node = tage.innerNodes[ii];
                    innerRing[kk].w = ang / dtta;
                    innerRing[kk].sign = dL;

                    dx = tmesh.vertices[tage.outerNodes[ii]].x - tage.cx;
                    dy = tmesh.vertices[tage.outerNodes[ii]].y - tage.cy;
                    rx = cosO * dx - sinO * dy; ry = sinO * dx + cosO * dy;
                    ang = atan2(ry, rx) * 180.0 / PI;
                    if (ang < 0) ang += 360.0;
                    outerRing[kk].node = tage.outerNodes[ii];
                    outerRing[kk].w = ang / dtta;
                    outerRing[kk].sign = dL;
                    kk++;
                }
            }
            // Bubble sort by angular position
            for (int ii = 0; ii < n0; ii++) {
                bool done = true;
                for (int jj = 0; jj < n0 - 1; jj++) {
                    if (innerRing[jj].w > innerRing[jj + 1].w) { std::swap(innerRing[jj], innerRing[jj + 1]); done = false; }
                }
                if (done) break;
            }
            for (int ii = 0; ii < n0; ii++) {
                bool done = true;
                for (int jj = 0; jj < n0 - 1; jj++) {
                    if (outerRing[jj].w > outerRing[jj + 1].w) { std::swap(outerRing[jj], outerRing[jj + 1]); done = false; }
                }
                if (done) break;
            }

            Doc.agelist[i].InnerShift = innerRing[0].w;
            Doc.agelist[i].OuterShift = outerRing[0].w;

            Doc.agelist[i].node = (CQuadPoint*)calloc(n + 1, sizeof(CQuadPoint));
            for (int ii = 0; ii <= n; ii++) {
                int p1 = ii; if (p1 >= n0) p1 = 0;
                int p0 = p1 - 1; if (p0 < 0) p0 = n0 + p0;
                Doc.agelist[i].node[ii].n0 = innerRing[p0].node;
                Doc.agelist[i].node[ii].w0 = innerRing[p0].sign;
                Doc.agelist[i].node[ii].n1 = innerRing[p1].node;
                Doc.agelist[i].node[ii].w1 = innerRing[p1].sign;
                Doc.agelist[i].node[ii].n2 = outerRing[p0].node;
                Doc.agelist[i].node[ii].w2 = outerRing[p0].sign;
                Doc.agelist[i].node[ii].n3 = outerRing[p1].node;
                Doc.agelist[i].node[ii].w3 = outerRing[p1].sign;
            }
        }
    }

    // --- Elements ---
    Doc.NumEls = (int)tmesh.triangles.size();
    Doc.meshele = (CElement*)calloc(Doc.NumEls, sizeof(CElement));

    int defaultLabel = -1;
    for (i = 0; i < Doc.NumBlockLabels; i++)
        if (Doc.labellist[i].IsDefault) defaultLabel = i;

    for (i = 0; i < Doc.NumEls; i++) {
        Doc.meshele[i].p[0] = tmesh.triangles[i].v[0];
        Doc.meshele[i].p[1] = tmesh.triangles[i].v[1];
        Doc.meshele[i].p[2] = tmesh.triangles[i].v[2];

        // region_attrib is 1-based (k+1 encoding); convert to 0-based label index
        int lbl = (int)tmesh.triangles[i].region_attrib - 1;
        if (lbl < 0) lbl = defaultLabel;
        if (lbl < 0) {
            MsgBox("Material properties have not been defined for all regions.");
            return FALSE;
        }
        Doc.meshele[i].lbl = lbl;
        Doc.meshele[i].blk = Doc.labellist[lbl].BlockType;

        // Initialize edge BCs to "none"
        for (j = 0; j < 3; j++) {
            Doc.meshele[i].e[j] = -1;
            Doc.meshele[i].mu1 = -1.;
            Doc.meshele[i].mu2 = -1.;
        }
    }

    // --- Edge boundary conditions ---
    // Build segment marker lookup from tangle's segments
    // (same as what writeEdgeFile produces, but skip the file)
    {
        // Build edge→segment marker map
        auto edgeKey = [](int a, int b) -> long long {
            if (a > b) std::swap(a, b);
            return ((long long)a << 32) | (unsigned int)b;
        };

        std::unordered_map<long long, int> segMarker;
        for (auto& s : tmesh.segments)
            segMarker[edgeKey(s.v0, s.v1)] = s.marker;

        // Build node→element connectivity for fast edge lookup
        int* nmbr = (int*)calloc(Doc.NumNodes, sizeof(int));
        for (i = 0; i < Doc.NumEls; i++)
            for (j = 0; j < 3; j++)
                nmbr[Doc.meshele[i].p[j]]++;

        int** mbr = (int**)calloc(Doc.NumNodes, sizeof(int*));
        for (i = 0; i < Doc.NumNodes; i++) {
            mbr[i] = (int*)calloc(nmbr[i], sizeof(int));
            nmbr[i] = 0;
        }
        for (i = 0; i < Doc.NumEls; i++)
            for (j = 0; j < 3; j++) {
                k = Doc.meshele[i].p[j];
                mbr[k][nmbr[k]] = i;
                nmbr[k]++;
            }

        // For each edge in the mesh, check if it has a segment marker
        for (auto& edge : tmesh.edges) {
            int n0 = edge.first, n1 = edge.second;
            auto it = segMarker.find(edgeKey(n0, n1));
            if (it == segMarker.end()) continue;
            int marker = it->second;
            if (marker >= 0) continue; // only negative markers are BCs

            int bc = -(marker + 2); // decode -(j+2) encoding

            // Assign BC to the element edges containing this mesh edge
            for (int q = 0; q < nmbr[n0]; q++) {
                CElement& elm = Doc.meshele[mbr[n0][q]];
                if ((elm.p[0] == n0 && elm.p[1] == n1) || (elm.p[0] == n1 && elm.p[1] == n0)) elm.e[0] = bc;
                if ((elm.p[1] == n0 && elm.p[2] == n1) || (elm.p[1] == n1 && elm.p[2] == n0)) elm.e[1] = bc;
                if ((elm.p[2] == n0 && elm.p[0] == n1) || (elm.p[2] == n1 && elm.p[0] == n0)) elm.e[2] = bc;
            }
        }

        // Clean up connectivity
        for (i = 0; i < Doc.NumNodes; i++) free(mbr[i]);
        free(mbr);
        free(nmbr);
    }

    Doc.NumNodesPlus = Doc.NumNodes + Doc.NumFluxConstraints + Doc.NumCurrentConstraints;
    return TRUE;
}

// Combined entry point: mesh .fem file and populate solver arrays directly.
int tangle_mesh_and_load(const std::string& inputBase, CFemmeDocCore& Doc)
{
    Mesh tmesh;
    if (tangle_mesh_fem(inputBase, tmesh) != 0)
        return FALSE;
    return LoadMeshFromTangle(Doc, tmesh);
}
