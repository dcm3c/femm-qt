// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker


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

	private:

};
