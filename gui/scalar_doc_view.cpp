// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_doc_view.cpp — ScalarDocView implementation
#include "scalar_doc_view.h"
#include "heat_results_doc.h"
#include "curr_results_doc.h"

// ================================================================
// Factory methods
// ================================================================

ScalarDocView ScalarDocView::fromElec(ElecResultsDoc *doc)
{
    ScalarDocView v;
    v.meshnode = &doc->meshnode;
    v.meshelem = &doc->meshelem;
    v.labellist = &doc->labellist;
    v.nodelist = &doc->nodelist;
    v.linelist = &doc->linelist;
    v.arclist = &doc->arclist;
    v.PlotBounds = doc->PlotBounds;
    v.d_PlotBounds = doc->d_PlotBounds;
    v.A_Low = &doc->A_Low;
    v.A_High = &doc->A_High;
    v.A_lb = &doc->A_lb;
    v.A_ub = &doc->A_ub;
    v.Smooth = &doc->Smooth;
    v.bHasMask = &doc->bHasMask;
    v.ProblemType = &doc->ProblemType;
    v.Depth = &doc->Depth;
    v.filePath = &doc->filePath;
    v.contour = &doc->contour;
    v.physicsType = 1;
    v.elecDoc = doc;
    v.heatDoc = nullptr;
    return v;
}

ScalarDocView ScalarDocView::fromHeat(HeatResultsDoc *doc)
{
    ScalarDocView v;
    v.meshnode = &doc->meshnode;
    v.meshelem = &doc->meshelem;
    v.labellist = &doc->labellist;
    v.nodelist = &doc->nodelist;
    v.linelist = &doc->linelist;
    v.arclist = &doc->arclist;
    v.PlotBounds = doc->PlotBounds;
    v.d_PlotBounds = doc->d_PlotBounds;
    v.A_Low = &doc->A_Low;
    v.A_High = &doc->A_High;
    v.A_lb = &doc->A_lb;
    v.A_ub = &doc->A_ub;
    v.Smooth = &doc->Smooth;
    v.bHasMask = &doc->bHasMask;
    v.ProblemType = &doc->ProblemType;
    v.Depth = &doc->Depth;
    v.filePath = &doc->filePath;
    v.contour = &doc->contour;
    v.physicsType = 2;
    v.elecDoc = nullptr;
    v.heatDoc = doc;
    return v;
}

ScalarDocView ScalarDocView::fromCurrentFlow(CurrResultsDoc *doc)
{
    ScalarDocView v;
    v.meshnode = &doc->meshnode;
    v.meshelem = &doc->meshelem;
    v.labellist = &doc->labellist;
    v.nodelist = &doc->nodelist;
    v.linelist = &doc->linelist;
    v.arclist = &doc->arclist;
    v.PlotBounds = doc->PlotBounds;
    v.d_PlotBounds = doc->d_PlotBounds;
    v.A_Low = &doc->A_Low;
    v.A_High = &doc->A_High;
    v.A_lb = &doc->A_lb;
    v.A_ub = &doc->A_ub;
    v.Smooth = &doc->Smooth;
    v.bHasMask = &doc->bHasMask;
    v.ProblemType = &doc->ProblemType;
    v.Depth = &doc->Depth;
    v.filePath = &doc->filePath;
    v.contour = &doc->contour;
    v.physicsType = 3;
    v.elecDoc = nullptr;
    v.heatDoc = nullptr;
    v.currDoc = doc;
    return v;
}

// ================================================================
// Block property accessors
// ================================================================

double ScalarDocView::blockPropEx(int i) const
{
    if (currDoc) return currDoc->blockproplist[i].ox;
    if (heatDoc) return heatDoc->blockproplist[i].ex;
    return elecDoc->blockproplist[i].ex;
}

double ScalarDocView::blockPropEy(int i) const
{
    if (currDoc) return currDoc->blockproplist[i].oy;
    if (heatDoc) return heatDoc->blockproplist[i].ey;
    return elecDoc->blockproplist[i].ey;
}

std::string ScalarDocView::blockPropName(int i) const
{
    if (currDoc) return currDoc->blockproplist[i].BlockName;
    if (heatDoc) return heatDoc->blockproplist[i].BlockName;
    return elecDoc->blockproplist[i].BlockName;
}

int ScalarDocView::blockPropCount() const
{
    if (currDoc) return (int)currDoc->blockproplist.size();
    if (heatDoc) return (int)heatDoc->blockproplist.size();
    return (int)elecDoc->blockproplist.size();
}

// ================================================================
// Method dispatch
// ================================================================

int ScalarDocView::InTriangle(double x, double y)
{
    if (currDoc) return currDoc->InTriangle(x, y);
    if (heatDoc) return heatDoc->InTriangle(x, y);
    return elecDoc->InTriangle(x, y);
}

bool ScalarDocView::GetPointValues(double x, double y, EPointVals &u)
{
    if (currDoc) return currDoc->GetPointValues(x, y, u);
    if (heatDoc) return heatDoc->GetPointValues(x, y, u);
    return elecDoc->GetPointValues(x, y, u);
}

bool ScalarDocView::GetPointValues(double x, double y, int k, EPointVals &u)
{
    if (currDoc) return currDoc->GetPointValues(x, y, k, u);
    if (heatDoc) return heatDoc->GetPointValues(x, y, k, u);
    return elecDoc->GetPointValues(x, y, k, u);
}

void ScalarDocView::GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const
{
    if (currDoc) { currDoc->GetCircle(asegm, c, R); return; }
    if (heatDoc) { heatDoc->GetCircle(asegm, c, R); return; }
    elecDoc->GetCircle(asegm, c, R);
}

int ScalarDocView::ClosestInputNode(double x, double y) const
{
    if (currDoc) return currDoc->ClosestInputNode(x, y);
    if (heatDoc) return heatDoc->ClosestInputNode(x, y);
    return elecDoc->ClosestInputNode(x, y);
}

double ScalarDocView::ShortestDistanceFromSegment(double p, double q, int segm) const
{
    if (currDoc) return currDoc->ShortestDistanceFromSegment(p, q, segm);
    if (heatDoc) return heatDoc->ShortestDistanceFromSegment(p, q, segm);
    return elecDoc->ShortestDistanceFromSegment(p, q, segm);
}

double ScalarDocView::ShortestDistanceFromInputArc(CComplex p, int arcIdx) const
{
    if (currDoc) return currDoc->ShortestDistanceFromInputArc(p, arcIdx);
    if (heatDoc) return heatDoc->ShortestDistanceFromInputArc(p, arcIdx);
    return elecDoc->ShortestDistanceFromInputArc(p, arcIdx);
}

void ScalarDocView::GetInputCircle(int arcIdx, CComplex &c, double &R) const
{
    if (currDoc) { currDoc->GetInputCircle(arcIdx, c, R); return; }
    if (heatDoc) { heatDoc->GetInputCircle(arcIdx, c, R); return; }
    elecDoc->GetInputCircle(arcIdx, c, R);
}
