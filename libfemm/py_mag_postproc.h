// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_mag_postproc.h - mo_* registration for the magnetics postprocessor.
#ifndef PY_MAG_POSTPROC_H
#define PY_MAG_POSTPROC_H

class ResultsDoc;

// Register all mo_* functions and set the active results document.
void register_mag_postproc_py(ResultsDoc *doc);

// Active results document.
ResultsDoc *active_results_document();

#endif  // PY_MAG_POSTPROC_H
