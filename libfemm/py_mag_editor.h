// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_mag_editor.h - mi_* registration for the magnetics preprocessor.
#ifndef PY_MAG_EDITOR_H
#define PY_MAG_EDITOR_H

class MagDocument;

// Register all mi_* functions and set the active document.
void register_mag_editor_py(MagDocument *doc);

// Active magnetics document (for use by callbacks).
MagDocument *active_mag_document();

// Null the active editor doc pointer (called when a doc is closed/removed).
void deactivate_mag_editor_py();

// Clear the static results-doc pointer (called by mo_close to prevent a
// double-free when the GUI widget destructor also deletes the doc).
void clear_mag_results_doc();

#endif  // PY_MAG_EDITOR_H
