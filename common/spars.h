// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#ifndef SPARS_H
#define SPARS_H

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "complex.h"

// Sparse matrix entry: column index + value
struct SEntry {
	int c;       // column index
	double x;    // value
};

// Complex sparse matrix entry
struct CSEntry {
	int c;       // column index
	CComplex x;  // value
};

#define LAMBDA 1.5

class CBigLinProb
{
	public:

		double *V;				// solution
		double *P;				// search direction
		double *R;				// residual
		double *U;				// A * P
		double *DI;				// Preconditioner diagonal
		double *Z;
		double *b;				// RHS of linear equation
		int   *Q;				// per-node flags (conductor tracking for electrostatics)

		// Upper-triangle storage (used during assembly)
		std::vector<std::vector<SEntry>> M;

		// Full storage (both triangles, built by Finalize() for parallel SpMV)
		std::vector<std::vector<SEntry>> Mfull;

		int n;
		int bdw;
		double Precision;
		bool finalized;

		// Block Jacobi partitioning for parallel SSOR
		int numBlocks;
		std::vector<int> blockStart;  // blockStart[b] = first row of block b

		CBigLinProb();
		~CBigLinProb();
		int Create(int d, int bw);
		void Put(double v, int p, int q);
		double Get(int p, int q);
		void AddTo(double v, int p, int q);
		void Finalize();
		int PCGSolve(int flag);
		int QMRSolve(int flag);
		void MultPC(double *X, double *Y);
		void MultA(double *X, double *Y);
		void SetValue(int i, double x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		double Dot(double *X, double *Y);
		void ComputeBandwidth();
		int SaveMe(CString myFile);
		CFknDlg *TheView;
		void BuildDI();

	private:
		SEntry* FindEntry(int p, int q);
};


/////////////////////////////////////////////////////////////////////
// Complex sparse matrix

class CBigComplexLinProb
{
	public:

		CComplex *P;
		CComplex *U;
		CComplex *R;
		CComplex *V;
		CComplex *Z;
		CComplex *b;
		CComplex *uu;
		CComplex *vv;
		int     *Q;				// per-node flags (conductor tracking)

		// Upper-triangle storage (assembly)
		std::vector<std::vector<CSEntry>> M;
		std::vector<std::vector<CSEntry>> Mh;
		std::vector<std::vector<CSEntry>> Ma;
		std::vector<std::vector<CSEntry>> Ms;

		// Full storage for M (parallel SpMV)
		std::vector<std::vector<CSEntry>> Mfull;

		int n;
		int bdw;
		int bNewton;
		int NumNodes;
		double Precision;
		bool finalized;

		// Block Jacobi partitioning
		int numBlocks;
		std::vector<int> blockStart;

		CBigComplexLinProb();
		~CBigComplexLinProb();
		int Create(int d, int bw, int nodes);
		void Put(CComplex v, int p, int q, int k=0);
		CComplex Get(int p, int q, int k=0);
		void AddTo(CComplex v, int p, int q);
		void Finalize();
		void MultA(CComplex *X, CComplex *Y, int k=0);
		void MultConjA(CComplex *X, CComplex *Y, int k=0);
		CComplex Dot(CComplex *x, CComplex *y);
		CComplex ConjDot(CComplex *x, CComplex *y);
		void SetValue(int i, CComplex x);
		void Periodicity(int i, int j);
		void AntiPeriodicity(int i, int j);
		void Wipe();
		void MultPC(CComplex *X, CComplex *Y);
		void MultAPPA(CComplex *X, CComplex *Y);

		int PBCGSolveMod(int flag);
		int PCGSQStart();
		int PBCGSolve(int flag);
		int NewtonSolve(int flag);

		CFknDlg *TheView;

	private:
		std::vector<std::vector<CSEntry>>& GetMatrix(int k);
};

#endif // SPARS_H
