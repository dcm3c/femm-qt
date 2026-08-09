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

class CBigLinProb
{
	public:

		double *V;				// solution
		double *P;				// search direction
		double *R;				// residual
		double *U;				// A * P
		double *Z;
		double *b;				// RHS of linear equation
		int   *Q;				// per-node flags (conductor tracking for electrostatics)

		// Upper-triangle storage (used during assembly). This stays an
		// array of vectors so late structural edits -- circuit equations,
		// periodicity merges -- remain cheap; the flat solver-side forms
		// below are derived snapshots rebuilt lazily by Finalize().
		std::vector<std::vector<SEntry>> M;

		// Full storage (both triangles) as flat CSR, built by Finalize()
		// for SpMV: row i's entries are mfPtr[i]..mfPtr[i+1]-1, columns
		// ascending.
		std::vector<int> mfPtr, mfCol;
		std::vector<double> mfVal;

		int n;
		int bdw;
		double Precision;
		bool finalized;

		// Column adjacency (see the CBigComplexLinProb copy for the full
		// story): rows k<i holding an entry (k,i). Replaces the O(n) column
		// sweeps in SetValue and (Anti)Periodicity, whose bdw bound the
		// tangle-ordered solvers never set.
		std::vector<std::vector<int>> colAdj;
		bool colAdjValid = false;
		void BuildColAdj();
		void CoupledRows(int i, int j, std::vector<int> &out);

		// Linear-baseline snapshot for split assembly (see the complex twin).
		std::vector<double> baseM, baseB;
		void SaveBaseline();
		int RestoreBaseline();

		// Incomplete LDL^T preconditioner with threshold-based fill (Crout
		// ILDL over the whole matrix -- see FactorPC in spars.cpp for the
		// measurements behind that choice). Root-free LDL^T rather than
		// LL^T so one kernel serves both the SPD case and the symmetric-
		// indefinite circuit-constraint case that QMRSolve exists for -- D
		// simply carries the sign, and no square roots are taken. Factored
		// once per assembly and reused across every inner solve until a
		// value mutation resets icState; the per-pivot rescue in FactorPC
		// keeps the factor finite unconditionally, so this is the ONLY
		// preconditioner. icPtr/icCol/icVal store L by COLUMN k with row
		// indices r>k ascending; L's unit diagonal is implicit.
		std::vector<int> icPtr, icCol;
		std::vector<double> icVal, icD;
		// 0 = stale, 1 = factored, 2 = build in progress. Readers test
		// for 1 rather than against 0, so a factorization that throws
		// part-way leaves the factor stale and is rebuilt, rather than
		// being consumed half-built.
		int icState = 0;
		void FactorPC();
		void MultIC(double *X, double *Y);

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

		// Upper-triangle storage (assembly); stays an array of vectors so
		// late structural edits remain cheap (see the real twin).
		std::vector<std::vector<CSEntry>> M;
		std::vector<std::vector<CSEntry>> Mh;
		std::vector<std::vector<CSEntry>> Ma;
		std::vector<std::vector<CSEntry>> Ms;

		// Full storage for M as flat CSR (SpMV; see the real twin)
		std::vector<int> mfPtr, mfCol;
		std::vector<CComplex> mfVal;

		int n;
		int bdw;
		int bNewton;
		int NumNodes;
		double Precision;
		bool finalized;

		// Linear-baseline snapshot for split assembly (see the nonlinear
		// harmonic solvers): SaveBaseline copies M's values and b after the
		// V-independent contributions are assembled; RestoreBaseline puts them
		// back and zeroes the N-R sub-matrices, replacing a Wipe + full
		// reassembly. Restore returns 0 if the structure changed since the
		// snapshot (callers then fall back to a full reassembly).
		std::vector<CComplex> baseM, baseB;
		void SaveBaseline();
		int RestoreBaseline();

		// Column adjacency: colAdj[i] lists the rows k<i that hold an entry
		// (k,i) in M's upper-triangle storage. Lets SetValue touch exactly the
		// rows coupled to a fixed node instead of sweeping all n rows per call
		// (the bdw member that once bounded that sweep is never set by the
		// tangle-ordered solvers, so the sweep had degraded to O(n)). Built
		// lazily; invalidated whenever the structure gains an entry.
		std::vector<std::vector<int>> colAdj;
		bool colAdjValid = false;
		void BuildColAdj();
		void CoupledRows(int i, int j, std::vector<int> &out);

		// Incomplete LDL^T preconditioner for M with threshold-based fill
		// (see the real twin in CBigLinProb for the storage layout and
		// state machine). The harmonic matrix is complex-symmetric, NOT
		// Hermitian, so the factorization is the unconjugated M = L D L^T
		// with complex D -- root-free, so no complex square roots with
		// arbitrary phase are ever taken; a near-zero complex pivot is the
		// only breakdown mode and is caught by the same magnitude guard as
		// the real case.
		std::vector<int> icPtr, icCol;
		std::vector<CComplex> icVal, icD;
		// 0 = stale, 1 = factored, 2 = build in progress. Readers test
		// for 1 rather than against 0, so a factorization that throws
		// part-way leaves the factor stale and is rebuilt, rather than
		// being consumed half-built.
		int icState = 0;
		void FactorPC();
		void MultIC(CComplex *X, CComplex *Y);

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
