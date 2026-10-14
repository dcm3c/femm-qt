// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_doc.h — Magnetics postprocessor solution document
// Reads .ans files produced by fkn solver, provides mesh + field data.
// Ports the full post-load processing from FemmviewDoc.cpp.
#ifndef RESULTS_DOC_H
#define RESULTS_DOC_H

#include <string>
#include <vector>
#include <functional>
#include "mag_types.h"
#include "results_doc_base.h"

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

#define muo (PI * 4.e-7)

struct RMeshNode {
    double x = 0, y = 0;
    CComplex A;          // vector potential (complex for AC)
    double msk = 0;      // mask value for stress tensor
    double Aprev = 0;    // previous solution (incremental)
    bool IsSelected = false;
};

struct RElement {
    int p[3] = {};       // vertex indices
    int blk = 0;         // block type index (into blockproplist)
    int lbl = 0;         // label index
    CComplex B1, B2;     // elemental flux density (constant per element)
    CComplex b1[3], b2[3]; // nodal flux density (smoothed, per vertex)
    CComplex h1[3], h2[3]; // nodal field intensity (from GetH on nodal B)
    CComplex J[3];         // nodal current density (from GetJA)
    double magdir = 0;   // magnetization direction
    CComplex ctr;        // centroid
    double rsqr = 0;     // max squared distance from centroid to vertex
    int n[3] = {};       // neighbor element indices
    double B1p = 0, B2p = 0; // previous solution B (incremental)
    double Jp = 0;           // previous solution current density (incremental)
};

struct RMaterialProp {
    std::string BlockName;
    double mu_x = 1, mu_y = 1;
    int BHpoints = 0;
    std::vector<double> Bdata;
    std::vector<CComplex> Hdata;
    std::vector<CComplex> slope;  // spline slopes for BH interpolation
    int LamType = 0;
    double LamFill = 1;
    double H_c = 0;
    CComplex Jsrc;
    double Cduct = 0;
    double Lam_d = 0;
    double Theta_hn = 0, Theta_hx = 0, Theta_hy = 0;
    int NStrands = 0;
    double WireD = 0;
    CComplex mu_fdx, mu_fdy;  // frequency-dependent permeability
    double MuMax = 0;
    double Frequency = 0;

    // B-H curve lookups
    double GetH(double b) const;
    CComplex GetH(CComplex b) const;   // complex cubic-Hermite lookup (OF Problem.cpp)
    void GetMu(double b1, double b2, double &mu1, double &mu2) const;
    void GetMu(CComplex b1, CComplex b2, CComplex &mu1, CComplex &mu2) const;
    // 1-D nonlinear lamination FE solve for the apparent AC B-H (OF Problem.cpp)
    CComplex LaminatedBH(double omega, int i) const;
    void GetSlopes(double omega);

    // Energy density
    double GetB(double h) const;
    CComplex GetdHdB(double B) const;  // complex (OF); real callers take Re()
    double GetEnergy(double b) const;
    double GetCoEnergy(double b) const;
    double DoEnergy(double b1, double b2) const;
    double DoCoEnergy(double b1, double b2) const;
    double DoCoEnergy(CComplex b1, CComplex b2) const;
    double DoEnergy(CComplex b1, CComplex b2) const;
    double Nrg = 0;  // energy offset for PM materials
};

struct RCircuit {
    std::string CircName;
    CComplex Amps;
    int CircType = 0;
};

// Point property — needed for MakeMask and GetNodalB
struct RPointProp {
    std::string PointName;
    double Jr = 0, Ji = 0;    // applied point current (A)
    double Ar = 0, Ai = 0;    // prescribed nodal value
};

struct RBlockLabel {
    double x = 0, y = 0;
    double MaxArea = 0;
    double MagDir = 0;
    std::string MagDirFctn;
    int Turns = 1;
    int InCircuit = -1;
    int BlockType = -1;
    int InGroup = 0;
    bool IsExternal = false;
    bool IsDefault = false;
    bool IsSelected = false;
    int Case = 0;
    CComplex J, dVolts;
    double FillFactor = 0;
    CComplex o, mu;       // o = conductivity, mu = permeability (for prox regions)
    double LocalEnergy = 0;
    void ToggleSelect() { IsSelected = !IsSelected; }
};

struct RQuadPoint {
    int n0 = 0, n1 = 0, n2 = 0, n3 = 0;
    double w0 = 0, w1 = 0, w2 = 0, w3 = 0;
};

struct RAirGapElement {
    std::string BdryName;
    int BdryFormat = 0;      // 0=periodic, 1=antiperiodic
    int totalArcElements = 0;
    double totalArcLength = 0;
    double ri = 0, ro = 0;
    double InnerAngle = 0, OuterAngle = 0;
    double InnerShift = 0, OuterShift = 0;
    CComplex agc;            // center

    std::vector<RQuadPoint> qp;

    int nn = 0;              // number of harmonics
    CComplex aco;            // DC component of A
    std::vector<CComplex> brc, brs, btc, bts;   // harmonic coefficients
    std::vector<CComplex> br, bt;                // element-level B
    std::vector<int> nh;                         // harmonic numbers
    // Previous solution (incremental)
    std::vector<double> brcPrev, brsPrev, btcPrev, btsPrev;
    std::vector<double> brPrev, btPrev;
};

class ResultsDoc : public ResultsDocBase {
public:
    ResultsDoc() = default;
    ~ResultsDoc() override;

    // Load a .ans solution file
    bool OpenDocument(const std::string &pathname);

    // Problem parameters
    double Frequency = 0;
    double Depth = 1;
    int ProblemType = 0;   // 0=planar, 1=axi
    int LengthUnits = 1;
    int Coords = 0;
    // filePath inherited from ResultsDocBase
    std::string PrevSoln;

    // External region parameters (axisymmetric)
    double extRo = 0, extRi = 0, extZo = 0;
    int PrevType = 0;      // 0=none, 1=incremental, 2=frozen
    int bIncremental = 0;  // 0=none, 1=incremental, 2=frozen
    // Backward-Euler time step of a transient solution, seconds; 0 means the
    // solution is not transient. Written into the .ans header by the solver.
    // Independent of bIncremental: both cause Aprev to be stored per node, but
    // for a transient it is the previous TIME STEP rather than a linearization
    // base point, and it is what makes the eddy current dA/dt computable.
    double DeltaT = 0;

    // Length conversion factor to meters
    double LengthConv() const;

    // Input geometry (from the header, same as .fem format)
    std::vector<FNode> nodelist;
    std::vector<FSegment> linelist;
    std::vector<FArcSegment> arclist;
    std::vector<FBlockLabel> blocklist_geom;  // geometry block labels (from header)

    // Geometry helpers (for arc drawing)
    void GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const;

    // Mesh
    std::vector<RMeshNode> meshnode;
    std::vector<RElement> meshelem;

    // Properties
    std::vector<RMaterialProp> blockproplist;
    std::vector<RPointProp> nodeproplist;
    std::vector<RCircuit> circproplist;
    std::vector<RBlockLabel> labellist;

    // Air gap elements
    std::vector<RAirGapElement> agelist;

    // Node-to-element connectivity
    std::vector<int> NumList;
    std::vector<std::vector<int>> ConList;

    // Solution bounds (computed on load)
    double A_low = 0, A_high = 0;    // user-adjustable contour range
    double A_lb = 0, A_ub = 0;      // natural bounds from solution
    double B_low = 0, B_high = 0;
    double H_low = 0, H_high = 0;
    // PlotBounds[i][0]=low, [i][1]=high for each plot type
    double PlotBounds[10][2] = {};
    double d_PlotBounds[10][2] = {};  // default (natural) bounds

    // Compute fill factor, effective conductivity, permeability for stranded regions
    void GetFillFactor(int lbl);

    // Find which element contains point (x,y). Returns -1 if not found.
    // Uses banded search exploiting Cuthill-McKee sorted element ordering.
    int InTriangle(double x, double y);
    bool InTriangleTest(double x, double y, int i) const;

    // Get |B| at element centroid
    double GetElementBmag(int e) const;

    // Field computation
    void GetElementB(RElement &elm);
    void GetNodalB(CComplex *b1, CComplex *b2, RElement &elm);
    CComplex Ctr(int e) const;

    // H from B
    void GetH(double b1, double b2, double &h1, double &h2, int k);
    void GetH(CComplex b1, CComplex b2, CComplex &h1, CComplex &h2, int k);
    void GetMu(double b1, double b2, double &mu1, double &mu2, int k);
    void GetMu(CComplex b1, CComplex b2, CComplex &mu1, CComplex &mu2, int k);

    // Point values at arbitrary location
    struct PointVals {
        CComplex A;
        CComplex B1, B2;
        CComplex mu1, mu2, mu12;
        CComplex H1, H2;
        CComplex Je, Js;
        CComplex Hc;
        double c = 0;
        double E = 0;
        double Ph = 0, Pe = 0;
        double ff = 0;
        bool ffIsLam = false;   // ff holds the lamination fill, not a winding's
    };
    bool GetPointValues(double x, double y, PointVals &u);
    bool GetPointValues(double x, double y, int k, PointVals &u);
    void GetPointB(double x, double y, CComplex &B1, CComplex &B2, RElement &elm);

    // Element area
    double ElmArea(int i) const;

    // Integration
    CComplex BlockIntegral(int type);
    // True if BlockIntegral(type) is a genuinely complex (phasor) quantity in an
    // AC solve, so the binding should return a Python complex; false for
    // semantically-real types (area/volume/energy/losses/steady-state force &
    // torque), which return a real. Keyed on the type, never the runtime value,
    // so the returned Python type is stable per (type, frequency). Kept next to
    // BlockIntegral so the two stay in sync.
    bool BlockIntegralIsComplex(int type) const;
    void LineIntegral(int type, CComplex *z);

    // Circuit results
    CComplex GetVoltageDrop(int circNum);
    CComplex GetFluxLinkage(int circNum);

    // Contour path for line integrals
    std::vector<CComplex> contour;
    void BendContour(double angle, double anglestep);

    // Input geometry queries (for contour mode)
    int ClosestInputNode(double x, double y) const;
    double ShortestDistanceFromSegment(double p, double q, int segm) const;
    double ShortestDistanceFromInputArc(CComplex p, int arcIdx) const;
    void GetInputCircle(int arcIdx, CComplex &c, double &R) const;

    bool Smooth = true;
    bool ShiftH = true;   // shift H in PMs (subtract Hc, report separately)

    // Sampling density for the line integrals that are evaluated numerically
    // (the stress tensor pair and (B.n)^2); the rest are exact and ignore it.
    // A member rather than a constant so the GUI can hand over the user's
    // preference without libfemm having to know what a preference is -- the
    // three scalar results documents have carried the same field all along.
    int d_LineIntegralPoints = 400;
    bool bHasMask = false;
    int WeightingScheme = 0;

    // Integration helpers
    CComplex PlnInt(double a, CComplex *u, CComplex *v) const;
    CComplex AxiInt(double a, CComplex *u, CComplex *v, double *r) const;

    // Circuit helper functions for GetVoltageDrop/GetFluxLinkage
    CComplex GetStrandedVoltageDrop(int lbl);
    CComplex GetStrandedLinkage(int lbl);
    CComplex GetSolidAxisymmetricLinkage(int lbl);
    CComplex GetParallelLinkage(int numcirc);
    CComplex GetParallelLinkageAlt(int numcirc);

    // Line plot data
    struct XYPlotPoint { double x; std::vector<double> vals; };
    void GetLineValues(std::vector<XYPlotPoint> &pts, int plotType, int numPoints);
    void GetGapValues(std::vector<XYPlotPoint> &pts, int plotType, int numPoints, int ageIdx);

    // Weighted stress tensor / Henrotte force
    CComplex HenrotteVector(int k) const;
    double AECF(int k) const;

    // Current density per element
    void GetJA(int k, CComplex *J, CComplex *A);

    // Optional progress callback for long operations (e.g. MakeMask solve).
    // Receives percentage 0-100.
    std::function<void(int)> progressCallback;

    // Set true by MakeMask (cleared on each BlockIntegral) when the weighted
    // stress tensor selection is invalid because it abuts a non-air region.
    // Callers surface it: the script binding raises, the GUI shows a message box.
    bool maskSelectionInvalid = false;

private:
    bool PointInTri(double px, double py, int e) const;
    void PostProcessing();  // all post-load computation
    void BuildConnectivity();
    void FindBoundaryEdges();
    void ComputeMagDir();
    void ComputeElementFields();
    void ComputeNodalFields();
    void ComputeBounds();
    void ComputeFreqDepPermeability();
    void ComputeAGEHarmonics();
    bool MakeMask();
    bool IsKosher(int k) const;

    // Axisymmetric solver-consistent element field model: within an element the
    // flux function v = 2*pi*r*A is linear in (u, z) with u = r^2, so B_z is
    // constant and B_r = k/r. Used for quadrature-exact axisymmetric block
    // integrals (WST forces, energy, coenergy).
    struct AxiField {
        CComplex Bz, k;       // B_z (constant), B_r = k/r
        CComplex Bzp, kp;     // same for the previous (bias) solution
        double I0, I1, Im1;   // ∫dA, ∫r dA, ∫(1/r)dA over the element (meters)
    };
    bool AxiFluxFit(int i, AxiField &f, bool wantPrev) const;
    double AxiDensityIntegral(int i, CComplex k, CComplex Bz,
                              const std::function<double(CComplex, CComplex)> &dens) const;
};

#endif // RESULTS_DOC_H
