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
#include "hsolvdoc.h"

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
int LoadMeshFromTangle(Chsolvdoc& Doc, const Mesh& tmesh)
{
    int i, j, k;
    // Conversion to internal working units (meters) — must match units[] in prob1big.cpp
    double c[] = {0.0254, 0.001, 0.01, 1., 2.54e-05, 1.e-06};

    // --- Nodes ---
    Doc.NumNodes = (int)tmesh.vertices.size();
    Doc.meshnode = (CNode*)calloc(Doc.NumNodes, sizeof(CNode));
    for (i = 0; i < Doc.NumNodes; i++) {
        // Decode marker: FEMM packs (conductor+1) in upper 16 bits,
        // (bc+2) in lower 16 bits.  marker==0 or 1 means no BC/conductor.
        int marker = tmesh.vertices[i].marker;
        if (marker > 1) {
            Doc.meshnode[i].bc = (marker & 0xffff) - 2;
            if (Doc.meshnode[i].bc < 0) Doc.meshnode[i].bc = -1;
            Doc.meshnode[i].InConductor = (marker >> 16) - 1;
        } else {
            Doc.meshnode[i].bc = -1;
            Doc.meshnode[i].InConductor = -1;
        }
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

            // Decode bit-packed marker: -(((conductor+1)<<16) | (bc+2))
            int m = -marker;
            int bc = (m & 0xffff) - 2;
            if (bc < 0) bc = -1;
            int conductor = (m >> 16) - 1;

            // Assign conductor to edge endpoint nodes
            if (conductor >= 0) {
                Doc.meshnode[n0].InConductor = conductor;
                Doc.meshnode[n1].InConductor = conductor;
            }

            // Assign BC to the element edges containing this mesh edge
            if (bc >= 0) {
                for (int q = 0; q < nmbr[n0]; q++) {
                    CElement& elm = Doc.meshele[mbr[n0][q]];
                    if ((elm.p[0] == n0 && elm.p[1] == n1) || (elm.p[0] == n1 && elm.p[1] == n0)) elm.e[0] = bc;
                    if ((elm.p[1] == n0 && elm.p[2] == n1) || (elm.p[1] == n1 && elm.p[2] == n0)) elm.e[1] = bc;
                    if ((elm.p[2] == n0 && elm.p[0] == n1) || (elm.p[2] == n1 && elm.p[0] == n0)) elm.e[2] = bc;
                }
            }
        }

        // Clean up connectivity
        for (i = 0; i < Doc.NumNodes; i++) free(mbr[i]);
        free(mbr);
        free(nmbr);
    }

    // (belasolv doesn't have flux/current constraints)
    return TRUE;
}
