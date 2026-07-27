// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// postproc_select.h — conductor selection helpers for the scalar-physics
// post-processors (electrostatics, current flow, heat flow). Pure C++ (no
// MicroPython dependency) so both the Python bindings and the GUI can share
// them. Doc must expose circproplist (with .CircName), nodelist/linelist/
// arclist (with .InConductor and ToggleSelect()), meshnode (with .Q and
// .IsSelected), and ShortestDistanceFromSegment / ShortestDistanceFromInputArc.
#ifndef POSTPROC_SELECT_H
#define POSTPROC_SELECT_H

#include "complex.h"
#include <cmath>
#include <string>

// Flag regions defined by more than one block label, and select them so they
// stand out in the initial view. Returns true if any were found.
//
// A label whose own position lands in a mesh element owned by a DIFFERENT label
// means both labels sit in the same connected region, and only one of them got
// to define it. Following OF, the label selected is the one that won the region
// (meshelem[i].lbl), not the redundant one -- that is the region whose material
// actually took effect, which is what the user needs to look at.
template <class Doc>
bool ppMarkMultiplyDefinedLabels(Doc *doc) {
    bool found = false;
    for (int k = 0; k < (int)doc->labellist.size(); k++) {
        int i = doc->InTriangle(doc->labellist[k].x, doc->labellist[k].y);
        if (i < 0) continue;
        int owner = doc->meshelem[i].lbl;
        if (owner == k) continue;
        if (owner >= 0 && owner < (int)doc->labellist.size())
            doc->labellist[owner].IsSelected = true;
        found = true;
    }
    return found;
}

// Index of the input-geometry node closest to (x,y), or -1 when there are no
// nodes. Mirrors DocumentBase::ClosestNode on the preprocessor side.
template <class NodeList>
int ppClosestNode(double x, double y, const NodeList &nodelist) {
    if (nodelist.empty()) return -1;
    int best = 0;
    double bestd = nodelist[0].GetDistance(x, y);
    for (int i = 1; i < (int)nodelist.size(); i++) {
        double d = nodelist[i].GetDistance(x, y);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

// selectconductor("name") — toggle selection on the geometry entities and mesh
// nodes belonging to the named conductor. Returns true if the name matched a
// conductor (so an empty / "<None>" / unknown name is a no-op returning false).
template <class Doc>
bool ppSelectConductor(const std::string &name, Doc *doc) {
    int j = -1;
    for (int i = 0; i < (int)doc->circproplist.size(); i++) {
        if (name == doc->circproplist[i].CircName) { j = i; break; }
    }
    if (j < 0) return false;

    for (auto &n : doc->nodelist)
        if (n.InConductor == name) n.ToggleSelect();
    for (auto &s : doc->linelist)
        if (s.InConductor == name) s.ToggleSelect();
    for (auto &a : doc->arclist)
        if (a.InConductor == name) a.ToggleSelect();

    for (auto &mn : doc->meshnode)
        if (mn.Q == j) mn.IsSelected = !mn.IsSelected;
    return true;
}

// Clear all selection state (blocks, mesh nodes, and input geometry), matching
// OF's UnselectAll. The mesh-node selection in particular must be cleared or a
// previously selected conductor would leak into the next force/torque integral.
template <class Doc>
void ppClearSelection(Doc *doc) {
    for (auto &lbl : doc->labellist) lbl.IsSelected = false;
    for (auto &mn : doc->meshnode) mn.IsSelected = false;
    for (auto &n : doc->nodelist) n.IsSelected = false;
    for (auto &s : doc->linelist) s.IsSelected = false;
    for (auto &a : doc->arclist) a.IsSelected = false;
    doc->bHasMask = false;
}

// Right-click-near-a-conductor selection (GUI block mode): find the input
// entity (node, segment, or arc) closest to (x, y), and if it is assigned to a
// conductor, toggle that whole conductor. Mirrors OF's CbelaviewView::
// OnRButtonDown in the electrostatics/current-flow/heat post-processors.
// Returns true if a conductor was toggled.
template <class Doc>
bool ppSelectConductorNear(double x, double y, Doc *doc) {
    double dmin = -1.0;
    const std::string *cond = nullptr;

    for (auto &n : doc->nodelist) {
        double d = n.GetDistance(x, y);
        if (dmin < 0 || d < dmin) { dmin = d; cond = &n.InConductor; }
    }
    for (int k = 0; k < (int)doc->linelist.size(); k++) {
        double d = std::fabs(doc->ShortestDistanceFromSegment(x, y, k));
        if (dmin < 0 || d < dmin) { dmin = d; cond = &doc->linelist[k].InConductor; }
    }
    for (int k = 0; k < (int)doc->arclist.size(); k++) {
        double d = doc->ShortestDistanceFromInputArc(CComplex(x, y), k);
        if (dmin < 0 || d < dmin) { dmin = d; cond = &doc->arclist[k].InConductor; }
    }

    if (!cond) return false;                     // no input entities at all
    return ppSelectConductor(*cond, doc);        // false if entity has no conductor
}

#endif // POSTPROC_SELECT_H
