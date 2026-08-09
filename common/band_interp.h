// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker

// band_interp.h -- selects the sliding-band interpolation order.
//
// QUINTIC defined (the default): band-element corner values are
// interpolated from the mesh edge nodes with quintic Hermite weights whose
// endpoint second derivatives are rolled up from second central
// differences. Flux linkage, BEMF slope, and torque become C2 in rotor
// position (the cubic band is C1), cutting element-transition artifacts:
// measured on the sliding-band paper's BLDC, torque kinks drop 1.7x rms /
// 3.5x max and flux-linkage kinks 1.9-2.6x rms / 3.7-5.6x max, while the
// values themselves move by only ~0.05% -- the in-interval interpolation
// error constant is ~1.5x the cubic's, the price of the extra smoothness.
// Same 5-node stencils, same 10x10 element matrix; only the weight
// polynomials differ. Derivation and A/B measurements:
// docs/eccentric-sliding-band.md, appendices A and B, and
// QuinticInterpolationToNodesOfBastardElement.nb.
//
// QUINTIC undefined: the original C1 cubic (Catmull-Rom) band, kept while
// the quintic accumulates field experience. Bit-identical to the
// pre-switch solver on every fixture.
//
// This header is the ONLY place the choice may be made. It is included by
// the solvers' element-matrix code (prob1big.cpp, prob2big.cpp) AND the
// postprocessor's gap-field reconstruction (libfemm/results_doc.cpp,
// ComputeAGEHarmonics); the two MUST always agree -- a mismatched pair
// reconstructs a field the solver's energy functional never saw, and the
// DFT-based torque/force sums then pick up scheme-mismatch noise several
// times larger than the artifact the quintic removes.

#ifndef FEMM_BAND_INTERP_H
#define FEMM_BAND_INTERP_H

#define QUINTIC 1

#endif
