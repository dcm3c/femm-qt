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

#include "float256.h"
#include <cmath>
#include <cstring>

// ============================================================
// 256-bit unsigned integer helpers for mantissa arithmetic
// ============================================================

namespace {

struct u256 {
	uint64_t w[4]; // w[0] = most significant word
};

static const u256 U256_ZERO = {{0, 0, 0, 0}};

int u256_cmp(u256 a, u256 b) {
	for (int i = 0; i < 4; i++) {
		if (a.w[i] < b.w[i]) return -1;
		if (a.w[i] > b.w[i]) return 1;
	}
	return 0;
}

#if defined(__GNUC__) || defined(__clang__)

u256 u256_add(u256 a, u256 b, int& carry) {
	u256 r;
	unsigned __int128 c = 0;
	for (int i = 3; i >= 0; i--) {
		c += (unsigned __int128)a.w[i] + b.w[i];
		r.w[i] = (uint64_t)c;
		c >>= 64;
	}
	carry = (int)c;
	return r;
}

u256 u256_sub(u256 a, u256 b, int& borrow) {
	u256 r;
	__int128 c = 0;
	for (int i = 3; i >= 0; i--) {
		c += (__int128)a.w[i] - b.w[i];
		r.w[i] = (uint64_t)c;
		c >>= 64;
	}
	borrow = (c < 0) ? 1 : 0;
	return r;
}

#else // MSVC and other compilers

u256 u256_add(u256 a, u256 b, int& carry) {
	u256 r;
	uint64_t c = 0;
	for (int i = 3; i >= 0; i--) {
		uint64_t s = a.w[i] + b.w[i];
		uint64_t c1 = (s < a.w[i]) ? 1 : 0;
		r.w[i] = s + c;
		uint64_t c2 = (r.w[i] < s) ? 1 : 0;
		c = c1 + c2;
	}
	carry = (int)c;
	return r;
}

u256 u256_sub(u256 a, u256 b, int& borrow) {
	u256 r;
	uint64_t bw = 0;
	for (int i = 3; i >= 0; i--) {
		uint64_t d = a.w[i] - b.w[i];
		uint64_t b1 = (a.w[i] < b.w[i]) ? 1 : 0;
		r.w[i] = d - bw;
		uint64_t b2 = (d < bw) ? 1 : 0;
		bw = b1 + b2;
	}
	borrow = (int)bw;
	return r;
}

#endif

u256 u256_shr(u256 a, int s) {
	if (s >= 256) return U256_ZERO;
	if (s == 0) return a;
	u256 r = U256_ZERO;
	int ws = s >> 6;
	int bs = s & 63;
	if (bs == 0) {
		for (int i = 3; i >= ws; i--)
			r.w[i] = a.w[i - ws];
	} else {
		for (int i = 3; i > ws; i--)
			r.w[i] = (a.w[i - ws] >> bs) | (a.w[i - ws - 1] << (64 - bs));
		r.w[ws] = a.w[0] >> bs;
	}
	return r;
}

u256 u256_shl(u256 a, int s) {
	if (s >= 256) return U256_ZERO;
	if (s == 0) return a;
	u256 r = U256_ZERO;
	int ws = s >> 6;
	int bs = s & 63;
	if (bs == 0) {
		for (int i = 0; i <= 3 - ws; i++)
			r.w[i] = a.w[i + ws];
	} else {
		for (int i = 0; i < 3 - ws; i++)
			r.w[i] = (a.w[i + ws] << bs) | (a.w[i + ws + 1] >> (64 - bs));
		r.w[3 - ws] = a.w[3] << bs;
	}
	return r;
}

int u256_clz(u256 a) {
	for (int i = 0; i < 4; i++) {
		if (a.w[i] != 0) {
			int z = 0;
			uint64_t v = a.w[i];
			if (!(v & 0xFFFFFFFF00000000ULL)) { z += 32; v <<= 32; }
			if (!(v & 0xFFFF000000000000ULL)) { z += 16; v <<= 16; }
			if (!(v & 0xFF00000000000000ULL)) { z += 8; v <<= 8; }
			if (!(v & 0xF000000000000000ULL)) { z += 4; v <<= 4; }
			if (!(v & 0xC000000000000000ULL)) { z += 2; v <<= 2; }
			if (!(v & 0x8000000000000000ULL)) { z += 1; }
			return i * 64 + z;
		}
	}
	return 256;
}

#if defined(__GNUC__) || defined(__clang__)

// 256x256 -> 512-bit multiply using __int128 for partial products.
void u256_mul(u256 a, u256 b, u256& rhi, u256& rlo) {
	unsigned __int128 accum[8] = {};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			unsigned __int128 prod = (unsigned __int128)a.w[i] * b.w[j];
			accum[i + j] += prod >> 64;
			accum[i + j + 1] += (uint64_t)prod;
		}
	}
	for (int k = 7; k > 0; k--) {
		accum[k - 1] += accum[k] >> 64;
		accum[k] &= 0xFFFFFFFFFFFFFFFFULL;
	}
	for (int k = 0; k < 4; k++) {
		rhi.w[k] = (uint64_t)accum[k];
		rlo.w[k] = (uint64_t)accum[k + 4];
	}
}

#else // MSVC and other compilers

// 64x64 -> 128-bit multiply using 32-bit pieces (portable)
void mul64_256(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
	uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
	uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;

	uint64_t p0 = a_lo * b_lo;
	uint64_t p1 = a_lo * b_hi;
	uint64_t p2 = a_hi * b_lo;
	uint64_t p3 = a_hi * b_hi;

	uint64_t mid = p1 + (p0 >> 32);
	mid += p2;
	if (mid < p2) p3 += (1ULL << 32);

	hi = p3 + (mid >> 32);
	lo = ((mid & 0xFFFFFFFFULL) << 32) | (p0 & 0xFFFFFFFFULL);
}

// 256x256 -> 512-bit multiply using portable 64x64->128 multiply.
void u256_mul(u256 a, u256 b, u256& rhi, u256& rlo) {
	uint64_t acc_hi[8] = {};
	uint64_t acc_lo[8] = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			uint64_t ph, pl;
			mul64_256(a.w[i], b.w[j], ph, pl);
			acc_lo[i + j] += ph;
			if (acc_lo[i + j] < ph) acc_hi[i + j]++;
			acc_lo[i + j + 1] += pl;
			if (acc_lo[i + j + 1] < pl) acc_hi[i + j + 1]++;
		}
	}

	for (int k = 7; k > 0; k--) {
		acc_lo[k - 1] += acc_hi[k];
		if (acc_lo[k - 1] < acc_hi[k]) acc_hi[k - 1]++;
	}

	for (int k = 0; k < 4; k++) {
		rhi.w[k] = acc_lo[k];
		rlo.w[k] = acc_lo[k + 4];
	}
}

#endif

// ============================================================
// Pack / unpack between float256 and (sign, exponent, mantissa)
// ============================================================

bool f256_is_zero(const float256& a) {
	return a.hi == 0.0 && a.lo[0] == 0 && a.lo[1] == 0 && a.lo[2] == 0;
}

bool f256_is_special(double d) {
	uint64_t bits;
	memcpy(&bits, &d, 8);
	return ((bits >> 52) & 0x7FF) == 0x7FF;
}

void f256_unpack(double hi, const uint64_t lo[3],
                 bool& sign, int& exp, u256& m) {
	uint64_t bits;
	memcpy(&bits, &hi, 8);
	sign = (bits >> 63) != 0;
	exp = (int)((bits >> 52) & 0x7FF) - 1023;
	uint64_t frac = bits & 0x000FFFFFFFFFFFFFULL;

	m.w[0] = (1ULL << 63) | (frac << 11) | (lo[0] >> 53);
	m.w[1] = (lo[0] << 11) | (lo[1] >> 53);
	m.w[2] = (lo[1] << 11) | (lo[2] >> 53);
	m.w[3] = lo[2] << 11;
}

void f256_pack(bool sign, int exp, u256 m, double& hi, uint64_t lo[3]) {
	int biased = exp + 1023;
	if (biased >= 2047) {
		hi = sign ? -INFINITY : INFINITY;
		lo[0] = lo[1] = lo[2] = 0;
		return;
	}
	if (biased <= 0) {
		hi = sign ? -0.0 : 0.0;
		lo[0] = lo[1] = lo[2] = 0;
		return;
	}

	uint64_t frac = (m.w[0] >> 11) & 0x000FFFFFFFFFFFFFULL;
	uint64_t bits = ((uint64_t)sign << 63) | ((uint64_t)biased << 52) | frac;
	memcpy(&hi, &bits, 8);

	lo[0] = (m.w[0] << 53) | (m.w[1] >> 11);
	lo[1] = (m.w[1] << 53) | (m.w[2] >> 11);
	lo[2] = (m.w[2] << 53) | (m.w[3] >> 11);
}

} // anonymous namespace

// ============================================================
// float256 Constructors
// ============================================================

float256::float256() : hi(0.0), lo{0, 0, 0} {}

float256::float256(double a) : hi(a), lo{0, 0, 0} {}

float256::float256(double h, uint64_t l0, uint64_t l1, uint64_t l2)
	: hi(h), lo{l0, l1, l2} {}

// ============================================================
// float256 Addition
// ============================================================

float256 float256::operator+(const float256& b) const {
	if (f256_is_zero(*this)) return b;
	if (f256_is_zero(b)) return *this;
	if (f256_is_special(hi) || f256_is_special(b.hi))
		return float256(hi + b.hi);

	bool s1, s2;
	int e1, e2;
	u256 m1, m2;
	f256_unpack(hi, lo, s1, e1, m1);
	f256_unpack(b.hi, b.lo, s2, e2, m2);

	// Make operand 1 the one with larger or equal exponent
	const float256* big_ptr = this;
	if (e2 > e1) {
		bool ts = s1; s1 = s2; s2 = ts;
		int te = e1; e1 = e2; e2 = te;
		u256 tm = m1; m1 = m2; m2 = tm;
		big_ptr = &b;
	}

	int shift = e1 - e2;
	if (shift >= 256)
		return *big_ptr;

	u256 m2a = u256_shr(m2, shift);

	bool result_sign;
	int result_exp = e1;
	u256 result_m;

	if (s1 == s2) {
		int carry;
		result_m = u256_add(m1, m2a, carry);
		result_sign = s1;
		if (carry) {
			result_m = u256_shr(result_m, 1);
			result_m.w[0] |= (1ULL << 63);
			result_exp++;
		}
	} else {
		int cmp = u256_cmp(m1, m2a);
		if (cmp == 0)
			return float256(0.0);
		int borrow;
		if (cmp > 0) {
			result_m = u256_sub(m1, m2a, borrow);
			result_sign = s1;
		} else {
			result_m = u256_sub(m2a, m1, borrow);
			result_sign = s2;
		}
		int lz = u256_clz(result_m);
		if (lz >= 256)
			return float256(0.0);
		result_m = u256_shl(result_m, lz);
		result_exp -= lz;
	}

	double rhi;
	uint64_t rlo[3];
	f256_pack(result_sign, result_exp, result_m, rhi, rlo);
	return float256(rhi, rlo[0], rlo[1], rlo[2]);
}

// ============================================================
// float256 Negation and Subtraction
// ============================================================

float256 float256::operator-() const {
	return float256(-hi, lo[0], lo[1], lo[2]);
}

float256 float256::operator-(const float256& z) const { return *this + (-z); }

// ============================================================
// float256 Multiplication
// ============================================================

float256 float256::operator*(const float256& b) const {
	if (f256_is_zero(*this) || f256_is_zero(b))
		return float256(hi * b.hi); // preserves sign of zero
	if (f256_is_special(hi) || f256_is_special(b.hi))
		return float256(hi * b.hi);

	bool s1, s2;
	int e1, e2;
	u256 m1, m2;
	f256_unpack(hi, lo, s1, e1, m1);
	f256_unpack(b.hi, b.lo, s2, e2, m2);

	u256 phi, plo;
	u256_mul(m1, m2, phi, plo);

	bool result_sign = (s1 != s2);
	int result_exp = e1 + e2;
	u256 result_m;

	if (phi.w[0] >> 63) {
		// Product MSB at bit 511: mantissa = top 256 bits as-is
		result_m = phi;
		result_exp += 1;
	} else {
		// Product MSB at bit 510: shift left by 1 to normalize
		result_m.w[0] = (phi.w[0] << 1) | (phi.w[1] >> 63);
		result_m.w[1] = (phi.w[1] << 1) | (phi.w[2] >> 63);
		result_m.w[2] = (phi.w[2] << 1) | (phi.w[3] >> 63);
		result_m.w[3] = (phi.w[3] << 1) | (plo.w[0] >> 63);
	}

	double rhi;
	uint64_t rlo[3];
	f256_pack(result_sign, result_exp, result_m, rhi, rlo);
	return float256(rhi, rlo[0], rlo[1], rlo[2]);
}

// ============================================================
// float128: extended-precision floating point (d+1xu64 format)
//
// 128-bit unsigned integer helpers for mantissa arithmetic
// ============================================================

namespace {

struct u128 {
	uint64_t w[2]; // w[0] = most significant word
};

static const u128 U128_ZERO = {{0, 0}};

int u128_cmp(u128 a, u128 b) {
	if (a.w[0] < b.w[0]) return -1;
	if (a.w[0] > b.w[0]) return 1;
	if (a.w[1] < b.w[1]) return -1;
	if (a.w[1] > b.w[1]) return 1;
	return 0;
}

u128 u128_add(u128 a, u128 b, int& carry) {
	u128 r;
	r.w[1] = a.w[1] + b.w[1];
	uint64_t c = (r.w[1] < a.w[1]) ? 1 : 0;
	r.w[0] = a.w[0] + b.w[0] + c;
	carry = (r.w[0] < a.w[0] || (c && r.w[0] == a.w[0])) ? 1 : 0;
	return r;
}

u128 u128_sub(u128 a, u128 b, int& borrow) {
	u128 r;
	r.w[1] = a.w[1] - b.w[1];
	uint64_t bw = (a.w[1] < b.w[1]) ? 1 : 0;
	r.w[0] = a.w[0] - b.w[0] - bw;
	borrow = (a.w[0] < b.w[0] + bw || (bw && b.w[0] == UINT64_MAX)) ? 1 : 0;
	return r;
}

u128 u128_shr(u128 a, int s) {
	if (s >= 128) return U128_ZERO;
	if (s == 0) return a;
	u128 r = U128_ZERO;
	if (s >= 64) {
		r.w[1] = a.w[0] >> (s - 64);
	} else {
		r.w[1] = (a.w[1] >> s) | (a.w[0] << (64 - s));
		r.w[0] = a.w[0] >> s;
	}
	return r;
}

u128 u128_shl(u128 a, int s) {
	if (s >= 128) return U128_ZERO;
	if (s == 0) return a;
	u128 r = U128_ZERO;
	if (s >= 64) {
		r.w[0] = a.w[1] << (s - 64);
	} else {
		r.w[0] = (a.w[0] << s) | (a.w[1] >> (64 - s));
		r.w[1] = a.w[1] << s;
	}
	return r;
}

int u128_clz(u128 a) {
	for (int i = 0; i < 2; i++) {
		if (a.w[i] != 0) {
			int z = 0;
			uint64_t v = a.w[i];
			if (!(v & 0xFFFFFFFF00000000ULL)) { z += 32; v <<= 32; }
			if (!(v & 0xFFFF000000000000ULL)) { z += 16; v <<= 16; }
			if (!(v & 0xFF00000000000000ULL)) { z += 8; v <<= 8; }
			if (!(v & 0xF000000000000000ULL)) { z += 4; v <<= 4; }
			if (!(v & 0xC000000000000000ULL)) { z += 2; v <<= 2; }
			if (!(v & 0x8000000000000000ULL)) { z += 1; }
			return i * 64 + z;
		}
	}
	return 128;
}

// 64x64 -> 128-bit multiply using 32-bit pieces (portable)
void mul64_128(uint64_t a, uint64_t b, uint64_t& hi, uint64_t& lo) {
	uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
	uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;

	uint64_t p0 = a_lo * b_lo;
	uint64_t p1 = a_lo * b_hi;
	uint64_t p2 = a_hi * b_lo;
	uint64_t p3 = a_hi * b_hi;

	uint64_t mid = p1 + (p0 >> 32);
	mid += p2;
	if (mid < p2) p3 += (1ULL << 32); // carry from mid overflow

	hi = p3 + (mid >> 32);
	lo = ((mid & 0xFFFFFFFFULL) << 32) | (p0 & 0xFFFFFFFFULL);
}

// 128x128 -> 256-bit multiply. Returns top and bottom 128-bit halves.
void u128_mul(u128 a, u128 b, u128& rhi, u128& rlo) {
	uint64_t accum_hi[4] = {};
	uint64_t accum_lo[4] = {};

	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 2; j++) {
			uint64_t ph, pl;
			mul64_128(a.w[i], b.w[j], ph, pl);
			accum_lo[i + j] += ph;
			if (accum_lo[i + j] < ph) accum_hi[i + j]++;
			accum_lo[i + j + 1] += pl;
			if (accum_lo[i + j + 1] < pl) {
				accum_hi[i + j + 1]++;
			}
		}
	}

	for (int k = 3; k > 0; k--) {
		accum_lo[k - 1] += accum_hi[k];
		if (accum_lo[k - 1] < accum_hi[k]) accum_hi[k - 1]++;
	}

	rhi.w[0] = accum_lo[0];
	rhi.w[1] = accum_lo[1];
	rlo.w[0] = accum_lo[2];
	rlo.w[1] = accum_lo[3];
}

bool f128_is_zero(const float128& a) {
	return a.hi == 0.0 && a.lo == 0;
}

bool f128_is_special(double d) {
	uint64_t bits;
	memcpy(&bits, &d, 8);
	return ((bits >> 52) & 0x7FF) == 0x7FF;
}

void f128_unpack(double hi, uint64_t lo,
                 bool& sign, int& exp, u128& m) {
	uint64_t bits;
	memcpy(&bits, &hi, 8);
	sign = (bits >> 63) != 0;
	exp = (int)((bits >> 52) & 0x7FF) - 1023;
	uint64_t frac = bits & 0x000FFFFFFFFFFFFFULL;

	m.w[0] = (1ULL << 63) | (frac << 11) | (lo >> 53);
	m.w[1] = lo << 11;
}

void f128_pack(bool sign, int exp, u128 m, double& hi, uint64_t& lo) {
	int biased = exp + 1023;
	if (biased >= 2047) {
		hi = sign ? -INFINITY : INFINITY;
		lo = 0;
		return;
	}
	if (biased <= 0) {
		hi = sign ? -0.0 : 0.0;
		lo = 0;
		return;
	}

	uint64_t frac = (m.w[0] >> 11) & 0x000FFFFFFFFFFFFFULL;
	uint64_t bits = ((uint64_t)sign << 63) | ((uint64_t)biased << 52) | frac;
	memcpy(&hi, &bits, 8);

	lo = (m.w[0] << 53) | (m.w[1] >> 11);
}

} // anonymous namespace (float128 helpers)

// ============================================================
// float128 Constructors
// ============================================================

float128::float128() : hi(0.0), lo(0) {}

float128::float128(double a) : hi(a), lo(0) {}

float128::float128(double h, uint64_t l) : hi(h), lo(l) {}

// ============================================================
// float128 Addition
// ============================================================

float128 float128::operator+(const float128& b) const {
	if (f128_is_zero(*this)) return b;
	if (f128_is_zero(b)) return *this;
	if (f128_is_special(hi) || f128_is_special(b.hi))
		return float128(hi + b.hi);

	bool s1, s2;
	int e1, e2;
	u128 m1, m2;
	f128_unpack(hi, lo, s1, e1, m1);
	f128_unpack(b.hi, b.lo, s2, e2, m2);

	const float128* big_ptr = this;
	if (e2 > e1) {
		bool ts = s1; s1 = s2; s2 = ts;
		int te = e1; e1 = e2; e2 = te;
		u128 tm = m1; m1 = m2; m2 = tm;
		big_ptr = &b;
	}

	int shift = e1 - e2;
	if (shift >= 128)
		return *big_ptr;

	u128 m2a = u128_shr(m2, shift);

	bool result_sign;
	int result_exp = e1;
	u128 result_m;

	if (s1 == s2) {
		int carry;
		result_m = u128_add(m1, m2a, carry);
		result_sign = s1;
		if (carry) {
			result_m = u128_shr(result_m, 1);
			result_m.w[0] |= (1ULL << 63);
			result_exp++;
		}
	} else {
		int cmp = u128_cmp(m1, m2a);
		if (cmp == 0)
			return float128(0.0);
		int borrow;
		if (cmp > 0) {
			result_m = u128_sub(m1, m2a, borrow);
			result_sign = s1;
		} else {
			result_m = u128_sub(m2a, m1, borrow);
			result_sign = s2;
		}
		int lz = u128_clz(result_m);
		if (lz >= 128)
			return float128(0.0);
		result_m = u128_shl(result_m, lz);
		result_exp -= lz;
	}

	double rhi;
	uint64_t rlo;
	f128_pack(result_sign, result_exp, result_m, rhi, rlo);
	return float128(rhi, rlo);
}

// ============================================================
// float128 Negation and Subtraction
// ============================================================

float128 float128::operator-() const {
	return float128(-hi, lo);
}

float128 float128::operator-(const float128& z) const { return *this + (-z); }

// ============================================================
// float128 Multiplication
// ============================================================

float128 float128::operator*(const float128& b) const {
	if (f128_is_zero(*this) || f128_is_zero(b))
		return float128(hi * b.hi);
	if (f128_is_special(hi) || f128_is_special(b.hi))
		return float128(hi * b.hi);

	bool s1, s2;
	int e1, e2;
	u128 m1, m2;
	f128_unpack(hi, lo, s1, e1, m1);
	f128_unpack(b.hi, b.lo, s2, e2, m2);

	u128 phi, plo;
	u128_mul(m1, m2, phi, plo);

	bool result_sign = (s1 != s2);
	int result_exp = e1 + e2;
	u128 result_m;

	if (phi.w[0] >> 63) {
		result_m = phi;
		result_exp += 1;
	} else {
		result_m.w[0] = (phi.w[0] << 1) | (phi.w[1] >> 63);
		result_m.w[1] = (phi.w[1] << 1) | (plo.w[0] >> 63);
	}

	double rhi;
	uint64_t rlo;
	f128_pack(result_sign, result_exp, result_m, rhi, rlo);
	return float128(rhi, rlo);
}
