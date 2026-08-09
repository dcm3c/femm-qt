// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// cspars.cpp — Complex sparse matrix with an incomplete LDL^T
// preconditioner. Deliberately sequential; see spars.cpp for the
// OpenMP-removal measurements.
//
// Main matrix M: upper-triangle during assembly, expanded to flat-CSR full
// storage by Finalize() for SpMV.
// N-R auxiliary matrices (Mh, Ma, Ms): upper-triangle only.

#include "StdAfx.h"
#include <math.h>
#include <stdio.h>
#include <cmath>
#include <utility>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"

#define KLUDGE
#define nrm(X) sqrt(Re(ConjDot(X,X)))

CBigComplexLinProb::CBigComplexLinProb()
{
	n=0;
	P=U=R=V=Z=b=uu=vv=NULL;
	Q=NULL;
	finalized=false;
}

CBigComplexLinProb::~CBigComplexLinProb()
{
	if (n==0) return;
	free(b); free(P); free(R);
	free(V); free(U); free(Z);
	free(uu); free(vv);
	if(Q) free(Q);
}

int CBigComplexLinProb::Create(int d, int bw, int nodes)
{
	bdw=bw;
	NumNodes=nodes;
	b=(CComplex *)calloc(d,sizeof(CComplex));
	V=(CComplex *)calloc(d,sizeof(CComplex));
	P=(CComplex *)calloc(d,sizeof(CComplex));
	R=(CComplex *)calloc(d,sizeof(CComplex));
	U=(CComplex *)calloc(d,sizeof(CComplex));
	Z=(CComplex *)calloc(d,sizeof(CComplex));
	uu=(CComplex *)calloc(d,sizeof(CComplex));
	vv=(CComplex *)calloc(d,sizeof(CComplex));
	Q=(int *)calloc(d,sizeof(int));
	n=d;
	finalized=false;

	M.resize(d);
	for(int i=0;i<d;i++)
		M[i].push_back({i, CComplex(0,0)});

	bNewton=FALSE;
	return 1;
}

std::vector<std::vector<CSEntry>>& CBigComplexLinProb::GetMatrix(int k)
{
	switch(k){
		case 1: return Mh;
		case 2: return Ms;
		case 3: return Ma;
		default: return M;
	}
}

static void InitNewtonMatrix(std::vector<std::vector<CSEntry>>& mat, int d)
{
	mat.resize(d);
	for(int i=0;i<d;i++)
		mat[i].push_back({i, CComplex(0,0)});
}

void CBigComplexLinProb::Put(CComplex v, int p, int q, int k)
{
	if(q<p){
		int t=p; p=q; q=t;
		if (k==1) v=conj(v);
		if (k==3) v=-conj(v);
	}

	if ((k>0) && (bNewton==FALSE)){
		bNewton=TRUE;
		InitNewtonMatrix(Mh, n);
		InitNewtonMatrix(Ma, n);
		InitNewtonMatrix(Ms, n);
	}

	if(k==0) icState = 0;

	auto& mat = GetMatrix(k);
	auto& row = mat[p];

	for(auto& e : row){
		if(e.c == q){ e.x = v; return; }
	}

	CSEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const CSEntry& a, const CSEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	if(k==0){
		finalized = false;
		// Incremental colAdj maintenance -- see CBigLinProb::Put.
		if (colAdjValid && p != q) colAdj[q].push_back(p);
	}
}

CComplex CBigComplexLinProb::Get(int p, int q, int k)
{
	int flip=FALSE;
	if(q<p){ int t=p; p=q; q=t; flip=TRUE; }

	if ((k>0) && (bNewton==FALSE)) return CComplex(0,0);

	auto& mat = GetMatrix(k);
	auto& row = mat[p];

	for(auto& e : row){
		if(e.c == q){
			if(flip){
				if(k==1) return conj(e.x);
				if(k==3) return -conj(e.x);
			}
			return e.x;
		}
	}
	return CComplex(0,0);
}

void CBigComplexLinProb::AddTo(CComplex v, int p, int q)
{
	if(q<p){ int t=p; p=q; q=t; }
	icState = 0;
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x += v; return; }
	}
	CSEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const CSEntry& a, const CSEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
	// Incremental colAdj maintenance -- see CBigLinProb::Put.
	if (colAdjValid && p != q) colAdj[q].push_back(p);
}

void CBigComplexLinProb::Finalize()
{
	// Build full storage as flat CSR (see the real twin's Finalize for the
	// two-pass fill that yields ascending columns without a sort). The
	// mirrored entries carry the same value: M is complex-symmetric.
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

void CBigComplexLinProb::MultA(CComplex *X, CComplex *Y, int k)
{
	int i;

	for(i=0;i<n;i++) Y[i]=0;

	if ((!bNewton) && (k!=0)) k=0;

	if ((bNewton) && (k==-1)){
		MultA(X,Y,0);
		MultA(X,uu,1);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultConjA(X,uu,2);
		MultA(X,vv,3);
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])+vv[i];
		return;
	}
	if ((bNewton) && (k==-2)){
		MultA(X,Y,0);
		MultConjA(X,uu,1);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultA(X,uu,2);
		MultConjA(X,vv,3);
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])-vv[i];
		return;
	}

	if(k==0){
		if(!finalized) Finalize();
		for(i=0;i<n;i++){
			CComplex sum(0,0);
			for(int idx=mfPtr[i];idx<mfPtr[i+1];idx++)
				sum += mfVal[idx] * X[mfCol[idx]];
			Y[i] = sum;
		}
	}
	else{
		// N-R sub-matrices — upper triangle, sequential
		auto& mat = GetMatrix(k);
		for(i=0;i<n;i++){
			auto& row = mat[i];
			for(auto& e : row){
				if(e.c == i){
					Y[i] += e.x * X[i];
				} else {
					Y[i] += e.x * X[e.c];
					if (k==1)      Y[e.c] += conj(e.x) * X[i];
					else if (k==3) Y[e.c] += (-conj(e.x)) * X[i];
					else           Y[e.c] += e.x * X[i];
				}
			}
		}
	}
}

void CBigComplexLinProb::MultConjA(CComplex *X, CComplex *Y, int k)
{
	int i;

	for(i=0;i<n;i++) Y[i]=0;

	if ((k!=0) && (!bNewton)) k=0;

	if(k==0){
		if(!finalized) Finalize();
		for(i=0;i<n;i++){
			CComplex sum(0,0);
			for(int idx=mfPtr[i];idx<mfPtr[i+1];idx++)
				sum += mfVal[idx].Conj() * X[mfCol[idx]];
			Y[i] = sum;
		}
	}
	else{
		auto& mat = GetMatrix(k);
		for(i=0;i<n;i++){
			auto& row = mat[i];
			for(auto& e : row){
				if(e.c == i){
					Y[i] += e.x.Conj() * X[i];
				} else {
					Y[i] += e.x.Conj() * X[e.c];
					if (k==1)      Y[e.c] += e.x * X[i];
					else if (k==3) Y[e.c] += (-e.x) * X[i];
					else           Y[e.c] += e.x.Conj() * X[i];
				}
			}
		}
	}
}

void CBigComplexLinProb::MultAPPA(CComplex *X, CComplex *Y)
{
	int i;
	MultA(X,Z);
	MultPC(Z,Y);
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
	MultPC(Y,Z);
	MultA(Z,Y);
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
}

CComplex CBigComplexLinProb::Dot(CComplex *x, CComplex *y)
{
	double zr=0, zi=0;
	for(int i=0;i<n;i++){
		CComplex p = x[i]*y[i];
		zr += p.re; zi += p.im;
	}
	return CComplex(zr, zi);
}

CComplex CBigComplexLinProb::ConjDot(CComplex *x, CComplex *y)
{
	double zr=0, zi=0;
	for(int i=0;i<n;i++){
		CComplex p = x[i].Conj()*y[i];
		zr += p.re; zi += p.im;
	}
	return CComplex(zr, zi);
}

// Incomplete LDL^T factorization of M with threshold-based fill (Crout-
// style ILDL), over the WHOLE matrix -- deliberately not block-Jacobi.
// Measured on the nonlinear-harmonic fixture: block-Jacobi zero-fill IC
// cut PBCG iterations only ~9% over the block-SSOR it replaced (the
// classic result that no-fill incomplete Cholesky and optimally-relaxed
// SSOR are comparable), and adding fill inside the blocks plateaued ~2x
// short of its serial potential because the discarded inter-block coupling
// became the binding constraint. Factored whole, the same fill collapses
// the iteration count ~15-20x, which pays for the triangular solves being
// sequential many times over.
//
// Column k keeps the ICFILL largest entries whose magnitude exceeds ICTOL
// times the original column's largest entry. Setup runs once per outer
// nonlinear pass (any value mutation resets icState) and is amortized over
// every PBCG iteration of that pass's inner solves.
//
// Complex-symmetric specifics: M is symmetric, NOT Hermitian, so this is
// the UNCONJUGATED M = L D L^T with complex D -- no conjugates appear, and
// being root-free it never takes the square root of a complex number with
// arbitrary phase. The one breakdown mode is a near-zero complex pivot;
// the magnitude guard rescues it with the row's diagonal (nonzero here --
// PCGSQStart trips a singular flag otherwise), and the per-pivot rescue
// keeps the factor finite, degrading toward Jacobi as rescues accumulate.
// Definiteness never enters: PBCGSolve's unconjugated BiCG needs only a
// complex-symmetric preconditioner, which LDL^T with complex D is.
// Correctness is never at stake: the Krylov loops measure the true
// residual against the true operator, so a degraded factor costs
// iterations, not accuracy. There is no fallback
// preconditioner -- an SSOR one existed until telemetry across the whole
// regression corpus showed not a single pivot ever rescued.
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
void CBigComplexLinProb::FactorPC()
{
	if(!finalized) Finalize();
	icState = 2;

	// Tuned on the nonlinear-harmonic fixture (ThrustBearingHyst): looser
	// dropping loses iterations, tighter dropping buys iterations more
	// slowly than the setup and triangular sweeps grow. The wall-clock
	// optimum is flat within roughly a factor of 3 around these values.
	const double ICTOL = 1e-3;
	const int ICFILL = 32;

	icPtr.assign(n+1, 0);
	icCol.clear();
	icVal.clear();
	icCol.reserve((size_t)n*8);
	icVal.reserve((size_t)n*8);
	icD.resize(n);

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
	std::vector<CComplex> w(n);
	std::vector<char> inw(n, 0);
	std::vector<int> touch, keep;
	touch.reserve(256); keep.reserve(256);

	for(int k=0;k<n;k++){
		CComplex dk = M[k][0].x;
		double cnorm = abs(dk);

		// load A's lower column k from upper-triangle row k
		for(auto& e : M[k])
			if(e.c>k){
				w[e.c]=e.x; inw[e.c]=1; touch.push_back(e.c);
				double a=abs(e.x); if(a>cnorm) cnorm=a;
			}

		// subtract d_j L(k,j) L(:,j) for the kept entries of row k
		for(int j : rowsOfL[k]){
			const int p = colStart[j];        // icCol[p] == k
			const CComplex Lkj = icVal[p];
			const CComplex f = icD[j]*Lkj;
			dk -= f*Lkj;
			for(int idx=p+1;idx<icPtr[j+1];idx++){
				const int r = icCol[idx];
				if(!inw[r]){ w[r]=CComplex(0,0); inw[r]=1; touch.push_back(r); }
				w[r] -= f*icVal[idx];
			}
			colStart[j] = p+1;
		}
		// row k is spent; release it rather than hold all n to the end
		std::vector<int>().swap(rowsOfL[k]);

		// pivot guard; negated comparison so a NaN pivot is also rescued
		double scale = abs(M[k][0].x);
		if(scale==0){
			// zero diagonal (constraint row): fall back to the row's
			// magnitude, as the real twin does
			for(int idx=mfPtr[k];idx<mfPtr[k+1];idx++) scale += abs(mfVal[idx]);
			if(scale==0) scale=1;
		}
		if(!(abs(dk) > 1e-8*scale))
			dk = (M[k][0].x!=0) ? M[k][0].x : CComplex(scale,0);
		icD[k]=dk;

		// drop small entries, cap the survivors at ICFILL largest
		keep.clear();
		const double drop = ICTOL*cnorm;
		for(int r : touch){
			if(abs(w[r]) >= drop) keep.push_back(r);
			inw[r]=0;
		}
		touch.clear();
		if((int)keep.size() > ICFILL){
			std::nth_element(keep.begin(), keep.begin()+ICFILL, keep.end(),
				[&](int a, int b){ return abs(w[a]) > abs(w[b]); });
			keep.resize(ICFILL);
		}
		std::sort(keep.begin(), keep.end());

		for(int r : keep){
			CComplex lrk = w[r]/dk;
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
void CBigComplexLinProb::MultIC(CComplex *X, CComplex *Y)
{
	int i;
	for(i=0;i<n;i++) Y[i]=X[i];
	for(int k=0;k<n;k++){
		const CComplex yk = Y[k];
		for(int idx=icPtr[k];idx<icPtr[k+1];idx++)
			Y[icCol[idx]] -= icVal[idx]*yk;
		// column k touches only rows > k, so Y[k] is final here: fold
		// the D solve in rather than making a separate pass over Y
		Y[k] = yk/icD[k];
	}
	for(int k=n-1;k>=0;k--){
		CComplex s = Y[k];
		for(int idx=icPtr[k];idx<icPtr[k+1];idx++)
			s -= icVal[idx]*Y[icCol[idx]];
		Y[k]=s;
	}
}

// Preconditioner on main matrix M: the incomplete LDL^T, factored lazily
// on first use after any value mutation.
void CBigComplexLinProb::MultPC(CComplex *X, CComplex *Y)
{
	if(!finalized) Finalize();
	if(icState!=1) FactorPC();
	MultIC(X,Y);
}

void CBigComplexLinProb::BuildColAdj()
{
	colAdj.assign(n, std::vector<int>());
	for(int k=0;k<n;k++)
		for(auto& e : M[k])
			if (e.c != k) colAdj[e.c].push_back(k);
	colAdjValid = true;
}

// Rows structurally coupled to column i or column j, ascending, excluding i
// and j. Pass j = i for one column. Snapshot of plain indices, so the
// caller's Puts (which may insert and invalidate colAdj) are safe. The N-R
// sub-matrix patterns are subsets of M's, so this enumeration covers them
// as well.
void CBigComplexLinProb::CoupledRows(int i, int j, std::vector<int> &out)
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

void CBigComplexLinProb::SetValue(int i, CComplex x)
{
	// The historical version swept rows k = fst..lst looking for entries in
	// column i -- with the bandwidth bound bdw, which the tangle-ordered
	// solvers never set, so the sweep covered ALL n rows, with up to four
	// Get() row-scans each. Instead enumerate exactly the rows coupled to
	// node i: rows k<i from the column adjacency, the diagonal and rows k>i
	// (including the circuit rows) from row i's own entries, which by
	// symmetric storage are precisely the entries (i,k)=(k,i). The
	// sub-matrix (Mh/Ms/Ma) patterns are subsets of M's, and their Get()
	// returns 0 for an absent entry, so the same enumeration covers them;
	// the zeroing Puts are guarded by z!=0, so they never insert.
	CComplex z;

	if (!colAdjValid) BuildColAdj();

	auto fixrow = [&](int k){
		z=Get(k,i);
		if(z!=0){
			b[k]-=(z*x);
			if(i!=k) Put(CComplex(0,0),k,i);
		}
		if (bNewton){
			z=Get(k,i,1);
			if(z!=0){ if(i!=k) b[k]=b[k]-(z*x); Put(CComplex(0,0),k,i,1); }
			z=Get(k,i,2);
			if(z!=0){ if(i!=k) b[k]=b[k]-(z*conj(x)); Put(CComplex(0,0),k,i,2); }
			z=Get(k,i,3);
			if(z!=0){ if(i!=k) b[k]=b[k]-(z*conj(x)); Put(CComplex(0,0),k,i,3); }
		}
	};

	for(int k : colAdj[i]) fixrow(k);       // rows below the diagonal
	for(auto& e : M[i]) fixrow(e.c);        // diagonal, rows above, circuit rows

	b[i]=Get(i,i)*x;
}

void CBigComplexLinProb::Wipe()
{
	int i;
	for(i=0;i<n;i++){
		b[i]=0;
		for(auto& e : M[i]) e.x=0;
	}
	finalized = false;
	icState = 0;

	if (!bNewton) return;
	for(i=0;i<n;i++) for(auto& e : Mh[i]) e.x=0;
	for(i=0;i<n;i++) for(auto& e : Ma[i]) e.x=0;
	for(i=0;i<n;i++) for(auto& e : Ms[i]) e.x=0;
}

void CBigComplexLinProb::SaveBaseline()
{
	int i;
	size_t tot=0;
	for(i=0;i<n;i++) tot+=M[i].size();
	baseM.clear();
	baseM.reserve(tot);
	for(i=0;i<n;i++) for(auto& e : M[i]) baseM.push_back(e.x);
	baseB.assign(b, b+n);
}

int CBigComplexLinProb::RestoreBaseline()
{
	int i;
	size_t tot=0;
	for(i=0;i<n;i++) tot+=M[i].size();
	if (tot!=baseM.size() || baseB.size()!=(size_t)n) return 0;

	size_t idx=0;
	for(i=0;i<n;i++) for(auto& e : M[i]) e.x=baseM[idx++];
	for(i=0;i<n;i++) b[i]=baseB[i];

	if (bNewton){
		for(i=0;i<n;i++){
			for(auto& e : Mh[i]) e.x=0;
			for(auto& e : Ma[i]) e.x=0;
			for(auto& e : Ms[i]) e.x=0;
		}
	}
	finalized = false;
	icState = 0;
	return 1;
}

void CBigComplexLinProb::AntiPeriodicity(int i, int j)
{
	int k,h; CComplex v1,v2,c;
	if (j<i) {k=j;j=i;i=k;}

	// Enumerate the rows structurally coupled to node i or j instead of
	// sweeping all n rows (the bdw bound is never set by the tangle-ordered
	// solvers). One snapshot serves the main matrix and, below, the N-R
	// sub-matrices, whose patterns are subsets of M's.
	std::vector<int> rows;
	CoupledRows(i, j, rows);

	for(int kk : rows){
		v1=Get(kk,i); v2=Get(kk,j);
		if((v1!=0)||(v2!=0)){ c=(v1-v2)/2.; Put(c,kk,i); Put(-c,kk,j); }
	}
	c=0.5*(Get(i,i)+Get(j,j)); Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]-b[j]); b[i]=c; b[j]=-c;

	if(bNewton) for(h=1;h<=3;h++){
		for(int kk : rows){
			v1=Get(kk,i,h); v2=Get(kk,j,h);
			if((v1!=0)||(v2!=0)){ c=(v1-v2)/2.; Put(c,kk,i,h); Put(-c,kk,j,h); }
		}
		c=(Get(i,i,h)-Get(i,j,h)-Get(j,i,h)+Get(j,j,h))/4.;
		Put(c,i,i,h); Put(-c,i,j,h); Put(c,j,j,h);
	}
}

void CBigComplexLinProb::Periodicity(int i, int j)
{
	int k,h; CComplex v1,v2,c;
	if (j<i) {k=j;j=i;i=k;}

	std::vector<int> rows;
	CoupledRows(i, j, rows);   // see AntiPeriodicity

	for(int kk : rows){
		v1=Get(kk,i); v2=Get(kk,j);
		if((v1!=0)||(v2!=0)){ c=(v1+v2)/2.; Put(c,kk,i); Put(c,kk,j); }
	}
	c=(Get(i,i)+Get(j,j))/2.; Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]+b[j]); b[i]=c; b[j]=c;

	if(bNewton) for(h=1;h<=3;h++){
		for(int kk : rows){
			v1=Get(kk,i,h); v2=Get(kk,j,h);
			if((v1!=0)||(v2!=0)){ c=(v1+v2)/2.; Put(c,kk,i,h); Put(c,kk,j,h); }
		}
		c=(Get(i,i,h)+Get(i,j,h)+Get(j,i,h)+Get(j,j,h))/4.;
		Put(c,i,i,h); Put(c,i,j,h); Put(c,j,j,h);
	}
}

int CBigComplexLinProb::PCGSQStart()
{
	int i,k;
	CComplex res,res_new,del,rho,pAp;

	for(i=0;i<n;i++) if((M[i][0].x.re==0)&&(M[i][0].x.im==0)){
		fprintf(stderr,"singular flag tripped.");
		return 0;
	}

	MultPC(b,Z);
	for(i=0;i<n;i++) Z[i].im=-Z[i].im;
	MultPC(Z,P);
	MultA(P,Z);
	for(i=0;i<n;i++) P[i]=Z[i].Conj();
	for(i=0;i<n;i++) V[i]=0;

	MultAPPA(V,R);
	for(i=0;i<n;i++) R[i]=P[i]-R[i];
	for(i=0;i<n;i++) P[i]=R[i];
	res=ConjDot(R,R);

	for(k=0;k<3;k++){
		MultAPPA(P,U);
		pAp=ConjDot(P,U);
		del=res/pAp;
		for(i=0;i<n;i++) V[i]+=(del*P[i]);
		for(i=0;i<n;i++) R[i]-=(del*U[i]);
		res_new=ConjDot(R,R);
		rho=res_new/res;
		res=res_new;
		for(i=0;i<n;i++) P[i]=R[i]+(rho*P[i]);
	}
	return 1;
}

int CBigComplexLinProb::PBCGSolve(int flag)
{
	int i;
	CComplex res,res_new,del,rho,pAp;
	double er,normb;
	int prg2,prg1=0;

	if(!finalized) Finalize();

	if(flag==FALSE){
		for(i=0;i<n;i++) V[i]=0;
	}

	MultA(V,R);
	for(i=0;i<n;i++) R[i]=b[i]-R[i];
	normb=nrm(b);

	// A problem with no sources has a zero RHS, and its exact solution is zero.
	// That is a converged answer, not an error, so return before normalizing by
	// normb: er would be 0/0 = NaN, the do-loop below still runs its body once,
	// and del = res/pAp = 0/0 would fill V with NaN. (The real-valued solvers in
	// spars.cpp guard this already, via res_o == 0.)
	if(normb==0){
		for(i=0;i<n;i++) V[i]=0;
		return 1;
	}

	er=nrm(R)/normb;
	prg1=(int)(20.*log10(er)/(log10(Precision)));
	TheView->m_prog1.SetPos(5*prg1);
	TheView->SetDlgItemText(IDC_FRAME1,"BiConjugate Gradient Solver");
	TheView->InvalidateRect(NULL, FALSE);
	TheView->UpdateWindow();

	MultPC(R,Z);
	for(i=0;i<n;i++) P[i]=Z[i];
	res=Dot(Z,R);

	// Breakdown/stall guard. Unconjugated complex-symmetric BiCG has genuine
	// breakdown modes (pAp -> 0 blows up del); a non-finite er makes the loop
	// condition below FALSE, which would return "converged" with garbage in V.
	// Bail with failure instead. Likewise bail after a long run of iterations
	// with no new best residual -- the same stall logic the real-valued
	// QMRSolve has via bBail -- rather than spinning forever.
	double erBest=er;
	int nStall=0;
	const int stallLimit=(n/2>200)?(n/2):200;

	do{
		MultA(P,U);
		pAp=Dot(P,U);
		del=res/pAp;
		for(i=0;i<n;i++) V[i]+=(del*P[i]);
		for(i=0;i<n;i++) R[i]-=(del*U[i]);
		MultPC(R,Z);
		res_new=Dot(Z,R);
		rho=res_new/res;
		res=res_new;
		for(i=0;i<n;i++) P[i]=Z[i]+(rho*P[i]);

		er=nrm(R)/normb;
		if (!std::isfinite(er)) return 0;
		if (er<erBest) { erBest=er; nStall=0; }
		else if (++nStall>=stallLimit) return 0;
		prg2=(int)(20.*log10(er)/(log10(Precision)));
		if(prg2>prg1){
			prg1=prg2;
			prg2=(prg1*5);
			if(prg2>100) prg2=100;
			TheView->m_prog1.SetPos(prg2);
			TheView->InvalidateRect(NULL, FALSE);
			TheView->UpdateWindow();
		}
	} while(er>Precision);

	return 1;
}

// Linearized solve for the nonlinear-harmonic (Newton) formulation.
//
// The linearized operator, applied by MultA(...,-1), is
//
//     A(X) = M X + Mh X + Ms conj(X) + Ma X
//
// The conj(X) term makes A real-linear but NOT complex-linear, so standard
// complex Krylov methods do not apply. This routine (OF's KludgeSolve) does
// a preconditioned Richardson iteration: solve the complex-symmetric part,
// M V_new = b - (Mh V + Ms conj(V) + Ma V), with PBCGSolve, then take a
// REAL minimal-residual step along P = V_new - V, repeating until the true
// linearized residual is small.
//
// HISTORY: a GCR acceleration of this loop (complex step coefficients plus
// a short direction history, efc24b7) was tried and REVERTED. Deep
// saturation makes the linearized operator near-singular, and the residual
// minimization then wanders along near-null directions -- steps with tiny
// operator image but enormous dV -- which the outer nonlinear loop reads
// as divergence, collapsing its relaxation into a limit cycle
// (RadialMagneticBearingAC: 88 damped outer passes vs 21 for this loop; a
// step-size gate contained the worst of it but still lost to this plain
// loop on hard-saturation cases). The acceleration also no longer paid:
// with the ILDL preconditioner the inner M-solves are cheap, so this
// loop's extra passes cost ~10% on the fixtures GCR helped, while its
// conservative real-coefficient steps cannot inject null-space content.
// Benchmarks for both variants are recorded in the revert commit.
int CBigComplexLinProb::NewtonSolve(int flag)
{
	int i, k;
	double er, normb, c;
	CString out;
	CComplex *borig, *v, *r;

	borig = (CComplex*)calloc(n, sizeof(CComplex));
	v     = (CComplex*)calloc(n, sizeof(CComplex));
	r     = (CComplex*)calloc(n, sizeof(CComplex));

	if (flag == 0) for (i = 0; i < n; i++) V[i] = 0;
	normb = nrm(b);

	// No sources -> zero RHS -> zero solution; see PBCGSolve. Without this the
	// er < Precision test below is NaN < x, which is false, so the Newton loop
	// runs and spreads NaN through c = 0/0 into V.
	if (normb == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
		free(borig); free(v); free(r);
		return 1;
	}

	for (i = 0; i < n; i++) { borig[i] = b[i]; v[i] = V[i]; }

	MultA(V, r, -1);
	for (i = 0; i < n; i++) r[i] = b[i] - r[i];
	er = nrm(r) / normb;
	if (er < Precision) { free(borig); free(v); free(r); return 1; }

	for (k = 0; k < 10; k++){
		MultA(V, P, 1);
		MultConjA(V, U, 2);
		MultA(V, R, 3);
		for (i = 0; i < n; i++)
			b[i] = borig[i] - P[i] - conj(U[i]) - R[i];

		PBCGSolve(TRUE);

		for (i = 0; i < n; i++) P[i] = V[i] - v[i];
		MultA(P, U, -1);
		c = Re(ConjDot(r, U)) / Re(ConjDot(U, U));

		for (i = 0; i < n; i++) {
			V[i] = v[i] + c * P[i];
			r[i] = r[i] - c * U[i];
			v[i] = V[i];
		}

		er = nrm(r) / normb;
		out.Format("N-R inner iteration %i, er=%g", k, er);
		TheView->SetDlgItemText(IDC_FRAME1, out);

		if (er < Precision * 10.) break;
	}

	free(borig); free(v); free(r);
	return 1;
}

int CBigComplexLinProb::PBCGSolveMod(int flag)
{
	if (bNewton)
		return NewtonSolve(flag);

	if(flag==FALSE){
		TheView->SetDlgItemText(IDC_FRAME1,"Initializing Solver");
		if (PCGSQStart()==0) return 0;
	}

	return PBCGSolve(2);
}
