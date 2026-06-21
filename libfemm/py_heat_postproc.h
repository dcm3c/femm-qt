// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_heat_postproc.h - ho_* registration for the heat-flow postprocessor.
#ifndef PY_HEAT_POSTPROC_H
#define PY_HEAT_POSTPROC_H

class HeatResultsDoc;

void register_heat_postproc_py(HeatResultsDoc *doc);
HeatResultsDoc *active_heat_results();

#endif  // PY_HEAT_POSTPROC_H
