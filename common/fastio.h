// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker

// fastio.h -- fast text I/O helpers for the .ans hot loops, shared by the
// solvers (write + previous-solution read) and libfemm's postprocessor
// readers. Lives in common/ on the MIT side of the license split so both
// may use it.
//
// The same two techniques tangle uses for the Shewchuk-format files, where
// they were measured to dominate mesh time:
//
//  read:  a strtod/strtol cursor over the fgets line instead of sscanf.
//         sscanf re-parses its format string and goes through the locale
//         machinery on every call, which costs ~1 us/line -- most of the
//         time spent reading a large .ans.
//
//  write: build rows into a reusable string buffer with a hand-rolled
//         integer formatter and std::to_chars for doubles, flushed in ~1 MB
//         blocks. to_chars emits the SHORTEST digit string that strtod
//         reads back to the identical double (round-trips are bit-exact),
//         which is both faster than snprintf("%.17g") and ~30-40% smaller
//         on disk. NOTE: the bytes differ from %.17g ("0.1" vs
//         "0.10000000000000001"), so byte-level file baselines change even
//         though every parsed value is identical.

#ifndef FKN_FASTIO_H
#define FKN_FASTIO_H

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <charconv>

// Parse one number at the cursor, advancing it past the token (leading
// whitespace, including the tabs the .ans uses, is skipped by strtod/strtol).
// Returns false if no number is there -- callers treat that as a malformed
// record, the same hard failure a short fgets gets.
inline bool fio_double(const char *&p, double &out)
{
	char *q = NULL;
	double v = strtod(p, &q);
	if (q == p) return false;
	out = v; p = q;
	return true;
}

inline bool fio_int(const char *&p, int &out)
{
	char *q = NULL;
	long v = strtol(p, &q, 10);
	if (q == p) return false;
	out = (int)v; p = q;
	return true;
}

inline void fio_append_int(std::string &s, int v)
{
	char tmp[16];
	char *p = tmp + sizeof(tmp);
	unsigned int u = (v < 0) ? (unsigned int)(-(v + 1)) + 1u : (unsigned int)v;
	do { *--p = (char)('0' + (int)(u % 10)); u /= 10; } while (u);
	if (v < 0) *--p = '-';
	s.append(p, tmp + sizeof(tmp) - p);
}

inline void fio_append_g(std::string &s, double v)
{
	char tmp[40];
	auto r = std::to_chars(tmp, tmp + sizeof(tmp), v);
	s.append(tmp, (size_t)(r.ptr - tmp));
}

inline void fio_flush(FILE *fp, std::string &s)
{
	if (s.size() >= (1u << 20)) {
		fwrite(s.data(), 1, s.size(), fp);
		s.clear();
	}
}

#endif
