// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_pybridge.h - the `_pybridge` module: the transport MicroPython sees.
//
// Deliberately dumb. It moves opaque byte frames and reports lifecycle state;
// it knows nothing about ops, handles or encodings, all of which live in
// pybridge.py. Built at runtime like `os` (py_os.cpp), so it needs no
// MP_REGISTER_MODULE and no regenerated qstr headers.
#ifndef PY_PYBRIDGE_H
#define PY_PYBRIDGE_H

// Build the `_pybridge` module. Called once from init_py().
void register_pybridge_py();

#endif  // PY_PYBRIDGE_H
