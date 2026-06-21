// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_types.h — Electrostatics property data structures
// Ported from femm42src/femm/bd_nosebl.h (beladraw namespace)
// Clean C++17, no MFC dependencies.
#ifndef ELEC_TYPES_H
#define ELEC_TYPES_H

#include <string>

// ---------------------------------------------------------------
// Electrostatics material properties
// ---------------------------------------------------------------

struct EMaterialProp {
    std::string BlockName = "New Material";
    double ex = 1.0, ey = 1.0;   // relative permittivity
    double qv = 0.0;             // volume charge density (C/m^3)
};

// ---------------------------------------------------------------
// Electrostatics boundary properties
// ---------------------------------------------------------------

struct EBoundaryProp {
    std::string BdryName = "New Boundary";
    int BdryFormat = 0;
    // 0 = Fixed Voltage
    // 1 = Mixed BC
    // 2 = Surface Charge Density
    // 3 = Periodic
    // 4 = Antiperiodic

    double V = 0.0;              // fixed voltage (BdryFormat=0)
    double qs = 0.0;             // surface charge density (BdryFormat=2)
    double c0 = 0.0, c1 = 0.0;  // mixed BC coefficients (BdryFormat=1)
};

// ---------------------------------------------------------------
// Electrostatics point properties
// ---------------------------------------------------------------

struct EPointProp {
    std::string PointName = "New Point Property";
    double V = 0.0;              // prescribed nodal voltage
    double qp = 0.0;             // point charge (C/m)
};

// ---------------------------------------------------------------
// Electrostatics conductor (circuit) properties
// ---------------------------------------------------------------

struct ECircuit {
    std::string CircName = "New Conductor";
    double V = 0.0;              // prescribed voltage
    double q = 0.0;              // total charge
    int CircType = 0;            // 0 = prescribed V, 1 = prescribed q
};

#endif // ELEC_TYPES_H
