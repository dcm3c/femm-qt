// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// cspars.cpp — Complex sparse matrix with parallel SpMV and block-Jacobi SSOR
//
// Main matrix M: upper-triangle during assembly, expanded to full storage
// by Finalize() for parallel SpMV and block-Jacobi SSOR.
// N-R auxiliary matrices (Mh, Ma, Ms): upper-triangle only.

#include "StdAfx.h"
#include <math.h>
#include <stdio.h>
#ifdef _OPENMP
#include <omp.h>
#endif
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
	numBlocks=0;
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

	auto& mat = GetMatrix(k);
	auto& row = mat[p];

	for(auto& e : row){
		if(e.c == q){ e.x = v; return; }
	}

	CSEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const CSEntry& a, const CSEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	if(k==0) finalized = false;
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
	auto& row = M[p];
	for(auto& e : row){
		if(e.c == q){ e.x += v; return; }
	}
	CSEntry ne = {q, v};
	auto it = std::lower_bound(row.begin(), row.end(), ne,
		[](const CSEntry& a, const CSEntry& b){ return a.c < b.c; });
	row.insert(it, ne);
	finalized = false;
}

void CBigComplexLinProb::Finalize()
{
	Mfull.resize(n);
	for(int i=0;i<n;i++) Mfull[i].clear();

	for(int i=0;i<n;i++){
		for(auto& e : M[i]){
			Mfull[i].push_back({e.c, e.x});
			if(e.c != i)
				Mfull[e.c].push_back({i, e.x}); // complex-symmetric
		}
	}

	for(int i=0;i<n;i++)
		std::sort(Mfull[i].begin(), Mfull[i].end(),
			[](const CSEntry& a, const CSEntry& b){ return a.c < b.c; });

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

void CBigComplexLinProb::MultA(CComplex *X, CComplex *Y, int k)
{
	int i;

	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Y[i]=0;

	if ((!bNewton) && (k!=0)) k=0;

	if ((bNewton) && (k==-1)){
		MultA(X,Y,0);
		MultA(X,uu,1);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultConjA(X,uu,2);
		MultA(X,vv,3);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])+vv[i];
		return;
	}
	if ((bNewton) && (k==-2)){
		MultA(X,Y,0);
		MultConjA(X,uu,1);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		MultA(X,uu,2);
		MultConjA(X,vv,3);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) Y[i]=Y[i]+uu[i];
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) Y[i]=Y[i]+conj(uu[i])-vv[i];
		return;
	}

	if(k==0){
		if(!finalized) Finalize();
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++){
			CComplex sum(0,0);
			for(auto& e : Mfull[i])
				sum += e.x * X[e.c];
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

	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Y[i]=0;

	if ((k!=0) && (!bNewton)) k=0;

	if(k==0){
		if(!finalized) Finalize();
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++){
			CComplex sum(0,0);
			for(auto& e : Mfull[i])
				sum += e.x.Conj() * X[e.c];
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
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
	MultPC(Y,Z);
	MultA(Z,Y);
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Y[i].im=-Y[i].im;
}

CComplex CBigComplexLinProb::Dot(CComplex *x, CComplex *y)
{
	double zr=0, zi=0;
	#pragma omp parallel for reduction(+:zr,zi) schedule(static)
	for(int i=0;i<n;i++){
		CComplex p = x[i]*y[i];
		zr += p.re; zi += p.im;
	}
	return CComplex(zr, zi);
}

CComplex CBigComplexLinProb::ConjDot(CComplex *x, CComplex *y)
{
	double zr=0, zi=0;
	#pragma omp parallel for reduction(+:zr,zi) schedule(static)
	for(int i=0;i<n;i++){
		CComplex p = x[i].Conj()*y[i];
		zr += p.re; zi += p.im;
	}
	return CComplex(zr, zi);
}

// Block-Jacobi parallel SSOR on main matrix M
void CBigComplexLinProb::MultPC(CComplex *X, CComplex *Y)
{
	int i;

	if(!finalized) Finalize();

	CComplex c = LAMBDA*(2.-LAMBDA);

	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Y[i]=X[i]*c;

	// Block-Jacobi: each block does independent SSOR using upper-triangle
	// storage (M) with push-style updates, bounded to the block range.
	// This avoids iterating over Mfull's 2x entries in the inner loop.
	#pragma omp parallel for schedule(static)
	for(int blk=0;blk<numBlocks;blk++)
	{
		int lo = blockStart[blk];
		int hi = blockStart[blk+1];

		// Forward sweep: process rows lo..hi-1, push to upper-triangle neighbors within block
		for(int ii=lo;ii<hi;ii++){
			Y[ii] /= M[ii][0].x;  // diagonal
			for(auto& e : M[ii]){
				if(e.c > ii && e.c < hi)  // upper triangle, within block
					Y[e.c] -= e.x * Y[ii] * LAMBDA;
			}
		}

		// Diagonal scaling
		for(int ii=lo;ii<hi;ii++) Y[ii] *= M[ii][0].x;

		// Backward sweep: process rows hi-1..lo, pull from upper-triangle neighbors within block
		for(int ii=hi-1;ii>=lo;ii--){
			for(auto& e : M[ii]){
				if(e.c > ii && e.c < hi)
					Y[ii] -= e.x * Y[e.c] * LAMBDA;
			}
			Y[ii] /= M[ii][0].x;
		}
	}
}

void CBigComplexLinProb::SetValue(int i, CComplex x)
{
	int k,fst,lst;
	CComplex z;

	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=i+bdw; if(lst>NumNodes) lst=NumNodes; }

	for(k=fst;k<n;k++){
		if (k==lst) k=NumNodes;
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
	}
	b[i]=Get(i,i)*x;
}

void CBigComplexLinProb::Wipe()
{
	int i;
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++){
		b[i]=0;
		for(auto& e : M[i]) e.x=0;
	}
	finalized = false;

	if (!bNewton) return;
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) for(auto& e : Mh[i]) e.x=0;
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) for(auto& e : Ma[i]) e.x=0;
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) for(auto& e : Ms[i]) e.x=0;
}

void CBigComplexLinProb::AntiPeriodicity(int i, int j)
{
	int k,fst,lst,h; CComplex v1,v2,c;
#ifdef KLUDGE
	int tmpbdw=bdw; bdw=0;
#endif
	if (j<i) {k=j;j=i;i=k;}
	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=j+bdw; if(lst>NumNodes-1) lst=NumNodes-1; }

	for(k=fst;k<n;k++){
		if((k!=i)&&(k!=j)){
			v1=Get(k,i); v2=Get(k,j);
			if((v1!=0)||(v2!=0)){ c=(v1-v2)/2.; Put(c,k,i); Put(-c,k,j); }
		}
		if((k==i+bdw)&&(k<j-bdw)&&(bdw!=0)) k=j-bdw;
		else if(k==lst) k=NumNodes;
	}
	c=0.5*(Get(i,i)+Get(j,j)); Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]-b[j]); b[i]=c; b[j]=-c;

	if(bNewton) for(h=1;h<=3;h++){
		for(k=fst;k<n;k++){
			if((k!=i)&&(k!=j)){
				v1=Get(k,i,h); v2=Get(k,j,h);
				if((v1!=0)||(v2!=0)){ c=(v1-v2)/2.; Put(c,k,i,h); Put(-c,k,j,h); }
			}
			if((k==i+bdw)&&(k<j-bdw)&&(bdw!=0)) k=j-bdw;
			else if(k==lst) k=NumNodes;
		}
		c=(Get(i,i,h)-Get(i,j,h)-Get(j,i,h)+Get(j,j,h))/4.;
		Put(c,i,i,h); Put(-c,i,j,h); Put(c,j,j,h);
	}
#ifdef KLUDGE
	bdw=tmpbdw;
#endif
}

void CBigComplexLinProb::Periodicity(int i, int j)
{
	int k,fst,lst,h; CComplex v1,v2,c;
#ifdef KLUDGE
	int tmpbdw=bdw; bdw=0;
#endif
	if (j<i) {k=j;j=i;i=k;}
	if(bdw==0){ fst=0; lst=n; }
	else { fst=i-bdw; if(fst<0) fst=0; lst=j+bdw; if(lst>NumNodes-1) lst=NumNodes-1; }

	for(k=fst;k<n;k++){
		if((k!=i)&&(k!=j)){
			v1=Get(k,i); v2=Get(k,j);
			if((v1!=0)||(v2!=0)){ c=(v1+v2)/2.; Put(c,k,i); Put(c,k,j); }
		}
		if((k==i+bdw)&&(k<j-bdw)&&(bdw!=0)) k=j-bdw;
		else if(k==lst) k=NumNodes;
	}
	c=(Get(i,i)+Get(j,j))/2.; Put(c,i,i); Put(c,j,j);
	c=0.5*(b[i]+b[j]); b[i]=c; b[j]=c;

	if(bNewton) for(h=1;h<=3;h++){
		for(k=fst;k<n;k++){
			if((k!=i)&&(k!=j)){
				v1=Get(k,i,h); v2=Get(k,j,h);
				if((v1!=0)||(v2!=0)){ c=(v1+v2)/2.; Put(c,k,i,h); Put(c,k,j,h); }
			}
			if((k==i+bdw)&&(k<j-bdw)&&(bdw!=0)) k=j-bdw;
			else if(k==lst) k=NumNodes;
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
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) Z[i].im=-Z[i].im;
	MultPC(Z,P);
	MultA(P,Z);
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) P[i]=Z[i].Conj();
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) V[i]=0;

	MultAPPA(V,R);
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) R[i]=P[i]-R[i];
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) P[i]=R[i];
	res=ConjDot(R,R);

	for(k=0;k<3;k++){
		MultAPPA(P,U);
		pAp=ConjDot(P,U);
		del=res/pAp;
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) V[i]+=(del*P[i]);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) R[i]-=(del*U[i]);
		res_new=ConjDot(R,R);
		rho=res_new/res;
		res=res_new;
		#pragma omp parallel for schedule(static)
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
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) V[i]=0;
	}

	MultA(V,R);
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) R[i]=b[i]-R[i];
	normb=nrm(b);

	// A problem with no sources has a zero RHS, and its exact solution is zero.
	// That is a converged answer, not an error, so return before normalizing by
	// normb: er would be 0/0 = NaN, the do-loop below still runs its body once,
	// and del = res/pAp = 0/0 would fill V with NaN. (The real-valued solvers in
	// spars.cpp guard this already, via res_o == 0.)
	if(normb==0){
		#pragma omp parallel for schedule(static)
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
	#pragma omp parallel for schedule(static)
	for(i=0;i<n;i++) P[i]=Z[i];
	res=Dot(Z,R);

	do{
		MultA(P,U);
		pAp=Dot(P,U);
		del=res/pAp;
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) V[i]+=(del*P[i]);
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) R[i]-=(del*U[i]);
		MultPC(R,Z);
		res_new=Dot(Z,R);
		rho=res_new/res;
		res=res_new;
		#pragma omp parallel for schedule(static)
		for(i=0;i<n;i++) P[i]=Z[i]+(rho*P[i]);

		er=nrm(R)/normb;
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
		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++)
			b[i] = borig[i] - P[i] - conj(U[i]) - R[i];

		PBCGSolve(TRUE);

		#pragma omp parallel for schedule(static)
		for (i = 0; i < n; i++) P[i] = V[i] - v[i];
		MultA(P, U, -1);
		c = Re(ConjDot(r, U)) / Re(ConjDot(U, U));

		#pragma omp parallel for schedule(static)
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
