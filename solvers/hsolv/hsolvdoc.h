// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

#define muo 1.2566370614359173e-6
#define Golden 0.3819660112501051517954131656

class Chsolvdoc
{

	

// Attributes
public:

	Chsolvdoc();
	~Chsolvdoc();

	// General problem attributes
	double  Precision;
	double  Depth;
	int		LengthUnits;
	int    ProblemType;
	int	Coords;
	double	dT;
	CString	PrevSoln;

	// Axisymmetric external region parameters
	double extRo,extRi,extZo;
	
	CFknDlg *TheView;
	
	// CArrays containing the mesh information
	int	BandWidth;
	CNode *meshnode;
	CElement	*meshele;

	int NumNodes;
	int NumEls;
	
	// Vector containing previous solution for time-transient analysis
	double *Tprev;

	// lists of properties
	int NumBlockProps;
	int NumPBCs;
	int NumLineProps;
	int NumPointProps;
	int NumCircProps;
	int NumBlockLabels;

	CMaterialProp	*blockproplist;
	CBoundaryProp	*lineproplist;
	CPointProp		*nodeproplist;
	CCircuit		*circproplist;
	CBlockLabel		*labellist;
	CCommonPoint	*pbclist;

	// stuff usually kept track of by CDocument
	char *PathName;
	

// Operations
public:

	int LoadMesh();
	int LoadPrev();
	int OnOpenDocument();
	int Cuthill();
	int SortElements();
	int AnalyzeProblem(CBigLinProb &L);
	double ChargeOnConductor(int OnConductor, CBigLinProb &L);
	int WriteResults(CBigLinProb &L);
	void CleanUp();

};

/////////////////////////////////////////////////////////////////////////////

double Power(double x, int n);
