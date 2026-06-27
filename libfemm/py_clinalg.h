// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_clinalg.h — register complex linear-algebra functions (_clinalg_eig,
// _clinalg_qr, ...) into the femm module. Call once from init_py(), before the
// init script runs (the init script grafts these onto numpy.linalg / clinalg).

#ifndef PY_CLINALG_H
#define PY_CLINALG_H

void register_clinalg();

#endif // PY_CLINALG_H
