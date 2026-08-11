// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_results_doc.h — Electrostatics postprocessor solution document
// Reads .res files produced by belasolv, provides mesh + field data.
// Ported from femm42src/femm/belaviewDoc.cpp and bv_problem.h.
#ifndef ELEC_RESULTS_DOC_H
#define ELEC_RESULTS_DOC_H

#include <string>
#include <vector>
#include <functional>
#include "elec_types.h"
#include "results_doc_base.h"
#include "mag_types.h"    // FNode, FSegment, FArcSegment, FBlockLabel, CComplex

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

#define eo 8.8541878188e-12

// ---------------------------------------------------------------
// Mesh node — stores voltage from the solver
// ---------------------------------------------------------------

struct ERMeshNode {
    double x = 0, y = 0;
    double V = 0;        // voltage (electrostatic potential)
    double msk = 0;      // mask value for stress tensor
    int Q = 0;           // boundary marker flag (-2 = interior)
    bool IsSelected = false;

    CComplex CC() const { return CComplex(x, y); }
};

// ---------------------------------------------------------------
// Mesh element — stores D (electric displacement)
// ---------------------------------------------------------------

struct ERElement {
    int p[3] = {};       // vertex indices
    int blk = 0;         // block type index (into blockproplist)
    int lbl = 0;         // label index
    CComplex D;          // elemental displacement (constant per element)
    CComplex d[3];       // smoothed nodal displacement (per vertex)
    CComplex ctr;        // centroid
    double rsqr = 0;     // max squared distance from centroid to vertex
    int n[3] = {};       // neighbor element indices (1 = boundary edge)
};

// ---------------------------------------------------------------
// Material properties for results
// ---------------------------------------------------------------

struct ERMaterialProp {
    std::string BlockName;
    double ex = 1, ey = 1;  // relative permittivity
    double qv = 0;           // volume charge density
};

// ---------------------------------------------------------------
// Circuit (conductor) properties for results
// ---------------------------------------------------------------

struct ERCircuit {
    std::string CircName;
    double V = 0;        // voltage
    double q = 0;        // charge
    int CircType = 0;
};

// ---------------------------------------------------------------
// Block label for results
// ---------------------------------------------------------------

struct ERBlockLabel {
    double x = 0, y = 0;
    double MaxArea = 0;
    int BlockType = -1;
    int InGroup = 0;
    bool IsExternal = false;
    bool IsDefault = false;
    bool IsSelected = false;

    void ToggleSelect() { IsSelected = !IsSelected; }
};

// ---------------------------------------------------------------
// Point values at an arbitrary location
// ---------------------------------------------------------------

struct EPointVals {
    double V = 0;        // voltage
    CComplex D;          // displacement
    CComplex e;          // permittivity (ex + I*ey)
    CComplex E;          // electric field intensity
    double nrg = 0;      // energy density
};

// ---------------------------------------------------------------
// ElecResultsDoc — electrostatics postprocessor document
// ---------------------------------------------------------------

class ElecResultsDoc : public ResultsDocBase {
public:
    ElecResultsDoc() = default;
    ~ElecResultsDoc() override;

    // Load a .res solution file
    bool OpenDocument(const std::string &pathname);

    // Problem parameters
    double Depth = 1;
    int ProblemType = 0;     // 0=planar, 1=axisymmetric
    int LengthUnits = 0;
    int Coords = 0;
    // filePath inherited from ResultsDocBase

    // External region parameters (axisymmetric)
    double extRo = 0, extRi = 0, extZo = 0;

    // Length conversion factor to meters
    double LengthConv() const;

    // Input geometry (from the header)
    std::vector<FNode> nodelist;
    std::vector<FSegment> linelist;
    std::vector<FArcSegment> arclist;

    // Geometry helpers
    void GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const;

    // Mesh
    std::vector<ERMeshNode> meshnode;
    std::vector<ERElement> meshelem;

    // Properties
    std::vector<ERMaterialProp> blockproplist;
    std::vector<ERCircuit> circproplist;
    std::vector<ERBlockLabel> labellist;

    // Point properties (for MakeMask)
    std::vector<EPointProp> nodeproplist;

    // Node-to-element connectivity
    std::vector<int> NumList;
    std::vector<std::vector<int>> ConList;

    // Solution bounds
    double A_Low = 0, A_High = 0;   // user-adjustable contour range
    double A_lb = 0, A_ub = 0;      // natural bounds from solution
    // PlotBounds[i][0]=low, [i][1]=high for each plot type
    double PlotBounds[4][2] = {};
    double d_PlotBounds[4][2] = {};

    // Find which element contains point (x,y). Returns -1 if not found.
    int InTriangle(double x, double y);
    bool InTriangleTest(double x, double y, int i) const;

    // Field computation
    void GetElementD(int k);
    void GetNodalD(CComplex *d, int N);
    CComplex Ctr(int e) const;

    // Point values at arbitrary location
    bool GetPointValues(double x, double y, EPointVals &u);
    bool GetPointValues(double x, double y, int k, EPointVals &u);
    void GetPointD(double x, double y, CComplex &D, ERElement &elm);

    // Element area
    double ElmArea(int i) const;

    // Integration
    CComplex BlockIntegral(int type);
    void LineIntegral(int type, double *z);

    // Contour path for line integrals
    std::vector<CComplex> contour;
    void BendContour(double angle, double anglestep);

    // Input geometry queries (for contour mode)
    int ClosestInputNode(double x, double y) const;
    double ShortestDistanceFromSegment(double p, double q, int segm) const;
    double ShortestDistanceFromInputArc(CComplex p, int arcIdx) const;
    void GetInputCircle(int arcIdx, CComplex &c, double &R) const;

    // Display state
    bool Smooth = true;
    // Sampling density for the numerically evaluated line integrals.
    // Public alongside Smooth: the GUI sets it from the user's preference.
    int d_LineIntegralPoints = 400;
    bool bHasMask = false;

    // Line plot data
    struct XYPlotPoint { double x; std::vector<double> vals; };
    void GetLineValues(std::vector<XYPlotPoint> &pts, int plotType, int numPoints);

    // Weighted stress tensor / Henrotte force
    CComplex HenrotteVector(int k) const;
    double AECF(int k) const;
    double AECF(int k, CComplex p) const;

    // Material comparison
    bool IsSameMaterial(int e1, int e2) const;

    // Optional progress callback for long operations (e.g. MakeMask solve).
    // Receives percentage 0-100.
    std::function<void(int)> progressCallback;

    // Set true by MakeMask (cleared on each BlockIntegral) when the weighted
    // stress tensor selection is invalid because it abuts a non-air region.
    // Callers surface it: the script binding raises, the GUI shows a message box.
    bool maskSelectionInvalid = false;

    bool MakeMask();

private:
    void PostProcessing();
    void BuildConnectivity();
    void FindBoundaryEdges();
    bool IsKosher(int k) const;

};

#endif // ELEC_RESULTS_DOC_H
