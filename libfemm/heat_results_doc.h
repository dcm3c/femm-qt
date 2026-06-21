// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_results_doc.h — Heat flow postprocessor solution document
// Reads .anh solution files produced by hsolv, provides mesh + field data.
// Ported from femm42src/femm/hviewDoc.cpp and hv_problem.h.
#ifndef HEAT_RESULTS_DOC_H
#define HEAT_RESULTS_DOC_H

#include <string>
#include <vector>
#include "heat_types.h"
#include "elec_results_doc.h"  // ERMeshNode, ERElement, ERBlockLabel, EPointVals
#include "results_doc_base.h"

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

// ---------------------------------------------------------------
// Mesh types — shared with electrostatics (identical layout)
// The scalar potential field is named V in both (temperature for heat).
// ---------------------------------------------------------------

using HRMeshNode  = ERMeshNode;
using HRElement   = ERElement;
using HRBlockLabel = ERBlockLabel;

// ---------------------------------------------------------------
// Material properties for results (with T-K nonlinearity)
// ex/ey aliases provided so ScalarResultsWidget can access uniformly.
// ---------------------------------------------------------------

struct HRMaterialProp {
    std::string BlockName;
    double Kx = 1, Ky = 1;  // thermal conductivity (W/m-K)
    double Kt = 0;           // volumetric heat capacity
    double qv = 0;           // volume heat generation
    int npts = 0;            // T-K curve points
    CComplex Kn[128];        // Kn[i].re = temperature, Kn[i].im = conductivity

    // Rendering-compatible aliases (ScalarResultsWidget accesses .ex/.ey)
    // Set equal to Kx/Ky during PostProcessing.
    double ex = 1, ey = 1;

    // Interpolate thermal conductivity at temperature T
    CComplex GetK(double T) const;
};

// ---------------------------------------------------------------
// Boundary properties for results (just the name)
// ---------------------------------------------------------------

struct HRBoundaryProp {
    std::string BdryName;
};

// ---------------------------------------------------------------
// Point properties for results
// ---------------------------------------------------------------

struct HRPointProp {
    std::string PointName;
    double T = 0;        // prescribed temperature
    double qp = 0;       // point heat generation
};

// ---------------------------------------------------------------
// Circuit (conductor) properties for results
// ---------------------------------------------------------------

struct HRCircuit {
    std::string CircName;
    double V = 0;        // temperature
    double q = 0;        // heat flow
    int CircType = 0;
};

// ---------------------------------------------------------------
// Point values at an arbitrary location (heat flow specific)
// ---------------------------------------------------------------

struct HPointVals {
    double T = 0;        // temperature
    CComplex F;          // heat flux density (W/m^2)
    CComplex K;          // thermal conductivity (Kx + I*Ky)
    CComplex G;          // temperature gradient (K/m)
};

// ---------------------------------------------------------------
// HeatResultsDoc — heat flow postprocessor document
// ---------------------------------------------------------------

class HeatResultsDoc : public ResultsDocBase {
public:
    HeatResultsDoc() = default;
    ~HeatResultsDoc() override;

    // Load a .anh solution file
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

    // Mesh (uses same types as electrostatics — V stores temperature)
    std::vector<HRMeshNode> meshnode;
    std::vector<HRElement> meshelem;

    // Properties
    std::vector<HRMaterialProp> blockproplist;
    std::vector<HRCircuit> circproplist;
    std::vector<HRBlockLabel> labellist;

    // Point and boundary properties (for MakeMask)
    std::vector<HRPointProp> nodeproplist;
    std::vector<HRBoundaryProp> lineproplist;

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

    // Point values at arbitrary location (heat-specific)
    bool GetPointValues(double x, double y, HPointVals &u);
    bool GetPointValues(double x, double y, int k, HPointVals &u);
    void GetPointD(double x, double y, CComplex &D, HRElement &elm);

    // EPointVals-compatible point values (for ScalarResultsWidget rendering)
    bool GetPointValues(double x, double y, EPointVals &u);
    bool GetPointValues(double x, double y, int k, EPointVals &u);

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

private:
    void PostProcessing();
    void BuildConnectivity();
    void FindBoundaryEdges();
    bool MakeMask();
    bool IsKosher(int k) const;

    int d_LineIntegralPoints = 400;
};

#endif // HEAT_RESULTS_DOC_H
