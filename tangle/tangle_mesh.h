// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker

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
    std::vector<AGEDef> age_defs;

    void rebuildAdjacency();
    int locateTriangle(double px, double py, int hint = 0) const;
    bool hasEdge(int a, int b, const std::vector<std::vector<int>>& v2t) const;
};

// Mesh a .fem file in-memory. Returns 0 on success.
int tangle_mesh_fem(const std::string& inputBase, Mesh& outMesh);

#endif
