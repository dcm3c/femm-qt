// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_elec_editor.h - ei_* registration for the electrostatics preprocessor.
#ifndef PY_ELEC_EDITOR_H
#define PY_ELEC_EDITOR_H

class ElecDocument;

void register_elec_editor_py(ElecDocument *doc);
ElecDocument *active_elec_document();
void deactivate_elec_editor_py();
void clear_elec_results_doc();
class ElecResultsDoc;   // eo_reload's swap: see replace_mag_results_doc
void replace_elec_results_doc(ElecResultsDoc *oldDoc, ElecResultsDoc *newDoc);

#endif  // PY_ELEC_EDITOR_H
