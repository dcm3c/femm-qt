// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// mag_types.h — Magnetics data structures for FEMM
// Ported from femm42src/femm/nosebl.h (magnetics namespace)
// Clean C++17, no MFC dependencies.
#ifndef MAG_TYPES_H
#define MAG_TYPES_H

#include <string>
#include <vector>
#include <cmath>
#include "complex.h"

// ---------------------------------------------------------------
// Geometry primitives (shared across all physics types)
// ---------------------------------------------------------------

struct FNode {
    double x = 0.0, y = 0.0;
    bool IsSelected = false;
    std::string BoundaryMarker;
    std::string InConductor;    // conductor assignment (electrostatics/current flow)
    int InGroup = 0;
    double lfs = -1.0;  // local feature size constraint (-1 = none)

    double GetDistance(double xo, double yo) const {
        return std::sqrt((x - xo) * (x - xo) + (y - yo) * (y - yo));
    }
    CComplex CC() const { return CComplex(x, y); }
    void ToggleSelect() { IsSelected = !IsSelected; }
};

struct FSegment {
    int n0 = 0, n1 = 0;
    bool IsSelected = false;
    bool Hidden = false;
    double MaxSideLength = -1.0;  // -1 = auto
    std::string BoundaryMarker;
    std::string InConductor;      // conductor assignment (electrostatics/current flow)
    int InGroup = 0;

    void ToggleSelect() { IsSelected = !IsSelected; }
};

struct FArcSegment {
    int n0 = 0, n1 = 0;
    bool NormalDirection = true;
    bool IsSelected = false;
    bool Hidden = false;
    double MaxSideLength = 10.0;
    double ArcLength = 90.0;      // degrees
    double mySideLength = 0.0;
    std::string BoundaryMarker;
    std::string InConductor;      // conductor assignment (electrostatics/current flow)
    int InGroup = 0;

    void ToggleSelect() { IsSelected = !IsSelected; }
};

struct FBlockLabel {
    double x = 0.0, y = 0.0;
    double MaxArea = 0.0;         // 0 = auto
    double MagDir = 0.0;          // magnetization direction, degrees
    int Turns = 1;
    bool IsSelected = false;
    std::string BlockType;
    std::string InCircuit;
    std::string MagDirFctn;
    int InGroup = 0;
    bool IsExternal = false;
    bool IsDefault = false;

    double GetDistance(double xo, double yo) const {
        return std::sqrt((x - xo) * (x - xo) + (y - yo) * (y - yo));
    }
    void ToggleSelect() { IsSelected = !IsSelected; }
};

// ---------------------------------------------------------------
// Magnetics material properties
// ---------------------------------------------------------------

struct FMaterialProp {
    std::string BlockName = "New Material";
    double mu_x = 1.0, mu_y = 1.0;       // relative permeability

    int BHpoints = 0;
    std::vector<CComplex> BHdata;         // BH curve: .re = B, .im = H

    int LamType = 0;        // 0=not laminated, 1=x-dir, 2=y-dir
    double LamFill = 1.0;
    double Theta_m = 0.0;   // magnetization direction, degrees
    double H_c = 0.0;       // coercivity, A/m
    CComplex Jsrc;           // applied current density, MA/m^2
    double Cduct = 0.0;     // conductivity, MS/m
    double Lam_d = 0.0;     // lamination thickness, mm
    double Theta_hn = 0.0;  // max hysteresis angle, degrees (nonlinear)
    double Theta_hx = 0.0;  // hysteresis angle x-dir
    double Theta_hy = 0.0;  // hysteresis angle y-dir
    int NStrands = 0;
    double WireD = 0.0;     // strand diameter, mm
};

// ---------------------------------------------------------------
// Boundary condition properties
// ---------------------------------------------------------------

struct FBoundaryProp {
    std::string BdryName = "New Boundary";
    int BdryFormat = 0;
    // 0 = prescribed A, 1 = small skin depth, 2 = mixed,
    // 3 = SDI, 4 = periodic, 5 = antiperiodic,
    // 6 = periodic AGE, 7 = antiperiodic AGE

    double A0 = 0.0, A1 = 0.0, A2 = 0.0, phi = 0.0;
    double Mu = 0.0, Sig = 0.0;
    CComplex c0, c1;
    double InnerAngle = 0.0, OuterAngle = 0.0;
};

// ---------------------------------------------------------------
// Point (nodal) properties
// ---------------------------------------------------------------

struct FPointProp {
    std::string PointName = "New Point Property";
    CComplex Jp;   // applied point current, Amps
    CComplex Ap;   // prescribed nodal value of A
};

// ---------------------------------------------------------------
// Circuit properties
// ---------------------------------------------------------------

struct FCircuit {
    std::string CircName = "New Circuit";
    CComplex Amps;
    int CircType = 0;   // 0 = series, 1 = parallel
};

#endif // MAG_TYPES_H
