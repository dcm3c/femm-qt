// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_types.cpp — Heat flow material GetK() implementation
// Ported from femm42src/femm/hv_problem.h (CMaterialProp::GetK)

#include "heat_types.h"

// Interpolate thermal conductivity at temperature T.
// For nonlinear materials, the T-K curve is stored in Kn[]:
//   Kn[i].re = temperature, Kn[i].im = conductivity at that temperature
// Returns isotropic conductivity: real = Kx, imag = Ky.
// For linear materials (npts==0), returns the constant Kx,Ky values.

CComplex HMaterialProp::GetK(double T) const
{
    // Linear material — just return the constant conductivities
    if (npts == 0) return CComplex(Kx, Ky);

    // Single-point curve — isotropic at that conductivity
    if (npts == 1) return CComplex(Kn[0].im, Kn[0].im);

    // Below the first data point — clamp to first value
    if (T <= Kn[0].re) return CComplex(Kn[0].im, Kn[0].im);

    // Above the last data point — clamp to last value
    if (T >= Kn[npts - 1].re) return CComplex(Kn[npts - 1].im, Kn[npts - 1].im);

    // Find the interval and linearly interpolate
    for (int i = 0, j = 1; j < npts; i++, j++)
    {
        if ((T >= Kn[i].re) && (T <= Kn[j].re))
        {
            double k = Kn[i].im + (Kn[j].im - Kn[i].im) *
                       (T - Kn[i].re) / (Kn[j].re - Kn[i].re);
            return CComplex(k, k);
        }
    }

    // Fallback (should not be reached)
    return CComplex(Kx, Ky);
}
