// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// zoom_fit.h - the one padding constant shared by every zoom-to-fit.
#pragma once

// Fraction of the model's own extent left as empty space on each side when a
// view is fitted to the window (Home, View | Zoom Natural, and the automatic
// fit when a document is first shown).
//
// Deliberate divergence from Original FEMM, which uses NO margin: both
// CFemmeView::OnZoomNatural (FemmeView.cpp:1336) and
// CFemmviewView::OnZoomNatural (FemmviewView.cpp:1651) fit the raw bounding box
// with mag = (r.right-1)/(x[1]-x[0]), so the extreme geometry lands exactly on
// the window edge and a node marker drawn there is half outside it.
//
// It lives in a header because the value has to be IDENTICAL in all three
// zoom-to-fit implementations -- EditorWidget (all four physics),
// ResultsWidget (magnetics post-processor) and ScalarResultsWidget
// (electrostatics, heat flow, current flow). It was not, until 2026-08-22: the
// editor used 0.1 and both post-processors 0.05. Since the margin is added to
// each side, that made the editor's span 1.2x the model and the
// post-processor's 1.1x, so the same geometry was drawn 9% larger the moment
// you analysed a model and switched to the results view. Nothing else about the
// two paths differed -- same min(mx, my), same (width() - 1.0), same origin.
static const double kZoomFitMargin = 0.05;
