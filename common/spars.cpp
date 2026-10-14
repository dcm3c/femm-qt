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
#include <cfloat>
#include <algorithm>
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
// On the SPD path (M's diagonal entirely positive, which is what PCGSolve
// serves) a nonpositive pivot is BREAKDOWN, and is handled by refactoring
// with a diagonal shift; see FactorPC below. On the indefinite path D is
// allowed to go negative -- QMR needs a symmetric preconditioner, not a
// definite one, and LDL^T with signed D is exactly that -- so there a
// nonpositive pivot is expected and only a non-finite one is breakdown.
// Correctness is never at stake either way: the Krylov loops measure the
// true residual against the true operator, so a degraded factor costs
// iterations, never accuracy.
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

// One factorization attempt against M + alpha*diag(M). Returns false on
// breakdown, having left icPtr/icCol/icVal/icD in a partial state the
// caller is expected to overwrite by retrying. alpha == 0 is the ordinary
// case and reproduces the unshifted factorization exactly.
bool CBigLinProb::FactorPCOnce(double alpha)
{
	if(!finalized) Finalize();

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
		// The shift is Manteuffel's: factor M + alpha*diag(M) rather than M
		// itself. Only the diagonal moves, so the sparsity pattern, the
		// dropping rule and (at alpha == 0) every stored value are unchanged.
		//
		// A ZERO diagonal is the exception, and it is not academic: a flux
		// constraint whose CircuitMatrix has no self term is a pure Lagrange
		// multiplier, and its row arrives here with a_kk == 0. No multiple of
		// zero strengthens anything, so those rows shift on the row's 1-norm
		// instead -- the same substitute the pivot guard below already uses
		// for them, so nothing new is being invented, and alpha == 0 still
		// leaves the diagonal at exactly zero. Sign: there is none to
		// preserve at zero, and positive matches what the guard picks.
		const double akk = M[k][0].x;
		const double dkk = (akk != 0) ? akk*(1.0+alpha)
		                              : alpha*RowNorm(k);
		double dk = dkk;
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

		// pivot guard; negated comparisons so a NaN pivot is also caught
		double scale = fabs(dkk);
		if(scale==0) scale = RowNorm(k);   // zero diagonal (constraint row)
		if(!std::isfinite(dk)) return false;
		if(posDiag){
			// SPD path. Exact Cholesky of an SPD matrix cannot produce a
			// nonpositive pivot, so one here means the DISCARDED fill has
			// pushed the incomplete Schur complement indefinite -- classic
			// incomplete-factorization breakdown. Patching the pivot in place
			// keeps D positive but leaves L's already-computed column divided
			// by a value inconsistent with it, and the error compounds
			// column over column: on the motor case that first exposed this,
			// the pivots ran -1.9e-3, -2.0, -43, -5.4e8, -4.2e18 ... to
			// overflow within ten columns, and the NaN reached PCG as
			// "Couldn't solve the problem" on a problem that is perfectly
			// well posed. Refactor with a larger shift instead.
			if(!(dk > 1e-8*scale)) return false;
		}
		else{
			// Indefinite path (constraint rows): a NEGATIVE pivot is
			// legitimate here and carries the inertia, so it is not a
			// breakdown. A VANISHING one is -- dividing L's column by it
			// means the factor has stopped approximating M in a way nothing
			// downstream can see. The old code patched such a pivot in place
			// and carried on; refactoring under a shift is the better
			// answer, and costs nothing, because across the entire
			// regression corpus (51 suite rows plus the harmonic fixtures)
			// this branch has never once been reached.
			if(!(fabs(dk) > 1e-8*scale)) return false;
		}
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
			if(!std::isfinite(lrk)) return false;
			icCol.push_back(r);
			icVal.push_back(lrk);
			rowsOfL[r].push_back(k);
		}
		icPtr[k+1]=(int)icCol.size();
		colStart[k]=icPtr[k];
	}

	return true;
}

// Row k's 1-norm, never zero. The stand-in for a diagonal that is zero and
// so carries no scale of its own -- used both as the pivot guard's yardstick
// and, in FactorPCOnce, as what such a row shifts on.
double CBigLinProb::RowNorm(int k)
{
	double s = 0;
	for(int idx=mfPtr[k];idx<mfPtr[k+1];idx++) s += fabs(mfVal[idx]);
	return (s>0) ? s : 1.0;
}

// The smallest alpha at which the shifted matrix FactorPCOnce builds is
// diagonally dominant, or -1 if some diagonal is non-finite.
//
// This is the shift at which breakdown becomes impossible rather than
// merely unlikely. A matrix diagonally dominant in MAGNITUDE is an
// H-matrix -- the definition runs through the comparison matrix, which
// carries |a_ii|, so the SIGNS of the diagonal are irrelevant -- and for an
// H-matrix an incomplete factorization exists with pivots bounded away
// from zero WHATEVER is dropped (Meijerink & van der Vorst 1977;
// Manteuffel 1980). That covers the quasi-definite (+,...,+,-) matrix the
// circuit constraints produce just as it covers the SPD one, which is why
// the test is on fabs and not on positivity.
//
// It has to mirror FactorPCOnce's shift exactly, including the zero-
// diagonal rows a pure Lagrange multiplier produces: those shift on the
// row's 1-norm. Every row therefore has a finite answer and the guarantee
// is available for EVERY matrix -- the reason this returns -1 only for a
// non-finite diagonal, which is not a shift problem but a broken matrix.
//
// DOMINANCE FACTOR. What is targeted is |a_kk| >= DOMFAC * (off-diagonal
// sum), not mere equality. Equality is weak dominance, and weak dominance
// does not carry the theorem: the comparison matrix is then a SINGULAR
// M-matrix and a pivot may still vanish. That is not a hypothetical -- for
// the textbook [[0,c],[c,0]], the alpha that achieves equality drives the
// SECOND pivot exactly to zero. With a factor of 2 the standard bound
// |d_k| >= |a_kk| * (1 - 1/DOMFAC) applies, so pivots are held at half the
// shifted diagonal. 2 is a choice, but it is a choice with a stated
// consequence rather than a number tuned to a case, and it is paid for only
// on the rung nothing has yet reached.
//
// Computing this beats guessing a ceiling: one pass over the CSR, and the
// answer is a property of this matrix rather than a constant fitted to
// whichever model happened to be on the bench.
double CBigLinProb::DominanceShift()
{
	if(!finalized) Finalize();
	const double DOMFAC = 2.0;
	double worst = 0;
	for(int i=0;i<n;i++){
		const double a = M[i][0].x;
		if(!std::isfinite(a)) return -1;
		double off = 0;
		for(int idx=mfPtr[i];idx<mfPtr[i+1];idx++)
			if(mfCol[idx]!=i) off += fabs(mfVal[idx]);
		// (1+alpha)*|a| >= DOMFAC*off, or alpha*RowNorm >= DOMFAC*off at a == 0
		const double need = (a != 0) ? (DOMFAC*off/fabs(a) - 1.0)
		                             : (off>0 ? DOMFAC*off/RowNorm(i) : 0.0);
		if(need>worst) worst = need;
	}
	return worst;
}

// Build the preconditioner, retrying with a growing diagonal shift if the
// incomplete factorization breaks down.
//
// Breakdown is rare -- across the whole regression corpus not one problem
// took a shift, which is why an unshifted first attempt stays the fast path
// and reproduces earlier results bit for bit. It is not, however, hypo-
// thetical: a saturated PM motor stepped from a previous solution across a
// sliding band drove the SPD path to a negative pivot at Newton step 7 and
// overflowed the factor within ten columns.
//
// Manteuffel's cure: refactor M + alpha*diag(M). The ramp below is a search
// for the WEAKEST sufficient shift, since every bit of alpha costs
// preconditioner strength -- but it ends at DominanceShift(), where success
// is a theorem rather than a hope, so the search cannot run out. A shifted
// factor costs iterations and nothing else -- the Krylov loops measure the
// true residual against the true operator -- so this trades a slower solve
// for a solve, where before there was none.
//
// Measured: the motor case that prompted all this succeeds at 1e-3, the
// deliberately adversarial matrix in tests/test_spars_pc.cpp at 1e-1.
// Nothing yet observed has needed the guaranteed rung, let alone the
// diagonal below it.
void CBigLinProb::FactorPC()
{
	if(!finalized) Finalize();
	icState = 2;

	// Unshifted first, and nothing else is computed unless that fails --
	// which is the overwhelmingly common case, so the fast path pays
	// nothing at all for any of what follows.
	if(FactorPCOnce(0.0)){ icState = 1; return; }

	// A geometric ramp, then the shift that makes breakdown impossible.
	// Ramp entries at or past that one are skipped: it is guaranteed and
	// they are not.
	const double guaranteed = DominanceShift();
	static const double ramp[] = { 1e-3, 1e-2, 1e-1, 1.0 };
	for(double alpha : ramp){
		if(guaranteed >= 0 && alpha >= guaranteed) break;
		if(FactorPCOnce(alpha)){ icState = 1; return; }
	}
	if(guaranteed >= 0 && FactorPCOnce(guaranteed)){ icState = 1; return; }

	// Belt and braces: reachable only through a non-finite diagonal (a
	// broken matrix, not a shift the ladder failed to find) or if rounding
	// defeated the theorem. The diagonal itself: L is empty, so MultIC
	// reduces to a diagonal scale, and a nonzero diagonal is all it needs.
	// Rows with a zero diagonal take the row's 1-norm, as above.
	icPtr.assign(n+1, 0);
	icCol.clear();
	icVal.clear();
	icD.resize(n);
	for(int k=0;k<n;k++){
		const double d = M[k][0].x;
		icD[k] = (d!=0 && std::isfinite(d)) ? d : RowNorm(k);
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

	// Breakdown guard: a non-finite er makes the while() below false, which
	// would return "converged" with garbage in V. FEMM 4.3 has that hole;
	// bailing with failure instead is a correctness fix and stays.
	//
	// It is the PRECONDITIONER that can get us here, not a singular A. CG
	// needs M positive definite. FactorPCOnce decides posDiag once, from
	// A's diagonal; when it holds, a nonpositive pivot is refused outright
	// and the accepted factor has icD > 0 throughout. When it does NOT --
	// a floating node, a zero diagonal -- the other branch tests only
	// fabs(dk) > 1e-8*scale, so icD can come back MIXED SIGN and MultPC
	// then applies an indefinite preconditioner. res = Dot(Z,R) is then
	// R'M^-1 R, which can vanish or change sign at a nonzero residual, and
	// rho = res_new/res divides through zero. That branch has never been
	// reached by the corpus, which is exactly why the guard is worth its
	// one comparison: the failure it prevents is silent, not loud.
	//
	// There is deliberately NO stall counter here. One was added in 876c0e9
	// because QMRSolve had bBail, which was the wrong reason: bBail is
	// Meeker's, it belongs to a different algorithm, and upstream pairs it
	// with an SSOR preconditioner that cannot degrade. Under the ILDL
	// preconditioner the factor's QUALITY varies with which rung of
	// FactorPC's ladder happened to factor, so a stall counter fires on a
	// merely slow solve of a well-posed problem and turns it into
	// "Couldn't solve the problem". Slow is not wrong; a user can cancel a
	// slow solve and cannot recover a refused one.

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
//
// Preconditioned CG on the indefinite system with QMR smoothing of the
// iterate (Freund's quasi-minimal residual laid over the Lanczos/CG
// recurrence). Two of its divisions are by INDEFINITE bilinear forms:
// res = R'M^-1 R and pAp = P'A P. Both can vanish for a nonzero vector,
// because A carries the negative flux block and the ILDL factor carries
// signed pivots. That is not a preconditioner defect: it is symmetric
// Lanczos in an indefinite inner product, and the recurrence breaks down
// on that product's isotropic vectors.
//
// Where it bites in practice (measured 2026-09 on myTestProblem with a
// manufactured isotropic warm start; the bench lives outside the tree):
// a WARM start whose error e has (Ae)'M^-1(Ae) ~ 0. Written out, that form
// is field-error energy MINUS circuit-error energy, so a cold start (e = x*,
// field-dominated) is safe while a transient step's warm start crosses zero
// at random -- the one-in-a-thousand transient failures. Three things
// follow, each measured:
//
//   * er = sqrt(|res/res_o|) is not a norm on that path. It read 9e-9 at a
//     step where the true residual was 8e-3, so the loop returned
//     "converged" after one iteration with the warm start as the answer and
//     the circuit current 0.5% off. The exit test below is sqrt(tau/tau_b):
//     tau is the QMR quasi-residual of the PRECONDITIONED residual, a norm
//     that bounds the residual it stands for (up to the usual sqrt(k)), and
//     tau_b = |M^-1 b|^2 is a cold start's initial tau. A warm start is
//     measured against that same absolute scale on purpose: its own initial
//     tau would demand Precision relative to an already-small residual.
//     Cost: one extra iteration on a healthy solve.
//
//   * The breakdown is visible at once. |res|/(|R||M^-1 R|) is a cosine in
//     the M^-1 form, and it drops below sqrt(eps) exactly when the next
//     division loses half its digits: >= 1e-8 solved normally, ~1e-9
//     degraded, <= 1e-10 stalled or lied. Healthy solves stay above 1e-2.
//     One extra Dot(R,R) per iteration buys the test; the other two dot
//     products are already computed.
//
//   * Restarting from the SAME iterate reproduces the same R and the same
//     breakdown. Restarting from zero works (b's form is far from zero) but
//     discards the warm start. One minimal-residual step V += a*M^-1 R with
//     a = (Az.R)/(Az.Az) moves the residual off the isotropic surface, and
//     a fresh Lanczos sequence from there converges in the iteration count
//     of a healthy warm start: 23 against 23, where zero cost 30 and the
//     previous code took 3372 or returned garbage.
//
// Recovery is therefore a BOUNDED chain, so a genuinely unsolvable system
// still fails loudly: up to kMaxNudge nudges from the current iterate, then
// zero, then nudges from zero, then failure. Non-finite state, a frozen tau
// (the smoothing factor saturates at 1 when |M^-1 R|^2 >> tau, seen on a
// diverging trajectory), and Meeker's bBail stagnation count (n/2 steps
// without |res| improving, the backstop that rescued stalls before the
// detector existed) all route to the same chain. Restart on the bBail path
// used to be a recursive QMRSolve(0); it is the zero rung here.
int CBigLinProb::QMRSolve(int flag)
{
	int i;
	double res, res_new, tau, tau_b, tta, tta_new, cn;
	double er, del, rho, pAp, rr, zz, resBest;
	int bBail;
	const int stallLimit = std::max(n / 2, 1);
	const double kappaMin = sqrt(DBL_EPSILON);   // half the digits gone
	const int kMaxNudge = 3;
	int nudges = 0;
	bool fromZero = (flag == 0), nudge = false;

	TheView->SetDlgItemText(IDC_FRAME1, "QMR Solver");
	TheView->m_prog1.SetPos(0);
	int prg1 = 0, prg2;

	if(!finalized) Finalize();

	MultPC(b, Z);
	tau_b = Dot(Z, Z);

	// A zero RHS has the exact solution V = 0, whatever V currently holds.
	// The warm-start paths (flag != 0) arrive here with a previous iterate or
	// an initial guess loaded from a previous solution, so returning without
	// clearing V would hand that guess back as though it had been solved for.
	if (Dot(b, b) == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
		return 1;
	}

	if (flag == 0) {
		for (i = 0; i < n; i++) V[i] = 0;
	}

restart:
	// Residual of the current iterate. V is the QMR-smoothed iterate, so a
	// restart here is a genuine fresh start from the best point we have,
	// not a continuation of the Lanczos sequence that just broke.
	MultA(V, R);
	for (i = 0; i < n; i++) R[i] = b[i] - R[i];

	if (nudge) {
		nudge = false;
		MultPC(R, Z);
		MultA(Z, U);
		double az = Dot(U, U);
		if (az > 0) {
			double al = Dot(U, R) / az;
			for (i = 0; i < n; i++) V[i] += al * Z[i];
			MultA(V, R);
			for (i = 0; i < n; i++) R[i] = b[i] - R[i];
		}
	}

	for (i = 0; i < n; i++) U[i] = 0;
	MultPC(R, P);
	tau = Dot(P, P);
	res = Dot(P, R);
	tta = 0;
	rr = Dot(R, R);
	zz = tau;
	resBest = fabs(res);
	bBail = 0;

	// A warm start can already be the answer.
	if (rr == 0) return 1;
	if (sqrt(tau / tau_b) <= Precision) return 1;

	// The start itself may be isotropic: that is the warm-start case.
	if (zz == 0 || fabs(res) < kappaMin * sqrt(rr * zz)) goto recover;

	do {
		MultA(P, Z);
		pAp = Dot(P, Z);
		del = res / pAp;

		for (i = 0; i < n; i++) R[i] -= (del * Z[i]);

		MultPC(R, Z);
		res_new = Dot(Z, R);
		rho = res_new / res;
		res = res_new;
		zz = Dot(Z, Z);
		tta_new = zz / tau;
		cn = 1. / sqrt(1. + tta_new * tta_new);
		tau *= (tta_new * cn);

		for (i = 0; i < n; i++) {
			U[i] = cn * cn * (tta * tta * U[i] + del * P[i]);
			V[i] += U[i];
			P[i] = Z[i] + (rho * P[i]);
		}
		tta = tta_new;

		rr = Dot(R, R);
		er = sqrt(tau / tau_b);

		if (!std::isfinite(er) || !std::isfinite(res) || (tau == 0 && rr > 0)) goto recover;
		if (rr > 0 && (zz == 0 || fabs(res) < kappaMin * sqrt(rr * zz))) goto recover;
		if (fabs(res) < resBest) { resBest = fabs(res); bBail = 0; }
		else if (++bBail >= stallLimit) goto recover;

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

recover:
	if (nudges < kMaxNudge) {
		nudges++;
		nudge = true;
		goto restart;
	}
	if (!fromZero) {
		fromZero = true;
		nudges = 0;
		for (i = 0; i < n; i++) V[i] = 0;
		prg1 = 0;
		TheView->m_prog1.SetPos(0);
		TheView->InvalidateRect(NULL, FALSE);
		TheView->UpdateWindow();
		goto restart;
	}
	return 0;
}
