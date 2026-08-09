// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// femmeDoc.h : interface of the CFemmeDoc class
//
/////////////////////////////////////////////////////////////////////////////
#define muo 1.2566370614359173e-6
#define Golden 0.3819660112501051517954131656

class CFemmeDocCore
{

	

// Attributes
public:

	CFemmeDocCore();
	~CFemmeDocCore();

	// General problem attributes
	double  Frequency;
	double  Precision;
	double  Relax;
	int		LengthUnits;
	int		ACSolver;
	int    ProblemType;
	int	Coords;
	CString	PrevSoln;
	int		PrevType;
	double	Depth;

	// Backward-Euler time step, seconds; 0 means "not a transient analysis".
	// Supplied by the driver in the .tmp sidecar and echoed into the .ans so
	// the postprocessor can reconstruct J = sigma*(v - (A-Aprev)/DeltaT).
	// Deliberately independent of PrevType, which continues to mean
	// incremental/frozen permeability -- prob1big does bIncremental=PrevType,
	// so overloading it would switch on that whole path by accident.
	double	DeltaT;

	// Vector containing previous solution for incremental permeability analysis
	double *Aprev;

	// axisymmetric external region parameters
	double  extRo,extRi,extZo;

	CFknDlg *TheView;
	
	// CArrays containing the mesh information
	int	BandWidth;
	CNode *meshnode;
	CElement	*meshele;

	int NumNodes;
	int NumEls;
	
	// lists of properties
	int NumBlockProps;
	int NumPBCs;
	int NumLineProps;
	int NumPointProps;
	int NumCircProps;
	int NumBlockLabels;
	int NumCircPropsOrig;
	int NumAGEs;
	int NumFluxConstraints;
	int NumCurrentConstraints;
	// Extra matrix rows carrying the axial voltage gradient of solid
	// conductors in a transient analysis (the static analogue of prob2big's
	// "Case 2"). One per circuit, allocated after the series expansion.
	int NumVoltageUnknowns;
	int NumNodesPlus;
	void SetNumNodesPlus();
	// Conductivity participating in transient eddy currents (MS/m); see the
	// definition in prob1big.cpp for why it must be the only such test.
	double EddyCduct(int blk, int lbl);

	// Build (or rebuild) an air gap element's ring mapping -- the sorted
	// quad-point pairing node[], plus InnerShift/OuterShift -- from its raw
	// ring node lists and the angles currently in the age struct. The
	// coordinate arrays parallel ringInner/ringOuter; cx,cy must be in the
	// same units as the coordinates (only differences enter). Shared by the
	// fresh-mesh path (load_tangle_mesh) and the transient mesh-reuse path
	// (LoadPrev), which is what keeps the two pairings identical by
	// construction.
	void BuildAgeRing(CAirGapElement &age,
	                  const double *ix, const double *iy,
	                  const double *ox, const double *oy,
	                  double cx, double cy);

	CMaterialProp	*blockproplist;
	CBoundaryProp	*lineproplist;
	CPointProp		*nodeproplist;
	CCircuit		*circproplist;
	CBlockLabel		*labellist;
	CCommonPoint	*pbclist;
	CAirGapElement  *agelist;
	int				*fluxproplist;
	double			**CircuitMatrix;
	double			**ConstraintMatrix;
	double			*currentlist;

	// stuff usually kept track of by CDocument
	char *PathName;

	int bDump;		// set to dump stiffness matrix in a matlab-readable formatdir 	

// Operations
public:

	int LoadPrev();
	// Ap, when supplied, additionally receives the raw nodal A in Wb/m --
	// the unit LoadPrev() stores and WriteStatic2D() emits. V gets A/c as
	// before. Used by transient analyses, where the previous solution is
	// data (the backward-Euler A_prev), not merely a starting guess.
	int LoadPrevA(CString myFile, double *V, double *Ap=NULL);
	int LoadMesh();
	int OnOpenDocument();
	int Cuthill();
	int SortElements();
	int Static2D(CBigLinProb &L);
	int WriteStatic2D(CBigLinProb &L);
	int Harmonic2D(CBigComplexLinProb &L);
	int WriteHarmonic2D(CBigComplexLinProb &L);
	int StaticAxisymmetric(CBigLinProb &L);
	int HarmonicAxisymmetric(CBigComplexLinProb &L);
	void GetPrevAxiB(int k, double &B1p, double &B2p);
	void GetPrev2DB(int k, double &B1p, double &B2p);
	void GetFillFactor(int lbl);
	double ElmArea(int i);
	void CleanUp();
	int MatlabDumpStatic2D(CBigLinProb &L);
	char* StripKey(char *c);
	char* ParseString(char *t, CString *s);
	char* ParseInt(char *t, int *f);
	char* ParseDbl(char *t, double *f);

};

/////////////////////////////////////////////////////////////////////////////

double GetNewMu(double mu,int BHpoints, CComplex *BHdata,double muc,double B);
double Power(double x, int n);
