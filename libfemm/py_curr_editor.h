// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_curr_editor.h - ci_* registration for the current-flow preprocessor.
#ifndef PY_CURR_EDITOR_H
#define PY_CURR_EDITOR_H

class CurrDocument;

void register_curr_editor_py(CurrDocument *doc);
CurrDocument *active_curr_document();
void deactivate_curr_editor_py();
void clear_curr_results_doc();

#endif  // PY_CURR_EDITOR_H
