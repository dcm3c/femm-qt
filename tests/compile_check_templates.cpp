// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// compile_check_templates.cpp - Phase-4 compile validation.
//
// Instantiates the shared editor template against the real MagDocument and
// references the postproc display registration, so the mechanical sol3->py
// translation is checked against the actual document model. Compiled with
// -fsyntax-only (no link); full instantiation of the postproc *template*
// helpers (ppSelectPoint/ppMakePlot) happens in Phase 5 with the concrete
// results-doc type.
#include "py_editor_common.h"
#include "py_postproc_common.h"
#include "mag_document.h"

static MagDocument *s_doc = nullptr;
static int s_editAction = 0;

void compile_check_instantiate() {
    register_common_editor(femm_py::femm_module, "mi", s_doc, "No magnetics input in focus", s_editAction);
    registerDisplayViewCmds(femm_py::femm_module, "mo");
    (void)ppContourType("imag");
}
