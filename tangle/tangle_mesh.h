// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Meeker

// tangle_mesh.h — Tangle mesh structures and library API
#ifndef TANGLE_MESH_H
#define TANGLE_MESH_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <array>
#include <utility>

// Minimal tangle struct definitions needed by the bridge code.
// These must match the definitions in tangle.cpp.

struct Point {
    double x, y;
    int id;
    int marker;
    std::vector<double> attribs;
    double lfs = -1.0;
};

struct Triangle {
    std::array<int,3> v;
    std::array<int,3> neighbors;
    double region_attrib;
    double region_max_area;
    int marker;
    int generation = 0;
};

struct Segment {
    int v0, v1;
    int marker;
    double lfs = -1.0;
    int pbc_type = -1;
    bool no_split = false;
};

struct Hole { double x, y; };
struct Region { double x, y; double attrib; double max_area; };

struct PBCNodePair {
    int node_a, node_b;
    int type;
};

struct PBCDef {
    // A PBC declaration from the input: the boundary with marker_a pairs with
    // the boundary with marker_b (.poly form; FEMM inputs share one marker).
    int marker_a, marker_b;
    int type; // 0=periodic, 1=anti-periodic
};

struct AGEDef {
    std::string name;
    int format;
    double innerAngle, outerAngle;
    double ri, ro;
    double totalArcLength;
    double cx, cy;
    int n;
    std::vector<int> innerNodes;
    std::vector<int> outerNodes;
};

struct Mesh {
    std::vector<Point>    vertices;
    std::vector<Triangle> triangles;
    std::vector<Segment>  segments;
    std::vector<Hole>     holes;
    std::vector<Region>   regions;
    std::vector<std::pair<int,int>> edges;
    std::vector<char> edge_boundary;     // parallel to edges: 1 if boundary (1 incident triangle)
    std::vector<PBCNodePair> pbc_pairs;
    std::map<int,int> pbc_twin;
    std::map<int,int> pbc_node_type;
    std::vector<PBCDef> pbc_defs;
    std::vector<AGEDef> age_defs;

    void rebuildAdjacency();
    int locateTriangle(double px, double py, int hint = 0) const;
    bool hasEdge(int a, int b, const std::vector<std::vector<int>>& v2t) const;
};

// Library/process return status. Each distinct failure egress has its own code
// so a caller (e.g. femm-qt's "Mesher failed (error N)") can tell a missing file
// from a parse error from a real meshing failure. Keep in sync with the copy in
// tangle.cpp.
enum TangleStatus {
    TANGLE_OK          = 0,  // success
    TANGLE_ERR_USAGE   = 1,  // bad invocation: no args / no input file named
    TANGLE_ERR_NO_FILE = 2,  // input file not found / could not be opened
    TANGLE_ERR_PARSE   = 3,  // file opened but could not be parsed
    TANGLE_ERR_MESH    = 4,  // meshing failed (e.g. a segment could not be enforced)
    TANGLE_ERR_OPTION  = 5,  // option not valid for this input (e.g. -g on a non-FEMM file)
};

// Mesh a .fem file in-memory. Returns TANGLE_OK (0) on success, else one of the
// TANGLE_ERR_* codes above (tangle_mesh_fem yields NO_FILE / PARSE / MESH).
int tangle_mesh_fem(const std::string& inputBase, Mesh& outMesh);

#endif
