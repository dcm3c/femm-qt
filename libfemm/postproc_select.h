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
