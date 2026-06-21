// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_types.h — Heat flow property data structures
// Ported from femm42src/femm/hd_nosebl.h (hdrawdata namespace)
// Clean C++17, no MFC dependencies.
#ifndef HEAT_TYPES_H
#define HEAT_TYPES_H

#include <string>
#include "complex.h"

// ---------------------------------------------------------------
// Heat flow material properties
// ---------------------------------------------------------------

struct HMaterialProp {
    std::string BlockName = "New Material";
    double Kx = 1.0, Ky = 1.0;   // thermal conductivity (W/m-K)
    double Kt = 0.0;              // volumetric heat capacity (MJ/(m^3*K))
    double qv = 0.0;              // volume heat generation (W/m^3)

    // Nonlinear thermal conductivity curve
    int npts = 0;                 // number of points in T-K curve
    CComplex Kn[128];             // Kn[i].re = temperature, Kn[i].im = conductivity

    // Interpolate thermal conductivity at temperature T.
    // Returns Kx in real part, Ky in imaginary part.
    CComplex GetK(double T) const;
};

// ---------------------------------------------------------------
// Heat flow boundary properties
// ---------------------------------------------------------------

struct HBoundaryProp {
    std::string BdryName = "New Boundary";
    int BdryFormat = 0;
    // 0 = Fixed Temperature
    // 1 = Heat Flux (Robin/Mixed)
    // 2 = Convection
    // 3 = Radiation
    // 4 = Periodic
    // 5 = Antiperiodic

    double Tset = 0.0;            // fixed temperature (BdryFormat=0)
    double qs = 0.0;              // heat flux density (W/m^2)
    double Tinf = 0.0;            // external temperature for convection
    double h = 0.0;               // heat transfer coefficient (W/(m^2*K))
    double beta = 0.0;            // radiosity coefficient
    double TinfRad = 0.0;         // external temperature for radiation
};

// ---------------------------------------------------------------
// Heat flow point properties
// ---------------------------------------------------------------

struct HPointProp {
    std::string PointName = "New Point Property";
    double T = 0.0;               // prescribed nodal temperature
    double qp = 0.0;              // point heat generation (W/m)
};

// ---------------------------------------------------------------
// Heat flow conductor (circuit) properties
// ---------------------------------------------------------------

struct HCircuit {
    std::string CircName = "New Conductor";
    double T = 0.0;               // prescribed temperature
    double q = 0.0;               // total heat flux
    int CircType = 0;             // 0 = prescribed T, 1 = prescribed q
};

#endif // HEAT_TYPES_H
