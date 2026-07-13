// SPDX-License-Identifier: MIT
// Copyright (c) 2026 David Meeker
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef FLOAT256_H
#define FLOAT256_H

#include <cstdint>

// float256: extended-precision floating point.
// Represented as one IEEE double (sign, exponent, top 52 mantissa bits)
// plus three uint64_t values providing 192 additional mantissa bits,
// for ~245 bits of mantissa precision total.
//
// The full mantissa is: 1.{52 bits from double}{lo[0]}{lo[1]}{lo[2]}
// where the implicit 1 and 52-bit fraction come from the double,
// and lo[0..2] extend the mantissa below the double's LSB.
// lo bits always extend magnitude (away from zero).

class float256
{
public:
	// data members
	double hi;       // sign + exponent + top 52 mantissa bits
	uint64_t lo[3];  // next 192 mantissa bits (lo[0] most significant)

	// constructors
	float256();
	float256(double a);
	float256(double h, uint64_t l0, uint64_t l1, uint64_t l2);

	// arithmetic
	float256 operator+(const float256& z) const;
	float256 operator-() const;
	float256 operator-(const float256& z) const;
	float256 operator*(const float256& z) const;
};

// float128: extended-precision floating point (d+1xu64 format).
// One IEEE double (sign, exponent, top 52 mantissa bits) plus one
// uint64_t providing 64 additional mantissa bits, for ~117 bits of
// mantissa precision total. Sufficient for exact orient2d/orient3d
// predicates on double-precision coordinates.

class float128
{
public:
	double hi;    // sign + exponent + top 52 mantissa bits
	uint64_t lo;  // next 64 mantissa bits

	// constructors
	float128();
	float128(double a);
	float128(double h, uint64_t l);

	// arithmetic
	float128 operator+(const float128& z) const;
	float128 operator-() const;
	float128 operator-(const float128& z) const;
	float128 operator*(const float128& z) const;
};

#endif // FLOAT256_H
