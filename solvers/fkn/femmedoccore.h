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
	int NumNodesPlus;

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
	int LoadPrevA(CString myFile, double *V);
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
