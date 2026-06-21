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
	double  Precision;
	double  Depth;
	int		LengthUnits;
	int    ProblemType;
	int	Coords;

	// Axisymmetric external region parameters
	double extRo,extRi,extZo;
	
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
