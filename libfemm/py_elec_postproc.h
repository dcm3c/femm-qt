// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_elec_postproc.h - eo_* registration for the electrostatics postprocessor.
#ifndef PY_ELEC_POSTPROC_H
#define PY_ELEC_POSTPROC_H

class ElecResultsDoc;

void register_elec_postproc_py(ElecResultsDoc *doc);
void deactivate_elec_postproc_py();   // clear active-results binding (window closed)
ElecResultsDoc *active_elec_results();

#endif  // PY_ELEC_POSTPROC_H
