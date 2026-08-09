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

            // Keep the raw ring node lists (angle-invariant) and derive the
            // angle-baked quad-point pairing through the shared builder --
            // the same routine the transient mesh-reuse path uses, which is
            // what guarantees the two paths pair the gap identically. The
            // lists are persisted through the .ans ([AgeRings]) so a later
            // step can re-pair at a new rotor angle without remeshing.
            int n = tage.n;
            if (n < 1) { Doc.agelist[i].totalArcElements = 0; continue; }
            Doc.agelist[i].totalArcElements = n;
            Doc.agelist[i].ringInner = (int*)calloc(n, sizeof(int));
            Doc.agelist[i].ringOuter = (int*)calloc(n, sizeof(int));
            std::vector<double> ix(n), iy(n), ox(n), oy(n);
            for (int ii = 0; ii < n; ii++) {
                Doc.agelist[i].ringInner[ii] = tage.innerNodes[ii];
                Doc.agelist[i].ringOuter[ii] = tage.outerNodes[ii];
                ix[ii] = tmesh.vertices[tage.innerNodes[ii]].x;
                iy[ii] = tmesh.vertices[tage.innerNodes[ii]].y;
                ox[ii] = tmesh.vertices[tage.outerNodes[ii]].x;
                oy[ii] = tmesh.vertices[tage.outerNodes[ii]].y;
            }
            // tmesh coordinates and tage.cx/cy share the problem's units;
            // only differences enter the builder.
            Doc.BuildAgeRing(Doc.agelist[i], ix.data(), iy.data(),
                             ox.data(), oy.data(), tage.cx, tage.cy);
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
            return -2;   // sentinel: no material assigned (distinct from generic FALSE)
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

    Doc.SetNumNodesPlus();
    return TRUE;
}

// Combined entry point: mesh .fem file and populate solver arrays directly.
int tangle_mesh_and_load(const std::string& inputBase, CFemmeDocCore& Doc)
{
    Mesh tmesh;
    // Full .fem name -> mesh the magnetics geometry (inputBase is the base, no
    // extension); fkn is the magnetics solver.
    int trc = tangle_mesh_fem(inputBase + ".fem", tmesh);
    if (trc != 0) return trc;   // propagate TangleStatus (2/3/4) so main can exit-code it
    return LoadMeshFromTangle(Doc, tmesh);
}
