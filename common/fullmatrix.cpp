// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// CFullMatrix::Eig (general complex eigen-decomposition) is ported, translated
// to CComplex, from melihaltun/complexEigenPairSolver:
//
//     MIT License — Copyright (c) 2018 melihaltun
//     https://github.com/melihaltun/complexEigenPairSolver
//
//     Permission is hereby granted, free of charge, to any person obtaining a
//     copy of this software and associated documentation files (the
//     "Software"), to deal in the Software without restriction, including
//     without limitation the rights to use, copy, modify, merge, publish,
//     distribute, sublicense, and/or sell copies of the Software, and to permit
//     persons to whom the Software is furnished to do so, subject to the
//     following conditions:  The above copyright notice and this permission
//     notice shall be included in all copies or substantial portions of the
//     Software.  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
//
// The algorithm (Hessenberg reduction -> Francis implicit double-shift QR ->
// inverse-iteration / back-substitution for eigenvectors) is unchanged; only the
// scalar type (their `complex` -> CComplex) and its operations were rewritten.

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include "complex.h"
#include "fullmatrix.h"

CFullMatrix::CFullMatrix()
{
	n=0;
	M=NULL;
	b=NULL;
}

CFullMatrix::CFullMatrix(int d)
{
	n=0;
	M=NULL;
	b=NULL;
	Create(d);
}

CFullMatrix::~CFullMatrix()
{
	if(n==0) return;

	int i;
	for(i=0;i<n;i++) free(M[i]);
	free(M);
	free(b);
	n=0;
}

void CFullMatrix::Wipe()
{
	/* fills up a square matrix with zeros in every entry */
	int i,j;

	for(i=0;i<n;i++){
		b[i]=0;
		for(j=0;j<n;j++)
			M[i][j]=0;
	}
}


int CFullMatrix::Create(int d)
{
	int i;

    M=(CComplex **)calloc(d,sizeof(CComplex *));
	b=(CComplex *)calloc(d,sizeof(CComplex));
	if ((M==NULL) || (b==NULL)) return 0;
	for(i=0;i<d;i++){
		M[i]=(CComplex *)calloc(d,sizeof(CComplex));
		if (M[i]==NULL) return 0;
	}
	n=d;
	return 1;
}

int CFullMatrix::GaussSolve()
{
	/* solves the linear system m x = b for x.  The result is returned
	   in b, m is destroyed in the process */

        int i,j,k,q;
        CComplex *z;
        CComplex max,f;

        for(i=0;i<n;i++)
        {
			q=i;  /* default pivot — search below may override */
			for(j=i,max=0;j<n;j++)
				if (abs(M[j][i])>abs(max))
				{
					max=M[j][i];
					q=j;
				}
			if(max==0) return 0;
			z=M[i];M[i]=M[q];M[q]=z;
			f=b[i];b[i]=b[q];b[q]=f;
			for(j=i+1;j<n;j++){
				f=M[j][i]/M[i][i];
                b[j]=b[j]-f*b[i];
                for (k=i;k<n;k++)
					M[j][k]-=(f*M[i][k]);
			}
		}

		for(i=n-1;i>=0;i--)
        {
			for(j=n-1,f=0;j>i;j--)
				f+=M[i][j]*b[j];
			b[i]=(b[i]-f)/M[i][i];
		}
        return 1;
}

// ===========================================================================
// Eigen-decomposition for general complex matrices.
// Ported (translated to CComplex) from melihaltun/complexEigenPairSolver (MIT);
// see the license notice at the top of this file.
// ===========================================================================

namespace {

#define lin_index(i, j, numCol)  ( ((i)*(numCol))+(j) )   // 2D -> 1D, row-major
#define EIG_EPS 2.221E-12

// scalar helpers that CComplex's own operators/functions don't cover directly
static inline CComplex cExpj(double a)            { return CComplex(cos(a), sin(a)); }            // exp(i a)
static inline CComplex polar2z(double r, double t){ return CComplex(r * cos(t), r * sin(t)); }
static inline CComplex cSqrtP(const CComplex &z)  { double r = abs(z), t = arg(z); return CComplex(sqrt(r) * cos(t / 2.0), sqrt(r) * sin(t / 2.0)); }

// ---- flat-array (row-major) matrix helpers --------------------------------

static void copyComplexMatrix(CComplex in[], unsigned N, unsigned M, CComplex out[]) {
    for (unsigned i = 0; i < N; i++) for (unsigned j = 0; j < M; j++) out[lin_index(i, j, M)] = in[lin_index(i, j, M)];
}
static void hermitian(CComplex in[], unsigned N, unsigned M, CComplex out[]) {
    for (unsigned i = 0; i < N; i++) for (unsigned j = 0; j < M; j++) out[lin_index(j, i, N)] = conj(in[lin_index(i, j, M)]);
}
static void complexIdentity(CComplex matrix[], int size) {
    std::fill_n(matrix, (size_t)size * size, CComplex(0.0, 0.0));
    for (int i = 0; i < size; i++) matrix[lin_index(i, i, size)] = CComplex(1, 0);
}
static double absMaxElement(CComplex in[], unsigned N, unsigned M) {
    double mx = abs(in[0]);
    for (unsigned i = 1; i < M * N; i++) { double a = abs(in[i]); if (mx < a) mx = a; }
    return mx;
}
static double complexVectorNorm(CComplex V[], unsigned N) {
    double s = 0; for (unsigned i = 0; i < N; i++) s += abs(V[i]) * abs(V[i]); return sqrt(s);
}
static void complexVectorSubtraction(CComplex V1[], CComplex V2[], unsigned N, CComplex U[]) {
    for (unsigned i = 0; i < N; i++) U[i] = V1[i] - V2[i];
}
static void complexScalarMultiplication(CComplex V[], CComplex alpha, unsigned N, unsigned M, CComplex U[]) {
    for (unsigned i = 0; i < N * M; i++) U[i] = alpha * V[i];
}
static void complexMatrixMultiplication(CComplex M1[], CComplex M2[], unsigned N, unsigned K, unsigned M, CComplex Mout[]) {
    std::fill_n(Mout, (size_t)N * M, CComplex(0.0, 0.0));
    for (unsigned i = 0; i < N; i++) for (unsigned j = 0; j < M; j++) for (unsigned k = 0; k < K; k++)
        Mout[i * M + j] = Mout[i * M + j] + M1[i * K + k] * M2[k * M + j];
}
static void complexMatrixSubtraction(CComplex M1[], CComplex M2[], unsigned N, unsigned M, CComplex Mout[]) {
    for (unsigned i = 0; i < N; i++) for (unsigned j = 0; j < M; j++) Mout[i * M + j] = M1[i * M + j] - M2[i * M + j];
}
static void complexMatrixConjungate(CComplex in[], unsigned N, unsigned M, CComplex out[]) {
    for (unsigned i = 0; i < N * M; i++) out[i] = conj(in[i]);
}
static void complexDiagonal(CComplex M[], unsigned N, CComplex D[]) {
    for (unsigned i = 0; i < N; i++) D[i] = M[lin_index(i, i, N)];
}

// ---- Hessenberg reduction --------------------------------------------------

static void hessenberg(CComplex A[], unsigned N, CComplex H[], CComplex U[]) {
    double absMax_A, tolr, norm_x_alpha_e1;
    int MSD, k, j, i;
    CComplex *x, *e1, *u, *u_conj, *x_alpha_e1, *P, *u_conj_u, *Ph, *Ph_H, *Ph_H_P, *U_P;
    CComplex ro, alpha;

    copyComplexMatrix(A, N, N, H);
    absMax_A = absMaxElement(A, N, N);
    MSD = (int)ceil(log10(absMax_A));
    tolr = EIG_EPS * pow(10.0, (double)(MSD + 1));
    complexIdentity(U, N);

    x = (CComplex*)malloc((N - 1) * sizeof(CComplex));
    e1 = (CComplex*)malloc((N - 1) * sizeof(CComplex));
    u = (CComplex*)malloc((N - 1) * sizeof(CComplex));
    u_conj = (CComplex*)malloc((N - 1) * sizeof(CComplex));
    x_alpha_e1 = (CComplex*)malloc((N - 1) * sizeof(CComplex));
    P = (CComplex*)malloc(N * N * sizeof(CComplex));
    u_conj_u = (CComplex*)malloc((N - 1) * (N - 1) * sizeof(CComplex));
    Ph = (CComplex*)malloc(N * N * sizeof(CComplex));
    Ph_H = (CComplex*)malloc(N * N * sizeof(CComplex));
    Ph_H_P = (CComplex*)malloc(N * N * sizeof(CComplex));
    U_P = (CComplex*)malloc(N * N * sizeof(CComplex));

    for (k = 0; k < (int)N - 2; k++) {
        for (j = k + 1; j < (int)N; j++)
            x[j - k - 1] = H[lin_index(j, k, N)];

        if (abs(x[0]) < tolr)
            ro = CComplex(-1, 0);
        else
            ro = -cExpj(arg(x[0]));   // ro = -(exp(j*atan2(imag(x(1)),real(x(1)))))

        alpha = ro * CComplex(complexVectorNorm(x, N - 1 - k), 0);

        std::fill_n(e1, (N - k - 1), CComplex(0.0, 0.0));
        e1[0] = CComplex(1, 0);

        complexScalarMultiplication(e1, alpha, N - k - 1, 1, x_alpha_e1);
        complexVectorSubtraction(x, x_alpha_e1, N - k - 1, x_alpha_e1);
        norm_x_alpha_e1 = complexVectorNorm(x_alpha_e1, N - k - 1);
        complexScalarMultiplication(x_alpha_e1, CComplex(1 / norm_x_alpha_e1, 0), N - k - 1, 1, u);

        complexIdentity(P, N);

        complexMatrixConjungate(u, N - k - 1, 1, u_conj);
        complexMatrixMultiplication(u, u_conj, N - k - 1, 1, N - k - 1, u_conj_u);
        complexScalarMultiplication(u_conj_u, CComplex(2, 0), N - k - 1, N - k - 1, u_conj_u);
        for (i = k + 1; i < (int)N; i++)
            for (j = k + 1; j < (int)N; j++)
                P[lin_index(i, j, N)] = P[lin_index(i, j, N)] - u_conj_u[lin_index(i - k - 1, j - k - 1, N - k - 1)];

        hermitian(P, N, N, Ph);
        complexMatrixMultiplication(Ph, H, N, N, N, Ph_H);
        complexMatrixMultiplication(Ph_H, P, N, N, N, Ph_H_P);
        memcpy(H, Ph_H_P, N * N * sizeof(CComplex));

        complexMatrixMultiplication(U, P, N, N, N, U_P);
        memcpy(U, U_P, N * N * sizeof(CComplex));
    }

    free(x); free(e1); free(u); free(x_alpha_e1); free(P);
    free(u_conj); free(u_conj_u); free(Ph); free(Ph_H); free(Ph_H_P); free(U_P);
}

// ---- Householder reflector + Francis double-shift QR -----------------------

static void householder(CComplex x[], unsigned n, CComplex u[]) {
    double mx, xc, sigma, s;
    CComplex *x2 = (CComplex*)malloc(n * sizeof(CComplex));
    mx = abs(x[0]);
    for (unsigned i = 1; i < n; i++) { xc = abs(x[i]); if (mx < xc) mx = xc; }
    for (unsigned i = 0; i < n; i++) x2[i] = x[i] / CComplex(mx, 0.0);
    s = copysign(1.0, x2[0].re);
    sigma = s * complexVectorNorm(x2, n);
    u[0] = x2[0] + CComplex(sigma, 0.0);
    for (unsigned i = 1; i < n; i++) u[i] = x2[i];
    free(x2);
}

static void doubleShiftInnerLoop(CComplex H[], unsigned N, unsigned p, CComplex NH[], CComplex Q[]) {
    CComplex t, d, xyz[3], u3[3], c3[3 * 3], I3[3 * 3], u3_u3t[3 * 3], ut_u, u3t[3], I2[2 * 2], u2[2], u2t[2], u2_u2t[2 * 2], c2[2 * 2];
    CComplex *Pk, *Pkt, *Qnew, *Hnew, *H_Pk;
    unsigned k = 0, j, i, MSD;
    double maxM, absM, tolr;

    Pk = (CComplex*)malloc(N * N * sizeof(CComplex));
    Pkt = (CComplex*)malloc(N * N * sizeof(CComplex));
    Qnew = (CComplex*)malloc(N * N * sizeof(CComplex));
    Hnew = (CComplex*)malloc(N * N * sizeof(CComplex));
    H_Pk = (CComplex*)malloc(N * N * sizeof(CComplex));

    complexIdentity(Q, N);
    memcpy(NH, H, N * N * sizeof(CComplex));

    t = NH[lin_index(p - 2, p - 2, N)] + NH[lin_index(p - 1, p - 1, N)];
    d = NH[lin_index(p - 2, p - 2, N)] * NH[lin_index(p - 1, p - 1, N)] - NH[lin_index(p - 1, p - 2, N)] * NH[lin_index(p - 2, p - 1, N)];
    xyz[0] = NH[lin_index(0, 0, N)] * NH[lin_index(0, 0, N)];
    xyz[0] = xyz[0] + NH[lin_index(0, 1, N)] * NH[lin_index(1, 0, N)];
    xyz[0] = xyz[0] - t * NH[lin_index(0, 0, N)];
    xyz[0] = xyz[0] + d;
    xyz[1] = (NH[lin_index(0, 0, N)] + NH[lin_index(1, 1, N)]) - t;
    xyz[1] = NH[lin_index(1, 0, N)] * xyz[1];
    xyz[2] = NH[lin_index(1, 0, N)] * NH[lin_index(2, 1, N)];

    for (k = 0; k <= p - 3; k++) {
        householder(xyz, 3, u3);

        complexIdentity(I3, 3);
        hermitian(u3, 3, 1, u3t);
        complexMatrixMultiplication(u3, u3t, 3, 1, 3, u3_u3t);
        complexMatrixMultiplication(u3t, u3, 1, 3, 1, &ut_u);
        for (j = 0; j < 9; j++) u3_u3t[j] = u3_u3t[j] / ut_u;
        complexScalarMultiplication(u3_u3t, CComplex(2.0, 0.0), 3, 3, u3_u3t);
        complexMatrixSubtraction(I3, u3_u3t, 3, 3, c3);

        complexIdentity(Pk, N);
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                Pk[lin_index(k + i, k + j, N)] = c3[lin_index(i, j, 3)];

        complexMatrixMultiplication(Q, Pk, N, N, N, Qnew);
        copyComplexMatrix(Qnew, N, N, Q);

        maxM = abs(Q[0]);
        for (i = 1; i < N * N; i++) { absM = abs(Q[i]); if (maxM < absM) maxM = absM; }
        MSD = (unsigned)ceil(log10(maxM));
        tolr = EIG_EPS * pow(10.0, (double)MSD);
        for (i = 1; i < N * N; i++)
            if (tolr > fabs(Q[i].im)) { Q[i].re = copysign(abs(Q[i]), Q[i].re); Q[i].im = 0.0; }

        hermitian(Pk, N, N, Pkt);
        complexMatrixMultiplication(NH, Pk, N, N, N, H_Pk);
        complexMatrixMultiplication(Pkt, H_Pk, N, N, N, Hnew);
        copyComplexMatrix(Hnew, N, N, NH);

        maxM = abs(NH[0]);
        for (i = 1; i < N * N; i++) { absM = abs(NH[i]); if (maxM < absM) maxM = absM; }
        MSD = (unsigned)ceil(log10(maxM));
        tolr = EIG_EPS * pow(10.0, (double)MSD);
        for (i = 1; i < N * N; i++)
            if (tolr > fabs(NH[i].im)) { NH[i].re = copysign(abs(NH[i]), NH[i].re); NH[i].im = 0.0; }

        xyz[0] = NH[lin_index(k + 1, k, N)];
        xyz[1] = NH[lin_index(k + 2, k, N)];
        if (k < p - 3) xyz[2] = NH[lin_index(k + 3, k, N)];
    }

    householder(xyz, 2, u2);

    complexIdentity(I2, 2);
    hermitian(u2, 2, 1, u2t);
    complexMatrixMultiplication(u2, u2t, 2, 1, 2, u2_u2t);
    complexMatrixMultiplication(u2t, u2, 1, 2, 1, &ut_u);
    for (j = 0; j < 4; j++) u2_u2t[j] = u2_u2t[j] / ut_u;
    complexScalarMultiplication(u2_u2t, CComplex(2.0, 0.0), 2, 2, u2_u2t);
    complexMatrixSubtraction(I2, u2_u2t, 2, 2, c2);

    complexIdentity(Pk, N);
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            Pk[lin_index(k + i, k + j, N)] = c2[lin_index(i, j, 2)];

    complexMatrixMultiplication(Q, Pk, N, N, N, Qnew);
    copyComplexMatrix(Qnew, N, N, Q);

    maxM = abs(Q[0]);
    for (i = 1; i < N * N; i++) { absM = abs(Q[i]); if (maxM < absM) maxM = absM; }
    MSD = (unsigned)ceil(log10(maxM));
    tolr = EIG_EPS * pow(10.0, (double)MSD);
    for (i = 1; i < N * N; i++) {
        absM = abs(Q[i]);
        if (tolr > absM) Q[i] = CComplex(0.0, 0.0);
        else if (tolr > fabs(Q[i].im)) { Q[i].re = copysign(abs(Q[i]), Q[i].re); Q[i].im = 0.0; }
    }

    hermitian(Pk, N, N, Pkt);
    complexMatrixMultiplication(NH, Pk, N, N, N, H_Pk);
    complexMatrixMultiplication(Pkt, H_Pk, N, N, N, Hnew);
    copyComplexMatrix(Hnew, N, N, NH);

    maxM = abs(NH[0]);
    for (i = 1; i < N * N; i++) { absM = abs(NH[i]); if (maxM < absM) maxM = absM; }
    MSD = (unsigned)ceil(log10(maxM));
    tolr = EIG_EPS * pow(10.0, (double)MSD);
    for (i = 1; i < N * N; i++) {
        absM = abs(NH[i]);
        if (tolr > absM) NH[i] = CComplex(0.0, 0.0);
        else if (tolr > fabs(NH[i].im)) { NH[i].re = copysign(abs(NH[i]), NH[i].re); NH[i].im = 0.0; }
    }

    free(Pk); free(Pkt); free(Qnew); free(Hnew); free(H_Pk);
}

static void doubleShift(CComplex H[], unsigned N, CComplex Q[], CComplex R[]) {
    unsigned p = N, maxIter = 1000, k = 0;
    double tolr = EIG_EPS;
    CComplex *HH, *NH, *QQ, *Qnew;

    HH = (CComplex*)malloc(N * N * sizeof(CComplex));
    NH = (CComplex*)malloc(N * N * sizeof(CComplex));
    QQ = (CComplex*)malloc(N * N * sizeof(CComplex));
    Qnew = (CComplex*)malloc(N * N * sizeof(CComplex));

    copyComplexMatrix(H, N, N, R);
    complexIdentity(Q, N);

    while (p > 2) {
        k++;
        copyComplexMatrix(R, N, N, HH);
        doubleShiftInnerLoop(HH, N, p, NH, QQ);
        copyComplexMatrix(NH, N, N, R);
        complexMatrixMultiplication(Q, QQ, N, N, N, Qnew);
        copyComplexMatrix(Qnew, N, N, Q);

        if (abs(R[lin_index(p - 2, p - 3, N)]) < tolr * (abs(R[lin_index(p - 2, p - 2, N)]) + abs(R[lin_index(p - 3, p - 3, N)]))) {
            R[lin_index(p - 2, p - 3, N)] = CComplex(0, 0);
            p -= 2;
            continue;
        }
        if (abs(R[lin_index(p - 1, p - 2, N)]) < tolr * (abs(R[lin_index(p - 1, p - 1, N)]) + abs(R[lin_index(p - 2, p - 2, N)]))) {
            R[lin_index(p - 1, p - 2, N)] = CComplex(0, 0);
            p--;
        }
        if (k > maxIter) break;
    }
    free(HH); free(QQ); free(NH); free(Qnew);
}

// ---- Givens QR + QR iteration ----------------------------------------------

static void complexGivensRotation(CComplex a, CComplex b, CComplex *c, CComplex *s) {
    double tolr = EIG_EPS, r;
    if (abs(b) < tolr) { *c = CComplex(1.0, 0.0); *s = CComplex(0.0, 0.0); }
    else if (abs(a) < tolr) { *c = CComplex(0.0, 0.0); *s = CComplex(-1.0, 0.0); }
    else {
        r = sqrt(abs(a) * abs(a) + abs(b) * abs(b));
        *c = conj(a) / CComplex(r, 0.0);
        *s = conj(b) / CComplex(r, 0.0);
    }
}

static void qrGivensComplex(CComplex A[], unsigned N, unsigned M, CComplex Q[], CComplex R[]) {
    unsigned i, j;
    CComplex c, s;
    CComplex *G, *Gt, *Rnew, *Qnew;
    G = (CComplex*)malloc(N * N * sizeof(CComplex));
    Gt = (CComplex*)malloc(N * N * sizeof(CComplex));
    Rnew = (CComplex*)malloc(N * M * sizeof(CComplex));
    Qnew = (CComplex*)malloc(N * N * sizeof(CComplex));

    complexIdentity(Q, N);
    copyComplexMatrix(A, N, M, R);

    for (j = 0; j < M; j++) {
        for (i = N - 1; i > j; i--) {
            complexIdentity(G, N);
            complexGivensRotation(R[lin_index((i - 1), j, M)], R[lin_index(i, j, M)], &c, &s);
            G[lin_index((i - 1), (i - 1), N)] = c;
            G[lin_index((i - 1), i, N)] = s;
            G[lin_index(i, (i - 1), N)] = -conj(s);
            G[lin_index(i, i, N)] = conj(c);

            hermitian(G, N, N, Gt);
            complexMatrixMultiplication(G, R, N, N, M, Rnew);
            complexMatrixMultiplication(Q, Gt, N, N, N, Qnew);
            copyComplexMatrix(Rnew, N, M, R);
            copyComplexMatrix(Qnew, N, N, Q);
        }
    }
    free(G); free(Gt); free(Rnew); free(Qnew);
}

static void qrIteration(CComplex A[], unsigned N, CComplex Q[], CComplex R[]) {
    unsigned k, maxIter = 1000;
    double n1, n2, tolr = EIG_EPS;
    CComplex *M, *V, *D, *Vnew;

    M = (CComplex*)malloc(N * N * sizeof(CComplex));
    V = (CComplex*)malloc(N * N * sizeof(CComplex));
    D = (CComplex*)malloc(N * sizeof(CComplex));
    Vnew = (CComplex*)malloc(N * N * sizeof(CComplex));

    memcpy(M, A, N * N * sizeof(CComplex));
    complexIdentity(V, N);
    complexDiagonal(M, N, D);
    n1 = complexVectorNorm(D, N);

    for (k = 0; k < maxIter; k++) {
        qrGivensComplex(M, N, N, Q, R);
        complexMatrixMultiplication(V, Q, N, N, N, Vnew);
        copyComplexMatrix(Vnew, N, N, V);
        complexMatrixMultiplication(R, Q, N, N, N, M);
        complexDiagonal(M, N, D);
        n2 = complexVectorNorm(D, N);
        if (fabs(n1 - n2) < tolr) break;
        n1 = n2;
    }
    copyComplexMatrix(V, N, N, Q);
    copyComplexMatrix(M, N, N, R);
    free(M); free(V); free(Vnew); free(D);
}

// ---- back substitution -----------------------------------------------------

static void backwardsSubstitution(CComplex U[], CComplex y[], unsigned N, CComplex x[]) {
    int MSD, i, j;
    double absMax_U, tolr;
    absMax_U = absMaxElement(U, N, N);
    MSD = (int)ceil(log10(absMax_U));
    tolr = EIG_EPS * pow(10.0, (double)(MSD + 1));
    for (i = (int)N - 1; i >= 0; i--) {
        x[i] = y[i];
        for (j = i + 1; j < (int)N; j++)
            x[i] = x[i] - U[lin_index(i, j, N)] * x[j];
        if (abs(U[lin_index(i, i, N)]) < tolr) { x[i].re = 1; x[i].im = 0; }
        else x[i] = x[i] / U[lin_index(i, i, N)];
    }
}

// ---- 2x2 closed form (used for complex-conjugate eigenpairs) ----------------

static void solve2by2eig(CComplex A[], CComplex V[], CComplex D[]) {
    double tolr = EIG_EPS, r, theta, nV1, nV2, m1, m2, a1, a2;
    CComplex x, y, z, x2, V1[2], V2[2], a, b, c, delta;

    std::fill_n(D, 2, CComplex(0.0, 0.0));
    a = CComplex(1, 0);
    b = -(A[lin_index(0, 0, 2)] + A[lin_index(1, 1, 2)]);
    c = A[lin_index(0, 0, 2)] * A[lin_index(1, 1, 2)] - A[lin_index(0, 1, 2)] * A[lin_index(1, 0, 2)];
    delta = b * b - CComplex(4, 0) * (a * c);

    if (delta.re < 0 && fabs(delta.im) < tolr) {
        D[0].re = -b.re / (2 * a.re); D[0].im = sqrt(-delta.re) / (2 * a.re);
        D[1].re = -b.re / (2 * a.re); D[1].im = -sqrt(-delta.re) / (2 * a.re);
    } else {
        CComplex sqrDelta = cSqrtP(delta);
        CComplex two_a = CComplex(2, 0) * a;
        D[0] = (sqrDelta - b) / two_a;
        D[1] = (-(sqrDelta + b)) / two_a;
    }

    if (abs(A[lin_index(0, 1, 2)]) > tolr) x = (D[0] - A[lin_index(0, 0, 2)]) / A[lin_index(0, 1, 2)];
    else                                   x = D[0] - A[lin_index(0, 0, 2)];
    x2 = x * x;
    z = CComplex(1, 0) + x2;
    if (abs(z) > 10 * tolr) { r = abs(z); theta = arg(z); y = polar2z(sqrt(r), theta / 2); }
    else y = CComplex(1, 0);

    V1[0] = CComplex(1, 0) / y;
    V1[1] = x / y;
    nV1 = complexVectorNorm(V1, 2);
    if (nV1 > tolr) { V1[0] = V1[0] / CComplex(nV1, 0.0); V1[1] = V1[1] / CComplex(nV1, 0.0); }
    if (fabs(V1[0].im) > tolr) {
        m1 = abs(V1[0]); m2 = abs(V1[1]); a1 = arg(V1[0]); a2 = arg(V1[1]);
        V1[0] = CComplex(m1, 0);
        V1[1] = CComplex(m2 * cos(a2 - a1), m2 * sin(a2 - a1));
    }

    if (abs(A[lin_index(0, 1, 2)]) > tolr) x = (D[1] - A[lin_index(0, 0, 2)]) / A[lin_index(0, 1, 2)];
    else                                   x = D[1] - A[lin_index(0, 0, 2)];
    x2 = x * x;
    z = CComplex(1, 0) + x2;
    if (abs(z) > 10 * tolr) { r = abs(z); theta = arg(z); y = polar2z(sqrt(r), theta / 2); }
    else y = CComplex(1, 0);

    V2[0] = CComplex(1, 0) / y;
    V2[1] = x / y;
    nV2 = complexVectorNorm(V2, 2);
    if (nV2 > tolr) { V2[0] = V2[0] / CComplex(nV2, 0.0); V2[1] = V2[1] / CComplex(nV2, 0.0); }
    if (fabs(V2[0].im) > tolr) {
        m1 = abs(V2[0]); m2 = abs(V2[1]); a1 = arg(V2[0]); a2 = arg(V2[1]);
        V2[0] = CComplex(m1 * cos(a1 - a2), m1 * sin(a1 - a2));
        V2[1] = CComplex(m2, 0);
    }

    V[0] = V1[0]; V[1] = V1[1]; V[2] = V2[0]; V[3] = V2[1];
}

// ---- driver: V (eigenvectors, columns), D (eigenvalues) ---------------------

static void eig_impl(CComplex A[], unsigned N, CComplex V[], CComplex D[]) {
    unsigned i, j, k, complexEigenPairCount = 0;
    bool offDiagonals = false;
    double tolr = EIG_EPS, nv, mag1, mag2, ang1, ang2;
    CComplex R2[2 * 2], V2[2 * 2], D2[2];
    CComplex *H, *U, *Q, *R, *R_, *Q_, *W, *RR, *DD, *rRR, *qRR, *y, *v, *Wnew;
    unsigned *complexPairs;

    H = (CComplex*)malloc(N * N * sizeof(CComplex));
    U = (CComplex*)malloc(N * N * sizeof(CComplex));
    Q = (CComplex*)malloc(N * N * sizeof(CComplex));
    R = (CComplex*)malloc(N * N * sizeof(CComplex));
    R_ = (CComplex*)malloc(N * N * sizeof(CComplex));
    Q_ = (CComplex*)malloc(N * N * sizeof(CComplex));
    W = (CComplex*)malloc(N * N * sizeof(CComplex));
    RR = (CComplex*)malloc(N * N * sizeof(CComplex));
    DD = (CComplex*)malloc(N * N * sizeof(CComplex));
    rRR = (CComplex*)malloc(N * N * sizeof(CComplex));
    qRR = (CComplex*)malloc(N * N * sizeof(CComplex));
    y = (CComplex*)calloc(N, sizeof(CComplex));
    v = (CComplex*)malloc(N * sizeof(CComplex));
    Wnew = (CComplex*)malloc(N * N * sizeof(CComplex));
    complexPairs = (unsigned*)calloc(N, sizeof(unsigned));

    hessenberg(A, N, H, U);
    doubleShift(H, N, Q, R);

    for (i = 0; i < N; i++) D[i] = R[lin_index(i, i, N)];

    for (i = 0; i < N - 1; i++) {
        if (abs(R[lin_index(i + 1, i, N)]) > tolr) {
            offDiagonals = true;
            R2[0] = R[lin_index(i, i, N)];
            R2[1] = R[lin_index(i, i + 1, N)];
            R2[2] = R[lin_index(i + 1, i, N)];
            R2[3] = R[lin_index(i + 1, i + 1, N)];
            solve2by2eig(R2, V2, D2);
            D[i] = D2[0];
            D[i + 1] = D2[1];
        }
    }

    i = 0;
    while (i < N) {
        if (fabs(D[i].im) > tolr) {
            complexEigenPairCount++;
            complexPairs[i] = complexEigenPairCount;
            i++;
            if (i < N) complexPairs[i] = complexEigenPairCount;
        }
        i++;
    }

    if (offDiagonals && complexEigenPairCount == 0) {
        qrIteration(R, N, Q_, R_);
        copyComplexMatrix(R_, N, N, R);
    }

    for (i = 0; i < N; i++) {
        complexIdentity(DD, N);
        complexScalarMultiplication(DD, D[i], N, N, DD);
        complexMatrixSubtraction(R, DD, N, N, RR);
        qrGivensComplex(RR, N, N, qRR, rRR);
        backwardsSubstitution(rRR, y, N, v);
        nv = complexVectorNorm(v, N);
        if (nv < tolr) nv = 1;
        complexScalarMultiplication(v, CComplex(1 / nv, 0.0), N, 1, v);
        for (j = 0; j < N; j++) W[lin_index(j, i, N)] = v[j];
    }

    if (offDiagonals && complexEigenPairCount == 0) {
        complexMatrixMultiplication(Q_, W, N, N, N, Wnew);
        copyComplexMatrix(Wnew, N, N, W);
    }
    complexMatrixMultiplication(Q, W, N, N, N, Wnew);
    complexMatrixMultiplication(U, Wnew, N, N, N, V);

    for (i = 0; i < N; i++) {
        if (complexPairs[i]) {
            if (i > 0 && complexPairs[i - 1] == complexPairs[i]) k = i - 1;
            else k = i;
            mag1 = abs(V[lin_index(k, i, N)]);
            ang1 = arg(V[lin_index(k, i, N)]);
            for (j = 0; j < N; j++) {
                if (j == k) V[lin_index(k, i, N)] = CComplex(mag1, 0.0);
                else {
                    mag2 = abs(V[lin_index(j, i, N)]);
                    ang2 = arg(V[lin_index(j, i, N)]);
                    V[lin_index(j, i, N)] = CComplex(mag2 * cos(ang2 - ang1), mag2 * sin(ang2 - ang1));
                }
            }
        }
    }

    free(H); free(U); free(Q); free(R); free(Q_); free(R_); free(W);
    free(DD); free(RR); free(qRR); free(rRR); free(y); free(v); free(Wnew); free(complexPairs);
}

#undef lin_index
#undef EIG_EPS

} // namespace

// Eigenvalues (D[n]) and, when V != NULL, eigenvectors (V[n*n], row-major:
// V[i*n+j] is component i of eigenvector j).  Returns 1 on success.
int CFullMatrix::Eig(CComplex *D, CComplex *V)
{
    if (n <= 0) return 0;
    CComplex *A  = (CComplex*)malloc((size_t)n * n * sizeof(CComplex));
    CComplex *Vi = (CComplex*)malloc((size_t)n * n * sizeof(CComplex));
    if (!A || !Vi) { free(A); free(Vi); return 0; }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            A[i * n + j] = M[i][j];                 // CFullMatrix stores M[i][j]

    eig_impl(A, (unsigned)n, Vi, D);

    if (V) for (int k = 0; k < n * n; k++) V[k] = Vi[k];

    free(A); free(Vi);
    return 1;
}

// ===========================================================================
// Flat-buffer entry points for the embedded-Python clinalg module.
// These reuse the tested kernels above (eig_impl, qrGivensComplex), which have
// internal linkage but are visible to the rest of this translation unit.
// ===========================================================================

namespace femm_linalg {

int eig(const CComplex *A, int n, CComplex *D, CComplex *V) {
    if (n <= 0) return 0;
    std::vector<CComplex> work(A, A + (size_t)n * n);   // eig_impl uses a workspace
    std::vector<CComplex> Vtmp((size_t)n * n);
    eig_impl(work.data(), (unsigned)n, Vtmp.data(), D); // (A, N, V, D)
    if (V) for (size_t k = 0; k < (size_t)n * n; k++) V[k] = Vtmp[k];
    return 1;
}

int qr(const CComplex *A, int m, int n, CComplex *Q, CComplex *R) {
    if (m <= 0 || n <= 0) return 0;
    std::vector<CComplex> work(A, A + (size_t)m * n);   // qrGivensComplex copies A into R
    qrGivensComplex(work.data(), (unsigned)m, (unsigned)n, Q, R);
    return 1;
}

} // namespace femm_linalg

// ===========================================================================
// One-sided Jacobi SVD, ported from the .m matrix runtime (xlab mtx.cpp) and
// retyped onto a small local column-major matrix so it needs no Lua binding.
// ===========================================================================
namespace {

struct FMtx {                                  // column-major dense complex
    int r = 0, c = 0;
    std::vector<CComplex> d;
    FMtx() {}
    FMtx(int rr, int cc) : r(rr), c(cc), d((size_t)rr * cc, CComplex(0, 0)) {}
    CComplex &at(int i, int j)             { return d[(size_t)i + (size_t)j * r]; }
    CComplex  at(int i, int j) const       { return d[(size_t)i + (size_t)j * r]; }
};


// A = U diag(S) V^H, with U (m x n orthonormal cols), S (descending), V (n x n).
static void svdJacobiTall(const FMtx &A, FMtx &U, std::vector<double> &S, FMtx &V) {
    int m = A.r, n = A.c;
    U = A;
    V = FMtx(n, n);
    for (int i = 0; i < n; i++) V.at(i, i) = CComplex(1, 0);

    for (int sweep = 0; sweep < 80; sweep++) {
        bool changed = false;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                double a = 0, b = 0; CComplex g(0, 0);
                for (int k = 0; k < m; k++) {
                    a += absq(U.at(k, i));
                    b += absq(U.at(k, j));
                    g = g + conj(U.at(k, i)) * U.at(k, j);
                }
                double absg = abs(g);
                if (absg == 0 || absg <= 1e-15 * sqrt(a * b)) continue;
                changed = true;
                double zeta = (b - a) / (2 * absg);
                double t = (zeta >= 0 ? 1.0 : -1.0) / (fabs(zeta) + sqrt(zeta * zeta + 1));
                double cs = 1.0 / sqrt(t * t + 1), sn = cs * t;
                CComplex cph = conj(g / CComplex(absg, 0));
                CComplex C(cs, 0), Sn(sn, 0);
                for (int k = 0; k < m; k++) {
                    CComplex ui = U.at(k, i), uj = U.at(k, j);
                    U.at(k, i) = C * ui - cph * Sn * uj;
                    U.at(k, j) = Sn * ui + cph * C * uj;
                }
                for (int k = 0; k < n; k++) {
                    CComplex vi = V.at(k, i), vj = V.at(k, j);
                    V.at(k, i) = C * vi - cph * Sn * vj;
                    V.at(k, j) = Sn * vi + cph * C * vj;
                }
            }
        if (!changed) break;
    }

    S.assign(n, 0);
    for (int k = 0; k < n; k++) {
        double s = 0; for (int i = 0; i < m; i++) s += absq(U.at(i, k));
        s = sqrt(s); S[k] = s;
        if (s > 0) for (int i = 0; i < m; i++) U.at(i, k) = U.at(i, k) / CComplex(s, 0);
    }
    std::vector<int> ord(n);
    for (int i = 0; i < n; i++) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int x, int y) { return S[x] > S[y]; });
    FMtx U2(m, n), V2(n, n); std::vector<double> S2(n);
    for (int k = 0; k < n; k++) {
        int o = ord[k]; S2[k] = S[o];
        for (int i = 0; i < m; i++) U2.at(i, k) = U.at(i, o);
        for (int i = 0; i < n; i++) V2.at(i, k) = V.at(i, o);
    }
    U = U2; V = V2; S = S2;
}

// Economy SVD any shape: U (m x k), V (n x k), S (k = min(m,n)); A = U diag(S) V^H.
static void computeSVD(const FMtx &A, FMtx &U, std::vector<double> &S, FMtx &V) {
    if (A.r >= A.c) { svdJacobiTall(A, U, S, V); return; }
    FMtx Ah(A.c, A.r);
    for (int i = 0; i < A.r; i++)
        for (int j = 0; j < A.c; j++) Ah.at(j, i) = conj(A.at(i, j));
    FMtx Ua, Va; std::vector<double> s;
    svdJacobiTall(Ah, Ua, s, Va);
    U = Va; V = Ua; S = s;
}

} // namespace

namespace femm_linalg {

int svd(const CComplex *A, int m, int n, CComplex *U, double *S, CComplex *Vh) {
    if (m <= 0 || n <= 0) return 0;
    int k = (m < n) ? m : n;
    FMtx Am(m, n);
    for (int i = 0; i < m; i++)
        for (int j = 0; j < n; j++) Am.at(i, j) = A[(size_t)i * n + j];  // row-major -> col-major
    FMtx Um, Vm; std::vector<double> Sv;
    computeSVD(Am, Um, Sv, Vm);                       // Um(m x k), Vm(n x k), Sv(k)
    for (int i = 0; i < m; i++)
        for (int t = 0; t < k; t++) U[(size_t)i * k + t] = Um.at(i, t);
    for (int t = 0; t < k; t++) S[t] = Sv[t];
    for (int t = 0; t < k; t++)
        for (int j = 0; j < n; j++) Vh[(size_t)t * n + j] = conj(Vm.at(j, t));  // V^H
    return 1;
}

int solve(const CComplex *A, int n, const CComplex *B, int nrhs, CComplex *X) {
    if (n <= 0 || nrhs <= 0) return 0;
    for (int col = 0; col < nrhs; col++) {
        CFullMatrix L;
        if (!L.Create(n)) return 0;
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) L.M[i][j] = A[(size_t)i * n + j];
            L.b[i] = B[(size_t)i * nrhs + col];
        }
        if (!L.GaussSolve()) return 0;                // singular
        for (int i = 0; i < n; i++) X[(size_t)i * nrhs + col] = L.b[i];
    }
    return 1;
}

int inv(const CComplex *A, int n, CComplex *X) {
    if (n <= 0) return 0;
    std::vector<CComplex> eye((size_t)n * n, CComplex(0, 0));   // not 'I' (macro)
    for (int i = 0; i < n; i++) eye[(size_t)i * n + i] = CComplex(1, 0);
    return solve(A, n, eye.data(), n, X);
}

int det(const CComplex *A, int n, CComplex *out) {
    if (n <= 0) return 0;
    std::vector<CComplex> M(A, A + (size_t)n * n);    // row-major working copy
    CComplex d(1, 0);
    for (int i = 0; i < n; i++) {
        int piv = i; double best = abs(M[(size_t)i * n + i]);
        for (int r = i + 1; r < n; r++) {
            double v = abs(M[(size_t)r * n + i]);
            if (v > best) { best = v; piv = r; }
        }
        if (best == 0) { *out = CComplex(0, 0); return 1; }
        if (piv != i) {
            for (int c = 0; c < n; c++)
                std::swap(M[(size_t)i * n + c], M[(size_t)piv * n + c]);
            d = CComplex(0, 0) - d;                    // row swap flips sign
        }
        CComplex pivval = M[(size_t)i * n + i];
        d = d * pivval;
        for (int r = i + 1; r < n; r++) {
            CComplex f = M[(size_t)r * n + i] / pivval;
            for (int c = i; c < n; c++)
                M[(size_t)r * n + c] = M[(size_t)r * n + c] - f * M[(size_t)i * n + c];
        }
    }
    *out = d;
    return 1;
}

int dot(const CComplex *A, int m, int k, const CComplex *B, int n, CComplex *C) {
    if (m <= 0 || k <= 0 || n <= 0) return 0;
    complexMatrixMultiplication(const_cast<CComplex *>(A), const_cast<CComplex *>(B),
                                (unsigned)m, (unsigned)k, (unsigned)n, C);   // C(m x n)
    return 1;
}

int pinv(const CComplex *A, int m, int n, CComplex *X) {
    if (m <= 0 || n <= 0) return 0;
    int k = (m < n) ? m : n;
    std::vector<CComplex> U((size_t)m * k), Vh((size_t)k * n);
    std::vector<double> S(k);
    if (!svd(A, m, n, U.data(), S.data(), Vh.data())) return 0;
    double smax = (k > 0) ? S[0] : 0.0;
    double tol  = ((m > n) ? m : n) * 2.220446049250313e-16 * smax;
    // X(n x m) = V diag(1/s) U^H ;  V[i][t] = conj(Vh[t][i]),  U^H[t][j] = conj(U[j][t])
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) {
            CComplex acc(0, 0);
            for (int t = 0; t < k; t++)
                if (S[t] > tol)
                    acc = acc + conj(Vh[(size_t)t * n + i])
                              * CComplex(1.0 / S[t], 0)
                              * conj(U[(size_t)j * k + t]);
            X[(size_t)i * m + j] = acc;
        }
    return 1;
}

int cond(const CComplex *A, int m, int n, double *out) {
    if (m <= 0 || n <= 0) return 0;
    int k = (m < n) ? m : n;
    std::vector<CComplex> U((size_t)m * k), Vh((size_t)k * n);
    std::vector<double> S(k);
    if (!svd(A, m, n, U.data(), S.data(), Vh.data())) return 0;
    double smin = S[k - 1];
    *out = (smin > 0) ? (S[0] / smin) : INFINITY;
    return 1;
}

int rank(const CComplex *A, int m, int n, int *out) {
    if (m <= 0 || n <= 0) return 0;
    int k = (m < n) ? m : n;
    std::vector<CComplex> U((size_t)m * k), Vh((size_t)k * n);
    std::vector<double> S(k);
    if (!svd(A, m, n, U.data(), S.data(), Vh.data())) return 0;
    double smax = (k > 0) ? S[0] : 0.0;
    double tol  = ((m > n) ? m : n) * 2.220446049250313e-16 * smax;
    int r = 0;
    for (int t = 0; t < k; t++) if (S[t] > tol) r++;
    *out = r;
    return 1;
}

int norm(const CComplex *A, int count, double *out) {
    double s = 0;
    for (int i = 0; i < count; i++) s += absq(A[i]);
    *out = sqrt(s);
    return 1;
}

} // namespace femm_linalg
