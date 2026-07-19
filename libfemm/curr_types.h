// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_types.h — Current flow property data structures
// Ported from femm42src/femm/cd_nosebl.h (cdrawdata namespace)
// Clean C++17, no MFC dependencies.
#ifndef CURR_TYPES_H
#define CURR_TYPES_H

#include <string>
#include "complex.h"

// ---------------------------------------------------------------
// Current flow material properties
// ---------------------------------------------------------------

struct CurrMaterialProp {
    std::string BlockName = "New Material";
    double ox = 0.0, oy = 0.0;    // electrical conductivity (S/m)
    double ex = 1.0, ey = 1.0;    // relative permittivity
    double ltx = 0.0, lty = 0.0;  // dielectric loss tangent
};

// ---------------------------------------------------------------
// Current flow boundary properties
// ---------------------------------------------------------------

struct CurrBoundaryProp {
    std::string BdryName = "New Boundary";
    int BdryFormat = 0;
    // 0 = Fixed Voltage
    // 1 = Mixed (c0 + c1 * dV/dn)
    // 2 = Surface Current Density
    // 3 = Periodic
    // 4 = Antiperiodic

    CComplex vs;    // fixed voltage (BdryFormat=0)
    CComplex c0;    // mixed BC coefficient c0 (BdryFormat=1)
    CComplex c1;    // mixed BC coefficient c1 (BdryFormat=1)
    CComplex qs;    // surface current density (BdryFormat=2)
};

// ---------------------------------------------------------------
// Current flow point properties
// ---------------------------------------------------------------

struct CurrPointProp {
    std::string PointName = "New Point Property";
    CComplex vp;    // prescribed nodal voltage
    CComplex qp;    // point current source
};

// ---------------------------------------------------------------
// Current flow conductor (circuit) properties
// ---------------------------------------------------------------

struct CurrCircuit {
    std::string CircName = "New Conductor";
    CComplex vc;    // conductor voltage
    CComplex qc;    // conductor charge/current
    int CircType = 0;  // 0 = prescribed voltage, 1 = prescribed current
};

#endif // CURR_TYPES_H
