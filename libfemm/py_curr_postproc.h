// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_curr_postproc.h - co_* registration for the current-flow postprocessor.
#ifndef PY_CURR_POSTPROC_H
#define PY_CURR_POSTPROC_H

class CurrResultsDoc;

void register_curr_postproc_py(CurrResultsDoc *doc);
void deactivate_curr_postproc_py();   // clear active-results binding (window closed)
CurrResultsDoc *active_curr_results();

#endif  // PY_CURR_POSTPROC_H
