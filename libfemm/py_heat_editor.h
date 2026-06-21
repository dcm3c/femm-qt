// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_heat_editor.h - hi_* registration for the heat-flow preprocessor.
#ifndef PY_HEAT_EDITOR_H
#define PY_HEAT_EDITOR_H

class HeatDocument;

void register_heat_editor_py(HeatDocument *doc);
HeatDocument *active_heat_document();
void deactivate_heat_editor_py();
void clear_heat_results_doc();

#endif  // PY_HEAT_EDITOR_H
