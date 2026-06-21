// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_elec_postproc.h - eo_* registration for the electrostatics postprocessor.
#ifndef PY_ELEC_POSTPROC_H
#define PY_ELEC_POSTPROC_H

class ElecResultsDoc;

void register_elec_postproc_py(ElecResultsDoc *doc);
ElecResultsDoc *active_elec_results();

#endif  // PY_ELEC_POSTPROC_H
