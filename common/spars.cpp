// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// spars.cpp — Real-valued sparse symmetric matrix
//
// Assembly uses upper-triangle storage; Finalize() expands it to flat-CSR
// full storage for SpMV. Preconditioning is the whole-matrix incomplete
// LDL^T (FactorPC).
//
// Deliberately sequential throughout. OpenMP was removed after the ILDL
// preconditioner landed: measured across 10k-156k node problems, 4 threads
// bought -11% to +4% wall time -- the sequential factor dominates each
// iteration and cut iteration counts ~10x, leaving too little vector work
// to amortize the fork/join -- while sequential reductions make results
// bit-deterministic run to run, which the regression tooling values more.

#include "StdAfx.h"
#include <math.h>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "utf8_io.h"

#define KLUDGE

CBigLinProb::CBigLinProb()
{
	n=0;
	V=P=R=U=Z=b=NULL;
	Q=NULL;
	Precision=1.e-08;
	finalized=false;
}

CBigLinProb::~CBigLinProb()
{
	if (n==0) return;
	free(b); free(P); free(R);
	free(V); free(U); free(Z);
	if(Q) free(Q);
}

int CBigLinProb::Create(int d, int bw)
{
	bdw=bw;
	b=(double *)calloc(d,sizeof(double));
	V=(double *)calloc(d,sizeof(double));
	P=(double *)calloc(d,sizeof(double));
	R=(double *)calloc(d,sizeof(double));
	U=(double *)calloc(d,sizeof(double));
	Z=(double *)calloc(d,sizeof(double));
	Q=(int *)calloc(d,sizeof(int));
	n=d;
	finalized=false;

	M.resize(d);
	for(int i=0;i<d;i++)
		M[i].push_back({i, 0.0});

	return 1;
}

SEntry* CBigLinProb::FindEntry(int p, int q)
{
	auto& row = M[p];
	for(auto& e : row)
		if(e.c == q) return &e;
	return nullptr;
}

void CBigLinProb::Put(double v, int p, int q)
{
	if(q < p){ int t=p; p=q; q=t; }
	icState = 0;
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x = v; return; }
	}
	SEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const SEntry& a, const SEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
	// Maintain the column adjacency incrementally: entry (p,q) means row p
	// now holds column q. Order within colAdj[q] does not matter --
	// CoupledRows sorts its merged output -- so an append keeps it valid.
	// Without this, every periodicity merge that inserts would force a full
	// O(nnz) rebuild for the NEXT pair, once per pair.
	if (colAdjValid && p != q) colAdj[q].push_back(p);
}

double CBigLinProb::Get(int p, int q)
{
	if(q < p){ int t=p; p=q; q=t; }
	auto& row = M[p];
	for(auto& e : row)
		if(e.c == q) return e.x;
	return 0;
}

void CBigLinProb::AddTo(double v, int p, int q)
{
	if(q < p){ int t=p; p=q; q=t; }
	icState = 0;
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x += v; return; }
	}
	SEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const SEntry& a, const SEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
	// Maintain the column adjacency incrementally: entry (p,q) means row p
	// now holds column q. Order within colAdj[q] does not matter --
	// CoupledRows sorts its merged output -- so an append keeps it valid.
	// Without this, every periodicity merge that inserts would force a full
	// O(nnz) rebuild for the NEXT pair, once per pair.
	if (colAdjValid && p != q) colAdj[q].push_back(p);
}

// Expand the upper-triangle assembly storage to flat-CSR full storage.
void CBigLinProb::Finalize()
{
	// --- Build full storage (flat CSR) ---
	// Each row's columns come out ascending without a sort: the first fill
	// pass sweeps upper-triangle rows j in ascending order, placing the
	// mirrored entry (r, j), j < r, in each row r it strikes; the second
	// appends each row's own upper-triangle entries (columns >= r, already
	// ascending). Ascending columns keep the SpMV summation order identical
	// to the sorted array-of-vectors this replaces.
	mfPtr.assign(n+1, 0);
	for(int i=0;i<n;i++)
		for(auto& e : M[i]){
			mfPtr[i+1]++;
			if(e.c != i) mfPtr[e.c+1]++;
		}
	for(int i=0;i<n;i++) mfPtr[i+1]+=mfPtr[i];
	mfCol.resize(mfPtr[n]);
	mfVal.resize(mfPtr[n]);
	{
		std::vector<int> cur(mfPtr.begin(), mfPtr.end()-1);
		for(int j=0;j<n;j++)
			for(auto& e : M[j])
				if(e.c != j){
					mfCol[cur[e.c]]=j;
					mfVal[cur[e.c]]=e.x;
					cur[e.c]++;
				}
		for(int i=0;i<n;i++)
			for(auto& e : M[i]){
				mfCol[cur[i]]=e.c;
				mfVal[cur[i]]=e.x;
				cur[i]++;
			}
	}

	finalized = true;
}

// SpMV over the full CSR storage.
void CBigLinProb::MultA(double *X, double *Y)
{
	if(!finalized) Finalize();

	for(int i=0;i<n;i++){
		double sum = 0;
		for(int idx=mfPtr[i];idx<mfPtr[i+1];idx++)
			sum += mfVal[idx] * X[mfCol[idx]];
		Y[i] = sum;
	}
}

double CBigLinProb::Dot(double *X, double *Y)
{
	double z=0;
	for(int i=0;i<n;i++) z += X[i]*Y[i];
	return z;
}

// Incomplete LDL^T factorization of M with threshold-based fill (Crout-
// style ILDL), over the WHOLE matrix -- deliberately not block-Jacobi. See
// the complex twin in CBigComplexLinProb::FactorPC for the measurements
// behind both choices: zero-fill IC(0) only matched the SSOR sweep it
// replaced (the classic equivalence), and per-block factors plateaued ~2x
// short of their serial potential because the discarded inter-block
// coupling became the binding constraint. Factored whole, the fill
// collapses the iteration count enough to pay for the triangular solves
// being sequential many times over.
//
// Column k keeps the ICFILL largest entries whose magnitude exceeds ICTOL
// times the original column's largest entry. Setup runs once per assembly
// (any value mutation resets icState) and is amortized over every PCG/QMR
// iteration solved against it.
//
// Root-free LDL^T rather than LL^T so one kernel serves both the SPD case
// and the symmetric-indefinite circuit-constraint case that QMRSolve
// exists for -- D simply carries the sign, and no square roots are taken.
// When M's diagonal is entirely positive (the SPD problems PCGSolve
// serves) a nonpositive pivot is treated as breakdown and rescued with the
// row's diagonal scale, so the factor stays SPD as PCG requires;
// otherwise D is allowed to go negative -- QMR needs a symmetric
// preconditioner, not a definite one, and LDL^T with signed D is exactly
// that. Correctness is never at stake: the per-pivot rescue below keeps
// the factor finite (degrading toward Jacobi as rescues accumulate), and
// the Krylov loops measure the true residual against the true operator,
// so a degraded factor costs iterations, never accuracy. There is no
// fallback preconditioner -- an SSOR one existed until telemetry across
// the whole regression corpus showed not a single pivot ever rescued.
//
// The Crout ordering computes one column of L at a time: column k is A's
// lower column k (read from upper-triangle row k, by symmetry) minus
// d_j * L(k,j) * L(:,j) over the previously kept entries L(k,j) of row k,
// which is exactly the bookkeeping rowsOfL maintains as columns finalize.
// The factor is stored per COLUMN (icPtr/icCol/icVal, row indices
// ascending; L's unit diagonal implicit), which serves both triangular
// sweeps: forward substitution pushes each finished y_k down its column,
// and the transposed backward solve gathers along the same column.
// Provenance: this is an independent implementation, written against this
// codebase's own storage classes. No code is derived from any library. The
// two published algorithms it implements are:
//   Y. Saad, "ILUT: A dual threshold incomplete LU factorization,"
//     Numerical Linear Algebra with Applications 1(4):387-402, 1994
//     -- the dual dropping rule: discard entries below ICTOL times the
//        column's largest, then keep at most ICFILL of the survivors.
//   N. Li, Y. Saad and E. Chow, "Crout Versions of ILU for General Sparse
//     Matrices," SIAM J. Sci. Comput. 25(2):716-728, 2003
//     -- the Crout ordering: build column k from the previously kept
//        entries of row k, which is what rowsOfL maintains.
// Algorithms are not copyrightable and neither is under any license that
// would reach an independent implementation; the citations are here because
// attribution is owed to the authors, not because a license requires it.
void CBigLinProb::FactorPC()
{
	if(!finalized) Finalize();
	icState = 2;

	// Tuning shared with the complex twin (see its FactorPC); the wall-
	// clock optimum is flat within roughly a factor of 3 around these.
	const double ICTOL = 1e-3;
	const int ICFILL = 32;

	icPtr.assign(n+1, 0);
	icCol.clear();
	icVal.clear();
	icCol.reserve((size_t)n*8);
	icVal.reserve((size_t)n*8);
	icD.resize(n);

	bool posDiag = true;
	for(int i=0;i<n;i++) if(!(M[i][0].x>0)){ posDiag=false; break; }

	// rowsOfL[i]: the columns k that kept an entry at row i, k ascending.
	// Only the column index is stored: the value L(i,k) is recoverable as
	// icVal[colStart[k]] at the moment row i is processed, because visits to
	// a column are in 1:1 order with its own entries. Column k's entries are
	// pushed with r ascending, and the outer loop reaches those r in the same
	// ascending order, so the i-th visit to column j lands on its i-th entry.
	// colStart[j] is that cursor: icCol[colStart[j]] == k on the visit at k,
	// everything before it is spent, everything after it has r > k. That
	// removes both the rescan of the dead prefix and the r<=k branch from the
	// innermost loop.
	std::vector<std::vector<int>> rowsOfL(n);
	std::vector<int> colStart(n, 0);
	// sparse column accumulator
	std::vector<double> w(n);
	std::vector<char> inw(n, 0);
	std::vector<int> touch, keep;
	touch.reserve(256); keep.reserve(256);

	for(int k=0;k<n;k++){
		double dk = M[k][0].x;
		double cnorm = fabs(dk);

		// load A's lower column k from upper-triangle row k
		for(auto& e : M[k])
			if(e.c>k){
				w[e.c]=e.x; inw[e.c]=1; touch.push_back(e.c);
				double a=fabs(e.x); if(a>cnorm) cnorm=a;
			}

		// subtract d_j L(k,j) L(:,j) for the kept entries of row k
		for(int j : rowsOfL[k]){
			const int p = colStart[j];        // icCol[p] == k
			const double Lkj = icVal[p];
			const double f = icD[j]*Lkj;
			dk -= f*Lkj;
			for(int idx=p+1;idx<icPtr[j+1];idx++){
				const int r = icCol[idx];
				if(!inw[r]){ w[r]=0; inw[r]=1; touch.push_back(r); }
				w[r] -= f*icVal[idx];
			}
			colStart[j] = p+1;
		}
		// row k is spent; release it rather than hold all n to the end
		std::vector<int>().swap(rowsOfL[k]);

		// pivot guard; negated comparisons so a NaN pivot is also rescued
		double scale = fabs(M[k][0].x);
		if(scale==0){
			// zero diagonal (constraint row): fall back to the row's
			// magnitude
			for(int idx=mfPtr[k];idx<mfPtr[k+1];idx++) scale += fabs(mfVal[idx]);
			if(scale==0) scale=1;
		}
		// Rescue with the ORIGINAL diagonal, not its magnitude: on the
		// indefinite path a negative diagonal must stay negative, or the
		// factor claims the wrong inertia at that pivot. Identical to the
		// old dk=scale whenever posDiag holds, since then the diagonal is
		// already positive; scale still covers the zero-diagonal
		// constraint rows, where it is the row's 1-norm.
		if(posDiag ? !(dk > 1e-8*scale) : !(fabs(dk) > 1e-8*scale))
			dk = (M[k][0].x != 0) ? M[k][0].x : scale;
		icD[k]=dk;

		// drop small entries, cap the survivors at ICFILL largest
		keep.clear();
		const double drop = ICTOL*cnorm;
		for(int r : touch){
			if(fabs(w[r]) >= drop) keep.push_back(r);
			inw[r]=0;
		}
		touch.clear();
		if((int)keep.size() > ICFILL){
			std::nth_element(keep.begin(), keep.begin()+ICFILL, keep.end(),
				[&](int a, int b){ return fabs(w[a]) > fabs(w[b]); });
			keep.resize(ICFILL);
		}
		std::sort(keep.begin(), keep.end());

		for(int r : keep){
			double lrk = w[r]/dk;
			icCol.push_back(r);
			icVal.push_back(lrk);
			rowsOfL[r].push_back(k);
		}
		icPtr[k+1]=(int)icCol.size();
		colStart[k]=icPtr[k];
	}

	icState = 1;
}

// Apply the incomplete LDL^T: with the factor stored by column, forward
// substitution pushes each finished y_k down column k, the D solve is a
// diagonal scale, and the transposed backward solve gathers along the same
// columns in descending order. Sequential by nature -- the strong whole-
// matrix factor makes the iteration count small enough that this wins over
// any parallel-but-decoupled alternative (see FactorPC).
void CBigLinProb::MultIC(double *X, double *Y)
{
	int i;
	for(i=0;i<n;i++) Y[i]=X[i];
	for(int k=0;k<n;k++){
		const double yk = Y[k];
		for(int idx=icPtr[k];idx<icPtr[k+1];idx++)
			Y[icCol[idx]] -= icVal[idx]*yk;
		// column k touches only rows > k, so Y[k] is final here: fold
		// the D solve in rather than making a separate pass over Y
		Y[k] = yk/icD[k];
	}
	for(int k=n-1;k>=0;k--){
		double s = Y[k];
		for(int idx=icPtr[k];idx<icPtr[k+1];idx++)
			s -= icVal[idx]*Y[icCol[idx]];
		Y[k]=s;
	}
}

// Preconditioner: the incomplete LDL^T, factored lazily on first use after
// any value mutation.
void CBigLinProb::MultPC(double *X, double *Y)
{
	if(!finalized) Finalize();
	if(icState!=1) FactorPC();
	MultIC(X,Y);
}

void CBigLinProb::BuildColAdj()
{
	colAdj.assign(n, std::vector<int>());
	for(int k=0;k<n;k++)
		for(auto& e : M[k])
			if (e.c != k) colAdj[e.c].push_back(k);
	colAdjValid = true;
}

// Rows structurally coupled to column i or column j, ascending, excluding i
// and j themselves. Pass j = i for a single column. Collected up front so the
// caller's Puts (which may insert and so invalidate colAdj) iterate a stable
// snapshot of plain indices.
void CBigLinProb::CoupledRows(int i, int j, std::vector<int> &out)
{
	if (!colAdjValid) BuildColAdj();
	out.clear();
	for(int k : colAdj[i]) out.push_back(k);
	for(auto& e : M[i]) out.push_back(e.c);
	if (j != i){
		for(int k : colAdj[j]) out.push_back(k);
		for(auto& e : M[j]) out.push_back(e.c);
	}
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	out.erase(std::remove(out.begin(), out.end(), i), out.end());
	out.erase(std::remove(out.begin(), out.end(), j), out.end());
}

void CBigLinProb::SetValue(int i, double x)
{
	// Historically swept rows k = fst..lst looking for entries in column i,
	// bounded by bdw -- which the tangle-ordered solvers never set, so the
	// sweep had degraded to all n rows per fixed node. Enumerate exactly the
	// coupled rows instead; semantics are unchanged (the z!=0 guard means the
	// zeroing Puts never insert).
	double z;
	std::vector<int> rows;
	CoupledRows(i, i, rows);
	rows.push_back(i);   // the historical sweep visits the diagonal too

	for(int k : rows){
		z=Get(k,i);
		if(z!=0){
			b[k]=b[k]-(z*x);
			if(i!=k) Put(0.,k,i);
		}
	}
	b[i]=Get(i,i)*x;
}

void CBigLinProb::Wipe()
{
	for(int i=0;i<n;i++){
		b[i]=0.;
		for(auto& e : M[i])
			e.x = 0;
	}
	finalized = false;
	icState = 0;
}

void CBigLinProb::SaveBaseline()
{
	int i;
	size_t tot=0;
	for(i=0;i<n;i++) tot+=M[i].size();
	baseM.clear();
	baseM.reserve(tot);
	for(i=0;i<n;i++) for(auto& e : M[i]) baseM.push_back(e.x);
	baseB.assign(b, b+n);
}

int CBigLinProb::RestoreBaseline()
{
	int i;
	size_t tot=0;
	for(i=0;i<n;i++) tot+=M[i].size();
	if (tot!=baseM.size() || baseB.size()!=(size_t)n) return 0;

	size_t idx=0;
	for(i=0;i<n;i++) for(auto& e : M[i]) e.x=baseM[idx++];
	for(i=0;i<n;i++) b[i]=baseB[i];

	finalized = false;
	icState = 0;
	return 1;
}

void CBigLinProb::AntiPeriodicity(int i, int j)
{
	int k;
	double v1,v2,c;

	if (j<i) {k=j;j=i;i=k;}

	// Only rows structurally coupled to node i or node j can satisfy the
	// v1/v2 != 0 test, so enumerate those instead of sweeping all n rows
	// (the bdw bound that once limited the sweep is never set).
	std::vector<int> rows;
	CoupledRows(i, j, rows);

	for(int kk : rows){
		v1=Get(kk,i); v2=Get(kk,j);
		if ((v1!=0) || (v2!=0)){
			c=(v1-v2)/2.;
			Put(c,kk,i); Put(-c,kk,j);
		}
	}
	c=0.5*(Get(i,i)+Get(j,j));
	Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]-b[j]);
	b[i]=c; b[j]=-c;
}

void CBigLinProb::Periodicity(int i, int j)
{
	int k;
	double v1,v2,c;

	if (j<i) {k=j;j=i;i=k;}

	std::vector<int> rows;
	CoupledRows(i, j, rows);   // see AntiPeriodicity

	for(int kk : rows){
		v1=Get(kk,i); v2=Get(kk,j);
		if ((v1!=0) || (v2!=0)){
			c=(v1+v2)/2.;
			Put(c,kk,i); Put(c,kk,j);
		}
	}
	c=(Get(i,i)+Get(j,j))/2.;
	Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]+b[j]);
	b[i]=c; b[j]=c;
}

void CBigLinProb::ComputeBandwidth()
{
	int maxbw=0;
	for(int k=0;k<n;k++){
		if(!M[k].empty()){
			int bw = M[k].back().c - k;
			if(bw > maxbw) maxbw = bw;
		}
	}
	MsgBox("Assumed Bandwidth = %i\nActual Bandwidth = %i",bdw,maxbw);
}

int CBigLinProb::SaveMe(CString myFile)
{
	FILE *fp;
	if ((fp=femm_fopen((const char*)myFile,"wt"))==NULL) return 0;
	for(int i=0;i<n;i++){
		for(auto& e : M[i]){
			fprintf(fp,"%i %i %.15g\n",i+1,e.c+1,e.x);
			fprintf(fp,"%i %i %.15g\n",e.c+1,i+1,e.x);
		}
	}
	fclose(fp);
	return 1;
}

// Preconditioned Conjugate Gradient solver.
int CBigLinProb::PCGSolve(int flag)
{
	int i;
	double res, res_o, res_new;
	double er, del, rho, pAp;

	TheView->SetDlgItemText(IDC_FRAME1, "Conjugate Gradient Solver");
	TheView->m_prog1.SetPos(0);
	int prg1 = 0, prg2;

	if(!finalized) Finalize();

	// residual with V=0
	MultPC(b, Z);
	res_o = Dot(Z, b);
	// A zero RHS has the exact solution V = 0, whatever V currently holds.
	// The warm-start paths (flag != 0) arrive here with a previous iterate or
	// an initial guess loaded from a previous solution, so returning without
	// clearing V would hand that guess back as though it had been solved for.
	// The complex twin (cspars.cpp PBCGSolve) already zeroes V here.
	//
	// Test the RHS directly rather than inferring emptiness from res_o: the
	// two agree only while the preconditioner is definite, and QMRSolve's
	// constraint rows give the incomplete factorization signed pivots. If
	// res_o is zero for a NONZERO b, falling through is the right thing --
	// the isfinite guard in the iteration below turns the resulting division
	// into an honest failure instead of a false claim of convergence.
	if (Dot(b, b) == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
		return 1;
	}

	// if flag is false, initialize V with zeros
	if (flag == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
	}

	// form residual
	MultA(V, R);
	for (i = 0; i < n; i++) R[i] = b[i] - R[i];

	// form initial search direction
	MultPC(R, Z);
	for (i = 0; i < n; i++) P[i] = Z[i];
	res = Dot(Z, R);

	// Breakdown/stall guard: a non-finite er makes the while() below false,
	// which would return "converged" with garbage in V. The stall counter
	// mirrors QMRSolve's bBail; PCG previously had no such protection.
	double erBest=1e300;
	int nStall=0;
	const int stallLimit=(n/2>200)?(n/2):200;

	// do iteration
	do {
		// step i) compute A*P and step length
		MultA(P, U);
		pAp = Dot(P, U);
		del = res / pAp;

		// step ii) update solution
		for (i = 0; i < n; i++) V[i] += (del * P[i]);

		// step iii) update residual
		for (i = 0; i < n; i++) R[i] -= (del * U[i]);

		// step iv) apply preconditioner and compute new search direction
		MultPC(R, Z);
		res_new = Dot(Z, R);
		rho = res_new / res;
		res = res_new;

		// step v) update search direction
		for (i = 0; i < n; i++) P[i] = Z[i] + (rho * P[i]);

		// have we converged yet?
		er = sqrt(fabs(res / res_o));
		if (!std::isfinite(er)) return 0;
		if (er < erBest) { erBest = er; nStall = 0; }
		else if (++nStall >= stallLimit) return 0;

		prg2 = (int)(20. * log10(er) / (log10(Precision)));
		if (prg2 > prg1) {
			prg1 = prg2;
			prg2 = (prg1 * 5);
			if (prg2 > 100) prg2 = 100;
			TheView->m_prog1.SetPos(prg2);
			TheView->InvalidateRect(NULL, FALSE);
			TheView->UpdateWindow();
		}
	} while (er > Precision);

	return 1;
}

// QMR solver (symmetric-indefinite systems: circuit constraints).
int CBigLinProb::QMRSolve(int flag)
{
	int i;
	double res, res_o, res_new, tau, tta, tta_new, cn;
	double er, del, rho, pAp;
	double erLast=0;
	int bBail=0;

	TheView->SetDlgItemText(IDC_FRAME1, "QMR Solver");
	TheView->m_prog1.SetPos(0);
	int prg1 = 0, prg2;

	if(!finalized) Finalize();

	MultPC(b, Z);
	res_o = Dot(Z, b);
	// A zero RHS has the exact solution V = 0, whatever V currently holds.
	// The warm-start paths (flag != 0) arrive here with a previous iterate or
	// an initial guess loaded from a previous solution, so returning without
	// clearing V would hand that guess back as though it had been solved for.
	// The complex twin (cspars.cpp PBCGSolve) already zeroes V here.
	//
	// Test the RHS directly rather than inferring emptiness from res_o: the
	// two agree only while the preconditioner is definite, and QMRSolve's
	// constraint rows give the incomplete factorization signed pivots. If
	// res_o is zero for a NONZERO b, falling through is the right thing --
	// the isfinite guard in the iteration below turns the resulting division
	// into an honest failure instead of a false claim of convergence.
	if (Dot(b, b) == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
		return 1;
	}

	if (flag == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
	}

	MultA(V, R);

	for (i = 0; i < n; i++) {
		R[i] = b[i] - R[i];
		U[i] = 0;
	}

	MultPC(R, P);
	tau = Dot(P, P);
	res = Dot(P, R);
	tta=0;

	do {
		MultA(P, Z);
		pAp = Dot(P, Z);
		del = res / pAp;

		for (i = 0; i < n; i++) R[i] -= (del * Z[i]);

		MultPC(R, Z);
		res_new = Dot(Z, R);
		rho = res_new / res;
		res = res_new;
		tta_new = Dot(Z, Z) / tau;
		cn = 1. / sqrt(1. + tta_new * tta_new);
		tau *= (tta_new * cn);

		for (i = 0; i < n; i++) {
			U[i] = cn * cn * (tta * tta * U[i] + del * P[i]);
			V[i] += U[i];
			P[i] = Z[i] + (rho * P[i]);
		}
		tta=tta_new;

		er = sqrt(fabs(res / res_o));
		if (!std::isfinite(er)) return 0;

		if (erLast==0) erLast=er;
		if (er<erLast) { erLast=er; bBail=0; }
		else bBail++;

		if (bBail==(n/2)) {
			if (flag!=0){
				TheView->m_prog1.SetPos(0);
				TheView->InvalidateRect(NULL, FALSE);
				TheView->UpdateWindow();
				return QMRSolve(0);
			}
			else return 0;
		}

		prg2 = (int)(20. * log10(er) / (log10(Precision)));
		if (prg2 > prg1) {
			prg1 = prg2;
			prg2 = (prg1 * 5);
			if (prg2 > 100) prg2 = 100;
			TheView->m_prog1.SetPos(prg2);
			TheView->InvalidateRect(NULL, FALSE);
			TheView->UpdateWindow();
		}
	} while (er > Precision);

	return 1;
}
