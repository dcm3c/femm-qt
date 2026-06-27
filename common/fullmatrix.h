// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker

// fullmatrix.h — small dense complex matrix with a Gaussian-elimination solver.
// Moved here from solvers/common and relicensed MIT so the MIT femm_common
// library (and thus the .m matrix runtime) can use it for inv / mldivide.

#ifndef FEMM_FULLMATRIX_H
#define FEMM_FULLMATRIX_H

#include "complex.h"

class CFullMatrix{

	public:

		// data members
		CComplex **M;			// Matrix on LHS
		CComplex *b;			// vector on RHS
		int n;				// dimension of the matrix

		// member functions
		CFullMatrix();
		CFullMatrix(int d);
		~CFullMatrix();
		void Wipe();
		int Create(int d);
		int GaussSolve();

		// Eigenvalues of this n x n matrix into D[n]; when V != NULL, also the
		// eigenvectors into V[n*n] row-major (V[i*n+j] = component i of
		// eigenvector j).  General complex matrices (Francis double-shift QR).
		// Ported from melihaltun/complexEigenPairSolver (MIT) — see fullmatrix.cpp.
		int Eig(CComplex *D, CComplex *V = 0);

	private:

};

// Flat-buffer complex linear-algebra entry points used by the embedded-Python
// clinalg module. Matrices are row-major, contiguous CComplex — bit-compatible
// with ulab's complex ndarray storage (two contiguous doubles per element).
// Return 1 on success, 0 on failure.
namespace femm_linalg {

    // Eigen-decomposition of n x n A.  D[n] = eigenvalues; if V != nullptr,
    // V[n*n] row-major with column j = eigenvector j (numpy's v[:,j] layout).
    int eig(const CComplex *A, int n, CComplex *D, CComplex *V = nullptr);

    // Complete QR of an m x n matrix A:  A = Q R, with Q[m*m] and R[m*n]
    // row-major.  (numpy's default 'reduced' mode is a slice of this.)
    int qr(const CComplex *A, int m, int n, CComplex *Q, CComplex *R);

    // Economy SVD of m x n A:  A = U diag(S) Vh,  k = min(m,n).
    // U[m*k] row-major, S[k] real (descending), Vh[k*n] row-major (= V^H).
    int svd(const CComplex *A, int m, int n, CComplex *U, double *S, CComplex *Vh);

    // Solve A X = B.  A is n x n, B is n x nrhs, X is n x nrhs (all row-major).
    // Returns 0 if A is singular.
    int solve(const CComplex *A, int n, const CComplex *B, int nrhs, CComplex *X);

    // Inverse of n x n A into X[n*n].  Returns 0 if singular.
    int inv(const CComplex *A, int n, CComplex *X);

    // Determinant of n x n A into *out.
    int det(const CComplex *A, int n, CComplex *out);

    // Matrix product C = A B.  A is m x k, B is k x n, C is m x n (row-major).
    int dot(const CComplex *A, int m, int k, const CComplex *B, int n, CComplex *C);

    // Moore-Penrose pseudo-inverse of m x n A into X[n*m] (row-major).
    int pinv(const CComplex *A, int m, int n, CComplex *X);

    // 2-norm condition number of m x n A (s_max / s_min) into *out.
    int cond(const CComplex *A, int m, int n, double *out);

    // Numerical rank of m x n A (singular values > max(m,n)*eps*s_max) into *out.
    int rank(const CComplex *A, int m, int n, int *out);

    // Frobenius norm of a `count`-element contiguous buffer into *out.
    int norm(const CComplex *A, int count, double *out);

}

#endif // FEMM_FULLMATRIX_H
