// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_doc_view.h — Uniform view of scalar results doc data for rendering
// Holds non-owning pointers to data members common across physics types.
// ScalarResultsWidget uses this to avoid physics-specific coupling.
#ifndef SCALAR_DOC_VIEW_H
#define SCALAR_DOC_VIEW_H

#include <string>
#include <vector>
#include "elec_results_doc.h"

class HeatResultsDoc;
class CurrResultsDoc;

// ---------------------------------------------------------------
// ScalarDocView — non-owning view of a scalar results doc
// All pointers refer to data owned by the underlying doc object.
// ---------------------------------------------------------------

struct ScalarDocView {
    // Mesh data (ERMeshNode/ERElement/ERBlockLabel are used by all scalar physics)
    std::vector<ERMeshNode> *meshnode = nullptr;
    std::vector<ERElement> *meshelem = nullptr;
    std::vector<ERBlockLabel> *labellist = nullptr;

    // Input geometry
    std::vector<FNode> *nodelist = nullptr;
    std::vector<FSegment> *linelist = nullptr;
    std::vector<FArcSegment> *arclist = nullptr;

    // Solution bounds
    double (*PlotBounds)[2] = nullptr;
    double (*d_PlotBounds)[2] = nullptr;
    double *A_Low = nullptr, *A_High = nullptr;
    double *A_lb = nullptr, *A_ub = nullptr;

    // Display state
    bool *Smooth = nullptr;
    bool *bHasMask = nullptr;

    // Problem parameters
    int *ProblemType = nullptr;
    double *Depth = nullptr;
    std::string *filePath = nullptr;

    // Contour
    std::vector<CComplex> *contour = nullptr;

    // Block property access (inline lambdas would be nicer but we keep it simple)
    int physicsType = 0;

    // Accessor for block property ex/ey/BlockName
    // These delegate to the underlying doc's blockproplist.
    double blockPropEx(int i) const;
    double blockPropEy(int i) const;
    std::string blockPropName(int i) const;
    int blockPropCount() const;

    // Method dispatch — calls the appropriate doc's method
    int InTriangle(double x, double y);
    bool GetPointValues(double x, double y, EPointVals &u);
    bool GetPointValues(double x, double y, int k, EPointVals &u);
    void GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const;
    int ClosestInputNode(double x, double y) const;
    double ShortestDistanceFromSegment(double p, double q, int segm) const;
    double ShortestDistanceFromInputArc(CComplex p, int arcIdx) const;
    void GetInputCircle(int arcIdx, CComplex &c, double &R) const;

    // Block mode: select the conductor whose nearest input entity is closest to
    // (x, y), toggling its geometry entities and mesh nodes. Returns true if a
    // conductor was toggled. Used for right-click conductor selection.
    bool SelectConductorNear(double x, double y);

    // Raw doc pointers for method dispatch
    ElecResultsDoc *elecDoc = nullptr;
    HeatResultsDoc *heatDoc = nullptr;
    CurrResultsDoc *currDoc = nullptr;

    // Build from ElecResultsDoc
    static ScalarDocView fromElec(ElecResultsDoc *doc);
    // Build from HeatResultsDoc
    static ScalarDocView fromHeat(HeatResultsDoc *doc);
    // Build from CurrResultsDoc
    static ScalarDocView fromCurrentFlow(CurrResultsDoc *doc);
};

#endif // SCALAR_DOC_VIEW_H
