// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// spars.cpp — Real-valued sparse symmetric matrix
//
// Assembly uses upper-triangle storage. Before solving, Finalize()
// expands to full storage for parallel SpMV and partitions rows into
// blocks for block-Jacobi parallel SSOR.
//
// OpenMP parallelism:
//   MultA  — parallel over rows (full storage, no atomics, no races)
//   MultPC — block-Jacobi SSOR (independent SSOR within each block, blocks in parallel)
//   Dot    — parallel reduction
//   AXPY   — parallel vector operations

#include "StdAfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#ifdef _OPENMP
#include <omp.h>
#endif

#define KLUDGE

CBigLinProb::CBigLinProb()
{
	n=0;
	V=P=R=U=DI=Z=b=NULL;
	Q=NULL;
	Precision=1.e-08;
	finalized=false;
	numBlocks=0;
}

CBigLinProb::~CBigLinProb()
{
	if (n==0) return;
	free(b); free(P); free(R);
	free(V); free(U); free(Z);
	free(DI); if(Q) free(Q);
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
	DI=(double *)calloc(d,sizeof(double));
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
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x = v; return; }
	}
	SEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const SEntry& a, const SEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
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
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x += v; return; }
	}
	SEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const SEntry& a, const SEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
}

// Expand to full storage and partition into blocks for parallel SSOR.
void CBigLinProb::Finalize()
{
	// --- Build full storage ---
	Mfull.resize(n);
	for(int i=0;i<n;i++) Mfull[i].clear();

	for(int i=0;i<n;i++){
		for(auto& e : M[i]){
			Mfull[i].push_back({e.c, e.x});
			if(e.c != i)
				Mfull[e.c].push_back({i, e.x});
		}
	}

	for(int i=0;i<n;i++)
		std::sort(Mfull[i].begin(), Mfull[i].end(),
			[](const SEntry& a, const SEntry& b){ return a.c < b.c; });

	// --- Block partitioning ---
#ifdef _OPENMP
	numBlocks = omp_get_max_threads();
#else
	numBlocks = 1;
#endif
	if(numBlocks > n) numBlocks = n;
	if(numBlocks < 1) numBlocks = 1;

	blockStart.resize(numBlocks + 1);
	for(int b=0;b<=numBlocks;b++)
		blockStart[b] = (int)((long long)n * b / numBlocks);

	finalized = true;
}

// Parallel SpMV using full storage — no atomics, no races.
void CBigLinProb::MultA(double *X, double *Y)
{
	if(!finalized) Finalize();

	#pragma omp parallel for schedule(static)
	for(int i=0;i<n;i++){
		double sum = 0;
		for(auto& e : Mfull[i])
			sum += e.x * X[e.c];
		Y[i] = sum;
	}
}

double CBigLinProb::Dot(double *X, double *Y)
{
	double z=0;
	#pragma omp parallel for reduction(+:z) schedule(static)
	for(int i=0;i<n;i++) z += X[i]*Y[i];
	return z;
}

void CBigLinProb::BuildDI()
{
	if(!finalized) Finalize();

	for(int i=0;i<n;i++)
	{
		double diag = Get(i,i);
		if(diag != 0){
			DI[i] = diag;
		} else {
			DI[i] = 0;
			for(auto& e : Mfull[i]) DI[i] += fabs(e.x);
		}
		if(DI[i] == 0) DI[i] = 1;
	}
}

// Block-Jacobi parallel SSOR preconditioner.
// Each block runs a full sequential SSOR sweep on its contiguous row range.
// Off-block entries are treated as known (from input X). Blocks run in parallel.
// Within each block, the sweep follows the natural Cuthill-McKee ordering,
// preserving the quality of sequential SSOR.
void CBigLinProb::MultPC(double *X, double *Y)
{
	if(!finalized) Finalize();

	double c = LAMBDA*(2.-LAMBDA);

	#pragma omp parallel for schedule(static)
	for(int i=0;i<n;i++) Y[i] = X[i]*c;

	// Block-Jacobi: each block does independent SSOR using upper-triangle
	// storage (M) with push-style updates, bounded to the block range.
	#pragma omp parallel for schedule(static)
	for(int blk=0;blk<numBlocks;blk++)
	{
		int lo = blockStart[blk];
		int hi = blockStart[blk+1];

		// Forward sweep: push to upper-triangle neighbors within block
		for(int i=lo;i<hi;i++){
			Y[i] /= DI[i];
			for(auto& e : M[i]){
				if(e.c > i && e.c < hi)
					Y[e.c] -= e.x * Y[i] * LAMBDA;
			}
		}

		// Diagonal scaling
		for(int i=lo;i<hi;i++) Y[i] *= DI[i];

		// Backward sweep: pull from upper-triangle neighbors within block
		for(int i=hi-1;i>=lo;i--){
			for(auto& e : M[i]){
				if(e.c > i && e.c < hi)
					Y[i] -= e.x * Y[e.c] * LAMBDA;
			}
			Y[i] /= DI[i];
		}
	}
}

void CBigLinProb::SetValue(int i, double x)
{
	int k,fst,lst;
	double z;

	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=i+bdw; if(lst>n) lst=n; }

	for(k=fst;k<lst;k++){
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
	#pragma omp parallel for schedule(static)
	for(int i=0;i<n;i++){
		b[i]=0.;
		for(auto& e : M[i])
			e.x = 0;
	}
	finalized = false;
}

void CBigLinProb::AntiPeriodicity(int i, int j)
{
	int k,fst,lst;
	double v1,v2,c;

#ifdef KLUDGE
	int tmpbdw=bdw;
	bdw=0;
#endif

	if (j<i) {k=j;j=i;i=k;}

	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=j+bdw; if(lst>n) lst=n; }

	for(k=fst;k<lst;k++){
		if((k!=i) && (k!=j)){
			v1=Get(k,i); v2=Get(k,j);
			if ((v1!=0) || (v2!=0)){
				c=(v1-v2)/2.;
				Put(c,k,i); Put(-c,k,j);
			}
		}
		if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
	}
	c=0.5*(Get(i,i)+Get(j,j));
	Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]-b[j]);
	b[i]=c; b[j]=-c;

#ifdef KLUDGE
	bdw=tmpbdw;
#endif
}

void CBigLinProb::Periodicity(int i, int j)
{
	int k,fst,lst;
	double v1,v2,c;

#ifdef KLUDGE
	int tmpbdw=bdw;
	bdw=0;
#endif

	if (j<i) {k=j;j=i;i=k;}

	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=j+bdw; if(lst>n) lst=n; }

	for(k=fst;k<lst;k++){
		if((k!=i) && (k!=j)){
			v1=Get(k,i); v2=Get(k,j);
			if ((v1!=0) || (v2!=0)){
				c=(v1+v2)/2.;
				Put(c,k,i); Put(c,k,j);
			}
		}
		if((k==i+bdw) && (k<j-bdw) && (bdw!=0)) k=j-bdw;
	}
	c=(Get(i,i)+Get(j,j))/2.;
	Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]+b[j]);
	b[i]=c; b[j]=c;

#ifdef KLUDGE
	bdw=tmpbdw;
#endif
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
	if ((fp=fopen(myFile,"wt"))==NULL) return 0;
	for(int i=0;i<n;i++){
		for(auto& e : M[i]){
			fprintf(fp,"%i %i %.15g\n",i+1,e.c+1,e.x);
			fprintf(fp,"%i %i %.15g\n",e.c+1,i+1,e.x);
		}
	}
	fclose(fp);
	return 1;
}

// Preconditioned Conjugate Gradient solver with parallel SpMV, dots,
// and block-Jacobi SSOR preconditioning.
int CBigLinProb::PCGSolve(int flag)
{
	int i;
	double res, res_o, res_new;
	double er, del, rho, pAp;

	TheView->SetDlgItemText(IDC_FRAME1, "Conjugate Gradient Solver");
	TheView->m_prog1.SetPos(0);
	int prg1 = 0, prg2;

	if(!finalized) Finalize();
	BuildDI();

	// residual with V=0
	MultPC(b, Z);
	res_o = Dot(Z, b);
	if (res_o == 0) return 1;

	// if flag is false, initialize V with zeros
	if (flag == 0) {
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) V[i] = 0;
	}

	// form residual
	MultA(V, R);
	#pragma omp parallel for schedule(static)
	for (i = 0; i < n; i++) R[i] = b[i] - R[i];

	// form initial search direction
	MultPC(R, Z);
	#pragma omp parallel for schedule(static)
	for (i = 0; i < n; i++) P[i] = Z[i];
	res = Dot(Z, R);

	// do iteration
	do {
		// step i) compute A*P and step length
		MultA(P, U);
		pAp = Dot(P, U);
		del = res / pAp;

		// step ii) update solution
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) V[i] += (del * P[i]);

		// step iii) update residual
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) R[i] -= (del * U[i]);

		// step iv) apply preconditioner and compute new search direction
		MultPC(R, Z);
		res_new = Dot(Z, R);
		rho = res_new / res;
		res = res_new;

		// step v) update search direction
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) P[i] = Z[i] + (rho * P[i]);

		// have we converged yet?
		er = sqrt(fabs(res / res_o));

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

// QMR solver with parallel SpMV, dots, and block-Jacobi SSOR
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
	BuildDI();

	MultPC(b, Z);
	res_o = Dot(Z, b);
	if (res_o == 0) return 1;

	if (flag == 0) {
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) V[i] = 0;
	}

	MultA(V, R);

	#pragma omp parallel for schedule(static)
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

		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) R[i] -= (del * Z[i]);

		MultPC(R, Z);
		res_new = Dot(Z, R);
		rho = res_new / res;
		res = res_new;
		tta_new = Dot(Z, Z) / tau;
		cn = 1. / sqrt(1. + tta_new * tta_new);
		tau *= (tta_new * cn);

		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) {
			U[i] = cn * cn * (tta * tta * U[i] + del * P[i]);
			V[i] += U[i];
			P[i] = Z[i] + (rho * P[i]);
		}
		tta=tta_new;

		er = sqrt(fabs(res / res_o));

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
