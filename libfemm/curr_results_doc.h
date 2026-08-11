// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_results_doc.h — Current flow postprocessor solution document
// Reads .anc files produced by csolv, provides mesh + field data.
// Ported from femm42src/femm/CVIEWDOC.H and cv_problem.h.
#ifndef CURR_RESULTS_DOC_H
#define CURR_RESULTS_DOC_H

#include <string>
#include <vector>
#include "curr_types.h"
#include "elec_results_doc.h"  // ERMeshNode, ERElement, ERBlockLabel, EPointVals
#include "results_doc_base.h"
#include "mag_types.h"         // FNode, FSegment, FArcSegment, FBlockLabel, CComplex

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

#ifndef eo
#define eo 8.8541878188e-12
#endif

// ---------------------------------------------------------------
// Complex mesh node — stores complex voltage from csolv
// ---------------------------------------------------------------

struct CRMeshNode {
    double x = 0, y = 0;
    CComplex V;          // complex voltage
    double msk = 0;      // mask value for stress tensor
    int Q = 0;           // boundary marker flag (-2 = interior)

    CComplex CC() const { return CComplex(x, y); }
};

// ---------------------------------------------------------------
// Complex element — stores complex current density
// ---------------------------------------------------------------

struct CRElement {
    int p[3] = {};       // vertex indices
    int blk = 0;         // block type index
    int lbl = 0;         // label index
    CComplex Jx, Jy;    // elemental current density (complex for AC)
    CComplex jx[3], jy[3]; // smoothed nodal current density (per vertex)
    CComplex ctr;        // centroid
    double rsqr = 0;     // max squared distance from centroid to vertex
    int n[3] = {};       // neighbor element indices
};

// ---------------------------------------------------------------
// Material properties for results
// ---------------------------------------------------------------

struct CRMaterialProp {
    std::string BlockName;
    CComplex Kx, Ky;       // effective conductivity = ox + j*2*pi*f*eo*ex
    double ox = 0, oy = 0; // DC conductivity (S/m)
    CComplex ex, ey;        // complex permittivity (with loss tangent)
};

// ---------------------------------------------------------------
// Circuit (conductor) properties for results
// ---------------------------------------------------------------

struct CRCircuit {
    std::string CircName;
    CComplex V;       // conductor voltage
    CComplex q;       // conductor current/charge
    int CircType = 0;
};

// ---------------------------------------------------------------
// Point values at an arbitrary location (all complex for AC)
// ---------------------------------------------------------------

struct CPointVals {
    CComplex V;                // voltage
    CComplex Jx, Jy;          // total AC current density
    CComplex Kx, Ky;          // effective conductivity
    CComplex Ex, Ey;           // electric field intensity
    CComplex ex, ey;           // permittivity
    CComplex Jcx, Jcy;        // conduction current density
    CComplex Jdx, Jdy;        // displacement current density
    double ox = 0, oy = 0;    // DC conductivity
};

// ---------------------------------------------------------------
// CurrResultsDoc — current flow postprocessor document
// ---------------------------------------------------------------

class CurrResultsDoc : public ResultsDocBase {
public:
    CurrResultsDoc() = default;
    ~CurrResultsDoc() override;

    // Load a .anc solution file
    bool OpenDocument(const std::string &pathname);

    // Problem parameters
    double Depth = 1;
    double Frequency = 0;
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

    // Complex mesh (the true solution)
    std::vector<CRMeshNode> cmeshnode;
    std::vector<CRElement> cmeshelem;

    // Display-projected mesh for ScalarResultsWidget compatibility
    // meshnode[i].V = projected real voltage, meshelem[i] has topology
    std::vector<ERMeshNode> meshnode;
    std::vector<ERElement> meshelem;

    // Properties
    std::vector<CRMaterialProp> blockproplist;
    std::vector<CRCircuit> circproplist;
    std::vector<ERBlockLabel> labellist;

    // Point properties (for MakeMask)
    std::vector<CurrPointProp> nodeproplist;
    // Boundary properties (needed for header parsing)
    std::vector<CurrBoundaryProp> lineproplist_props;

    // Node-to-element connectivity
    std::vector<int> NumList;
    std::vector<std::vector<int>> ConList;

    // Solution bounds
    double A_Low = 0, A_High = 0;   // user-adjustable contour range
    double A_lb = 0, A_ub = 0;      // natural bounds from solution
    // PlotBounds[i][0]=low, [i][1]=high for 9 bounded plot types
    // 0=|V|, 1=Re(V), 2=Im(V), 3=|J|, 4=Re(J), 5=Im(J), 6=|E|, 7=Re(E), 8=Im(E)
    double PlotBounds[9][2] = {};
    double d_PlotBounds[9][2] = {};

    // Display mode for voltage projection
    enum VoltageDisplay { VM_Magnitude = 0, VM_Real = 1, VM_Imaginary = 2 };
    VoltageDisplay voltageDisplay = VM_Real;
    void UpdateDisplayProjection();

    // Find which element contains point (x,y). Returns -1 if not found.
    int InTriangle(double x, double y);
    bool InTriangleTest(double x, double y, int i) const;

    // Field computation
    void GetElementJ(int k);
    void GetNodalJ(CComplex *jx, CComplex *jy, int N);
    CComplex Ctr(int e) const;

    // Point values at arbitrary location
    bool GetPointValues(double x, double y, CPointVals &u);
    bool GetPointValues(double x, double y, int k, CPointVals &u);
    void GetPointJ(double x, double y, CComplex &Jxout, CComplex &Jyout, CRElement &elm);

    // EPointVals overload for ScalarResultsWidget compatibility
    bool GetPointValues(double x, double y, EPointVals &u);
    bool GetPointValues(double x, double y, int k, EPointVals &u);

    // Element area
    double ElmArea(int i) const;

    // Integration — returns complex for current flow
    CComplex BlockIntegral(int type);
    // True if BlockIntegral(type) is a genuinely complex (phasor) quantity in an
    // AC solve, so the binding returns a Python complex; false for real types
    // (power/energy/area/volume/steady-state force & torque). Keyed on the type,
    // not the value. Kept next to BlockIntegral so the two stay in sync.
    bool BlockIntegralIsComplex(int type) const;
    void LineIntegral(int type, CComplex *z);

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

    // Line plot data — vals has 3 entries for AC (|mag|, Re, Im), 1 for DC
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

#endif // CURR_RESULTS_DOC_H
