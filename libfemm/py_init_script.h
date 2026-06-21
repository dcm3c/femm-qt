// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_init_script.h - the embedded Python init script.
// Bundled as a C string and executed at the end of
// init_py(): complex-aware math, unit constants, AWG, the asymptotic-BC
// permeability tables, and the per-physics
// drawarc/drawline/drawrectangle/makeABC helpers.
#ifndef PY_INIT_SCRIPT_H
#define PY_INIT_SCRIPT_H

extern const char *PY_INIT_SCRIPT;

#endif  // PY_INIT_SCRIPT_H
