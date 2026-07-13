// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// results_doc.cpp — Magnetics postprocessor solution document
// Reads .ans files and performs full post-load processing.
// Ported from femm42src/femm/FemmviewDoc.cpp

#include "results_doc.h"
#include "utf8_io.h"
#include "fknDlg.h"   // CFknDlg (needed for MakeMask PCG solver progress)
#include "spars.h"     // CBigLinProb (sparse matrix for MakeMask Laplace solve)
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif

static double sqr(double x) { return x * x; }

static const double LengthConvTable[6] = {
    0.0254,   // inches
    0.001,    // mm
    0.01,     // cm
    1.0,      // meters
    2.54e-05, // mils
    1.e-06    // microns
};

double ResultsDoc::LengthConv() const
{
    if (LengthUnits >= 0 && LengthUnits < 6)
        return LengthConvTable[LengthUnits];
    return 0.001;
}

// --- File parsing helpers ---
static char *StripKey(char *s)
{
    char *p = strchr(s, '=');
    if (p) return p + 1;
    return s;
}

static std::string ExtractQuoted(char *v)
{
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

ResultsDoc::~ResultsDoc() {}

// ================================================================
// Material B-H curve methods
// ================================================================

double RMaterialProp::GetH(double b) const
{
    if (BHpoints == 0) return b / (mu_x * muo);
    if (b <= 0) return 0;

    // Linear interpolation on BH curve
    // BHdata stores (B, H) pairs sorted by B
    for (int i = 0; i < BHpoints - 1; i++) {
        if (b >= Bdata[i] && b <= Bdata[i + 1]) {
            double t = (b - Bdata[i]) / (Bdata[i + 1] - Bdata[i]);
            return Hdata[i].re + t * (Hdata[i + 1].re - Hdata[i].re);
        }
    }
    // Extrapolate beyond curve
    if (BHpoints >= 2) {
        int n = BHpoints - 1;
        double dHdB = (Hdata[n].re - Hdata[n - 1].re) /
                       (Bdata[n] - Bdata[n - 1]);
        return Hdata[n].re + dHdB * (b - Bdata[n]);
    }
    return b / (mu_x * muo);
}

CComplex RMaterialProp::GetH(CComplex x) const
{
    // Cubic-Hermite interpolation of the (possibly complex) B-H curve, using the
    // spline slopes from GetSlopes. Complex because the apparent AC curve carries
    // hysteresis + lamination-eddy phase. Only meaningful for nonlinear materials
    // (the linear case uses mu_fdx in GetMu). Ported from OF Problem.cpp.
    double b = abs(x);
    if (BHpoints == 0 || b == 0) return CComplex(0);
    CComplex p = x / b;   // unit direction of B

    if (b > Bdata[BHpoints - 1])
        return p * (Hdata[BHpoints - 1] +
                    slope[BHpoints - 1] * (b - Bdata[BHpoints - 1]));

    for (int i = 0; i < BHpoints - 1; i++) {
        if (b >= Bdata[i] && b <= Bdata[i + 1]) {
            double l = Bdata[i + 1] - Bdata[i];
            double z = (b - Bdata[i]) / l;
            double z2 = z * z;
            CComplex h = (1.0 - 3.0*z2 + 2.0*z2*z) * Hdata[i] +
                         z * (1.0 - 2.0*z + z2) * l * slope[i] +
                         z2 * (3.0 - 2.0*z) * Hdata[i + 1] +
                         z2 * (z - 1.0) * l * slope[i + 1];
            return p * h;
        }
    }
    return CComplex(0);
}

void RMaterialProp::GetMu(double b1, double b2, double &mu1, double &mu2) const
{
    if (BHpoints == 0) {
        // Linear material
        if (LamType == 0) {
            mu1 = mu_x * LamFill + (1.0 - LamFill);
            mu2 = mu_y * LamFill + (1.0 - LamFill);
        }
        else if (LamType == 1) {
            mu1 = mu_x * LamFill + (1.0 - LamFill);
            mu2 = mu_y / (LamFill + mu_y * (1.0 - LamFill));
        }
        else if (LamType == 2) {
            mu1 = mu_x / (LamFill + mu_x * (1.0 - LamFill));
            mu2 = mu_y * LamFill + (1.0 - LamFill);
        }
        else {
            mu1 = mu_x;
            mu2 = mu_y;
        }
        return;
    }

    // Nonlinear material
    double b, h;
    if (LamType == 0) {
        b = sqrt(b1 * b1 + b2 * b2);
    }
    else if (LamType == 1) {
        b = sqrt(sqr(b1 / LamFill) + b2 * b2);
    }
    else {
        b = sqrt(b1 * b1 + sqr(b2 / LamFill));
    }

    if (b < 1e-20) {
        mu1 = mu_x;
        mu2 = mu_y;
        return;
    }

    h = GetH(b);
    double mu_iron = b / (muo * h);
    if (mu_iron < 1) mu_iron = 1;

    if (LamType == 0) {
        mu1 = mu_iron * LamFill + (1.0 - LamFill);
        mu2 = mu1;
    }
    else if (LamType == 1) {
        mu1 = mu_iron * LamFill + (1.0 - LamFill);
        mu2 = mu_iron / (LamFill + mu_iron * (1.0 - LamFill));
    }
    else {
        mu1 = mu_iron / (LamFill + mu_iron * (1.0 - LamFill));
        mu2 = mu_iron * LamFill + (1.0 - LamFill);
    }
}

void RMaterialProp::GetMu(CComplex b1, CComplex b2, CComplex &mu1, CComplex &mu2) const
{
    if (BHpoints == 0) {
        mu1 = mu_fdx;
        mu2 = mu_fdy;
        return;
    }
    // Nonlinear: read the permeability off the (complex) apparent AC B-H curve
    // built by GetSlopes. Ported from OF Problem.cpp::GetMu(CComplex).
    mu1 = mu2 = CComplex(muo, 0);
    CComplex biron, muiron;
    if (LamType == 0) {          // in-plane (lamination effect already in the curve)
        biron = sqrt(b1*conj(b1) + b2*conj(b2));
        if (abs(biron) < 1.e-8) mu1 = 1.0 / slope[0];
        else mu1 = biron / GetH(biron);
        mu2 = mu1;
    }
    else if (LamType == 1) {     // laminated parallel to x
        biron = sqrt((b1/LamFill)*(b1/LamFill) + b2*b2);
        if (abs(biron) < 1.e-8) muiron = 1.0 / slope[0];
        else muiron = biron / GetH(biron);
        mu1 = muiron * LamFill;
        mu2 = 1.0 / (LamFill/muiron + (1.0 - LamFill)/muo);
    }
    else if (LamType == 2) {     // laminated parallel to y
        biron = sqrt((b2/LamFill)*(b2/LamFill) + b1*b1);
        if (abs(biron) < 1.e-8) muiron = 1.0 / slope[0];
        else muiron = biron / GetH(biron);
        mu2 = muiron * LamFill;
        mu1 = 1.0 / (LamFill/muiron + (1.0 - LamFill)/muo);
    }
    // relative permeability
    mu1 /= muo;
    mu2 /= muo;
}

void RMaterialProp::GetSlopes(double omega)
{
    // Compute cubic Hermite spline slopes for BH interpolation.
    // Ported from OF Problem.cpp. Natural BC tridiagonal solve
    // with monotonicity-enforcing smoothing.
    if (BHpoints < 2) return;
    if (!slope.empty()) return;  // already computed

    int n = BHpoints;
    slope.resize(n);

    // Extract initial permeability from first data point
    if (n >= 2 && abs(Hdata[1]) > 0) {
        mu_x = Bdata[1] / (muo * abs(Hdata[1]));
        mu_y = mu_x;
    }

    // Frequency convolution: for AC problems, convolve BH curve with
    // a sinusoidal excitation to get the effective fundamental-harmonic
    // BH curve, then apply complex permeability for hysteresis loss
    // (O'Kelly approximation). Ported from OF Problem.cpp.
    if (omega != 0) {
        std::vector<double> bn(n);
        std::vector<CComplex> hn(n);
        double mumax = 0;
        for (int i = 1; i < n; i++) {
            hn[i] = Hdata[i];
            bn[i] = 0;
            for (int k = 1; k <= i; k++) {
                bn[i] += Re((4.0*(Hdata[k]*Bdata[k-1] -
                    Hdata[k-1]*Bdata[k])*(-cos((Hdata[k-1]*PI)/(2.0*Hdata[i])) +
                    cos((Hdata[k]*PI)/(2.0*Hdata[i]))) + (-Bdata[k-1] +
                    Bdata[k])*((Hdata[k-1] - Hdata[k])*PI +
                    Hdata[i]*(-sin((Hdata[k-1]*PI)/Hdata[i]) +
                    sin((Hdata[k]*PI)/Hdata[i])))) / ((Hdata[k-1] - Hdata[k])*PI));
            }
        }
        for (int i = 1; i < n; i++) {
            Bdata[i] = bn[i];
            Hdata[i] = hn[i];
            double munow = Re(Bdata[i] / Hdata[i]);
            if (munow > mumax) mumax = munow;
        }
        // Apply complex permeability (O'Kelly kludge for hysteresis loss)
        for (int i = 1; i < n; i++) {
            Hdata[i] *= exp(CComplex(0,1) * Bdata[i] * Theta_hn * PI / (180.0 * Hdata[i] * mumax));
        }
        MuMax = mumax / muo;
    }

    // Tridiagonal system for natural cubic spline slopes
    // Using direct Gauss elimination on a dense system (BHpoints is small)
    std::vector<std::vector<CComplex>> M(n, std::vector<CComplex>(n, CComplex(0, 0)));
    std::vector<CComplex> rhs(n, CComplex(0, 0));

    bool curveOK = false;
    bool processedLams = false;
    while (!curveOK) {
        // Clear
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) M[i][j] = CComplex(0, 0);
            rhs[i] = CComplex(0, 0);
        }

        double l1 = Bdata[1] - Bdata[0];
        M[0][0] = 4.0 / l1;
        M[0][1] = 2.0 / l1;
        rhs[0] = 6.0 * (Hdata[1] - Hdata[0]) / (l1 * l1);

        l1 = Bdata[n - 1] - Bdata[n - 2];
        M[n - 1][n - 1] = 4.0 / l1;
        M[n - 1][n - 2] = 2.0 / l1;
        rhs[n - 1] = 6.0 * (Hdata[n - 1] - Hdata[n - 2]) / (l1 * l1);

        for (int i = 1; i < n - 1; i++) {
            l1 = Bdata[i] - Bdata[i - 1];
            double l2 = Bdata[i + 1] - Bdata[i];
            M[i][i - 1] = 2.0 / l1;
            M[i][i] = 4.0 * (l1 + l2) / (l1 * l2);
            M[i][i + 1] = 2.0 / l2;
            rhs[i] = 6.0 * (Hdata[i] - Hdata[i - 1]) / (l1 * l1) +
                     6.0 * (Hdata[i + 1] - Hdata[i]) / (l2 * l2);
        }

        // Gauss elimination
        for (int i = 0; i < n - 1; i++) {
            CComplex pivot = M[i + 1][i] / M[i][i];
            M[i + 1][i] = CComplex(0, 0);
            M[i + 1][i + 1] = M[i + 1][i + 1] - pivot * M[i][i + 1];
            rhs[i + 1] = rhs[i + 1] - pivot * rhs[i];
        }
        // Back substitution
        rhs[n - 1] = rhs[n - 1] / M[n - 1][n - 1];
        for (int i = n - 2; i >= 0; i--)
            rhs[i] = (rhs[i] - M[i][i + 1] * rhs[i + 1]) / M[i][i];
        for (int i = 0; i < n; i++) slope[i] = rhs[i];

        // Monotonicity check (on real part)
        curveOK = true;
        for (int i = 1; i < n; i++) {
            double d0 = slope[i - 1].re, d1 = slope[i].re;
            double u0 = Hdata[i - 1].re, u1 = Hdata[i].re;
            double L = Bdata[i] - Bdata[i - 1];
            double c0 = d0;
            double c1 = -(2.0 * (2.0*d0*L + d1*L + 3.0*u0 - 3.0*u1)) / (L*L);
            double c2 = (3.0 * (d0*L + d1*L + 2.0*u0 - 2.0*u1)) / (L*L*L);
            double disc = c1*c1 - 4.0*c0*c2;
            double X0 = -1, X1 = -1;
            if (c2 == 0) { if (c1 != 0) X0 = -c0/c1; }
            else if (disc > 0) {
                double sd = sqrt(disc);
                X0 = -(c1 + sd) / (2.0 * c2);
                X1 = (-c1 + sd) / (2.0 * c2);
            }
            if ((X0 >= 0 && X0 <= L) || (X1 >= 0 && X1 <= L))
                curveOK = false;
        }

        if (!curveOK) {
            // 3-point moving average smoothing
            std::vector<double> bn(n);
            std::vector<CComplex> hn(n);
            for (int i = 1; i < n - 1; i++) {
                bn[i] = (Bdata[i-1] + Bdata[i] + Bdata[i+1]) / 3.0;
                hn[i] = (Hdata[i-1] + Hdata[i] + Hdata[i+1]) / 3.0;
            }
            for (int i = 1; i < n - 1; i++) { Bdata[i] = bn[i]; Hdata[i] = hn[i]; }
        }
        else if (!processedLams) {
            // Rewrite the B-H curve into its apparent AC form: first the
            // lamination eddy-current effect (a 1-D FE solve per point), then the
            // in-plane fill-factor mix. Both make Hdata complex, so GetH/GetMu
            // yield a complex permeability; the slopes are then re-solved on the
            // modified curve. Ported from OF Problem.cpp::GetSlopes.
            if (omega != 0 && Lam_d != 0 && Cduct != 0) {
                std::vector<double> bn(n);
                std::vector<CComplex> hn(n);
                for (int i = 1; i < n; i++) {
                    CComplex mu = LaminatedBH(omega, i);
                    bn[i] = abs(mu * Hdata[i]);
                    hn[i] = bn[i] / mu;
                }
                for (int i = 1; i < n; i++) { Bdata[i] = bn[i]; Hdata[i] = hn[i]; }
                curveOK = false;   // re-solve slopes on the apparent curve
            }
            if (LamType == 0 && LamFill != 1) {
                for (int i = 1; i < n; i++) {
                    CComplex mu = LamFill * Bdata[i] / Hdata[i] + (1.0 - LamFill) * muo;
                    Bdata[i] = abs(mu * Hdata[i]);
                    Hdata[i] = Bdata[i] / mu;
                }
                curveOK = false;
            }
            processedLams = true;
        }
    }
}

CComplex RMaterialProp::LaminatedBH(double w, int i) const
{
    // 1-D nonlinear complex FE solve across a half-lamination to find the
    // apparent AC permeability at B-H point i (accounts for eddy currents inside
    // the lamination). Ported from OF Problem.cpp::LaminatedBH.
    const int ElementsPerSkinDepth = 10;
    const CComplex J(0, 1);

    CComplex mu = Bdata[i] / Hdata[i];
    double o = Cduct * 1.e6;                  // conductivity, S/m
    double d = (Lam_d * 0.001) / 2.0;         // half lamination thickness, m
    double ds = sqrt(2.0 / (w * o * abs(mu)));// skin depth at the surface
    int n = ElementsPerSkinDepth * ((int)ceil(d / ds));
    double L = d / (double)n;                 // element size

    std::vector<CComplex> x(n + 1, CComplex(0, 0));
    std::vector<CComplex> b(n + 1, CComplex(0, 0));
    std::vector<CComplex> m0(n + 1, CComplex(0, 0));
    std::vector<CComplex> m1(n + 1, CComplex(0, 0));

    bool converged = false;
    int iter = 0;
    double res = 0, lastres = 0, Relax = 1;
    do {
        for (int k = 0; k <= n; k++) { m0[k] = 0; m1[k] = 0; b[k] = 0; }

        // assemble the FE matrix; reluctivity vi/vo from the local B-H slope
        for (int k = 0; k < n; k++) {
            CComplex vi, vo;
            if (iter != 0) {
                double B = abs(x[k + 1] - x[k]) / L;
                vi = GetdHdB(B);
                vo = GetH(CComplex(B)) / B;
            } else {
                vo = 1.0 / mu;
                vi = 1.0 / mu;
            }
            CComplex Md = ( (vi + vo) / (2.0 * L) + J * w * o * L / 4.0 );
            CComplex Mo = ( -(vi + vo) / (2.0 * L) + J * w * o * L / 4.0 );
            m0[k]     += Md;
            m1[k]     += Mo;
            m0[k + 1] += Md;

            Md = ( (vi - vo) / (2.0 * L) );
            Mo = ( -(vi - vo) / (2.0 * L) );
            b[k]     += (Md * x[k] + Mo * x[k + 1]);
            b[k + 1] += (Mo * x[k] + Md * x[k + 1]);
        }

        // boundary conditions
        m1[0] = 0;
        b[0]  = 0;
        b[n] += Hdata[i];

        // tridiagonal solve (result lands in b)
        for (int k = 0; k < n; k++) {
            CComplex c = m1[k] / m0[k];
            m0[k + 1] -= m1[k] * c;
            b[k + 1]  -= b[k] * c;
        }
        b[n] = b[n] / m0[n];
        for (int k = n - 1; k >= 0; k--)
            b[k] = (b[k] - m1[k] * b[k + 1]) / m0[k];

        iter++;
        lastres = res;
        res = abs(b[n] - x[n]) / d;
        if (res < 1.e-8) converged = true;

        // same relaxation scheme as the solver so this converges
        if (iter > 5) {
            if ((res > lastres) && (Relax > 0.1)) Relax /= 2.0;
            else Relax += 0.1 * (1.0 - Relax);
        }
        for (int k = 0; k <= n; k++) x[k] = Relax * b[k] + (1.0 - Relax) * x[k];
        if (iter > 5000) break;   // safety: never hang the postproc on load
    } while (!converged);

    return x[n] / (Hdata[i] * d);
}

// ================================================================
// ResultsDoc field computation wrappers
// ================================================================

void ResultsDoc::GetMu(double b1, double b2, double &mu1, double &mu2, int k)
{
    blockproplist[meshelem[k].blk].GetMu(b1, b2, mu1, mu2);
}

void ResultsDoc::GetMu(CComplex b1, CComplex b2, CComplex &mu1, CComplex &mu2, int k)
{
    if (blockproplist[meshelem[k].blk].LamType > 2) {
        mu1 = labellist[meshelem[k].lbl].mu;
        mu2 = mu1;
    } else {
        blockproplist[meshelem[k].blk].GetMu(b1, b2, mu1, mu2);
    }
}

void ResultsDoc::GetH(double b1, double b2, double &h1, double &h2, int k)
{
    double mu1, mu2;
    GetMu(b1, b2, mu1, mu2, k);
    h1 = b1 / (mu1 * muo);
    h2 = b2 / (mu2 * muo);
    // Shift H for permanent magnets (only when ShiftH is enabled)
    if (ShiftH && blockproplist[meshelem[k].blk].H_c != 0) {
        double Hc_re = blockproplist[meshelem[k].blk].H_c *
            cos(meshelem[k].magdir * PI / 180.0);
        double Hc_im = blockproplist[meshelem[k].blk].H_c *
            sin(meshelem[k].magdir * PI / 180.0);
        h1 -= Hc_re;
        h2 -= Hc_im;
    }
}

void ResultsDoc::GetH(CComplex b1, CComplex b2, CComplex &h1, CComplex &h2, int k)
{
    CComplex mu1, mu2;
    GetMu(b1, b2, mu1, mu2, k);
    h1 = b1 / (mu1 * muo);
    h2 = b2 / (mu2 * muo);
}

// ================================================================
// Element centroid
// ================================================================

CComplex ResultsDoc::Ctr(int e) const
{
    double x = 0, y = 0;
    for (int i = 0; i < 3; i++) {
        x += meshnode[meshelem[e].p[i]].x;
        y += meshnode[meshelem[e].p[i]].y;
    }
    return CComplex(x / 3.0, y / 3.0);
}

// ================================================================
// GetElementB — compute B from A for each element
// Ported from FemmviewDoc.cpp:2408-2518
// ================================================================

void ResultsDoc::GetElementB(RElement &elm)
{
    int n[3];
    double b[3], c[3], da;

    for (int i = 0; i < 3; i++) n[i] = elm.p[i];

    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    if (ProblemType == 0) {
        // Planar case
        elm.B1 = CComplex(0, 0);
        elm.B2 = CComplex(0, 0);
        for (int i = 0; i < 3; i++) {
            elm.B1 = elm.B1 + meshnode[n[i]].A * c[i] / (da * LengthConv());
            elm.B2 = elm.B2 - meshnode[n[i]].A * b[i] / (da * LengthConv());
        }
        if (bIncremental) {
            elm.B1p = elm.B2p = 0;
            for (int i = 0; i < 3; i++) {
                elm.B1p += meshnode[n[i]].Aprev * c[i] / (da * LengthConv());
                elm.B2p -= meshnode[n[i]].Aprev * b[i] / (da * LengthConv());
            }
        }
    }
    else {
        // Axisymmetric case — 6-node quadratic element
        CComplex v[6], dp, dq;
        double R[3], r;

        for (int i = 0; i < 3; i++) {
            R[i] = meshnode[n[i]].x;
        }
        r = (R[0] + R[1] + R[2]) / 3.0;

        // Corner nodes
        v[0] = meshnode[n[0]].A;
        v[2] = meshnode[n[1]].A;
        v[4] = meshnode[n[2]].A;

        // Mid-side nodes (constructed from corner values)
        if (R[0] < 1e-6 && R[1] < 1e-6) v[1] = (v[0] + v[2]) / 2.0;
        else v[1] = (R[1] * (3.0 * v[0] + v[2]) + R[0] * (v[0] + 3.0 * v[2])) /
                     (4.0 * (R[0] + R[1]));

        if (R[1] < 1e-6 && R[2] < 1e-6) v[3] = (v[2] + v[4]) / 2.0;
        else v[3] = (R[2] * (3.0 * v[2] + v[4]) + R[1] * (v[2] + 3.0 * v[4])) /
                     (4.0 * (R[1] + R[2]));

        if (R[2] < 1e-6 && R[0] < 1e-6) v[5] = (v[4] + v[0]) / 2.0;
        else v[5] = (R[0] * (3.0 * v[4] + v[0]) + R[2] * (v[4] + 3.0 * v[0])) /
                     (4.0 * (R[2] + R[0]));

        dp = (-v[0] + v[2] + 4.0 * v[3] - 4.0 * v[5]) / 3.0;
        dq = (-v[0] - 4.0 * v[1] + 4.0 * v[3] + v[4]) / 3.0;

        double da2 = da * 2.0 * PI * r * LengthConv() * LengthConv();
        elm.B1 = -(c[1] * dp + c[2] * dq) / da2;
        elm.B2 =  (b[1] * dp + b[2] * dq) / da2;

        if (bIncremental) {
            v[0] = meshnode[n[0]].Aprev;
            v[2] = meshnode[n[1]].Aprev;
            v[4] = meshnode[n[2]].Aprev;

            if (R[0] < 1e-6 && R[1] < 1e-6) v[1] = (v[0] + v[2]) / 2.0;
            else v[1] = (R[1] * (3.0 * v[0] + v[2]) + R[0] * (v[0] + 3.0 * v[2])) /
                         (4.0 * (R[0] + R[1]));
            if (R[1] < 1e-6 && R[2] < 1e-6) v[3] = (v[2] + v[4]) / 2.0;
            else v[3] = (R[2] * (3.0 * v[2] + v[4]) + R[1] * (v[2] + 3.0 * v[4])) /
                         (4.0 * (R[1] + R[2]));
            if (R[2] < 1e-6 && R[0] < 1e-6) v[5] = (v[4] + v[0]) / 2.0;
            else v[5] = (R[0] * (3.0 * v[4] + v[0]) + R[2] * (v[4] + 3.0 * v[0])) /
                         (4.0 * (R[2] + R[0]));

            dp = (-v[0] + v[2] + 4.0 * v[3] - 4.0 * v[5]) / 3.0;
            dq = (-v[0] - 4.0 * v[1] + 4.0 * v[3] + v[4]) / 3.0;

            double da3 = (b[0] * c[1] - b[1] * c[0]) * 2.0 * PI * r * LengthConv() * LengthConv();
            elm.B1p = Re(-(c[1] * dp + c[2] * dq) / da3);
            elm.B2p = Re( (b[1] * dp + b[2] * dq) / da3);
        } else {
            elm.B1p = 0;
            elm.B2p = 0;
        }
    }
}

// ================================================================
// GetNodalB — smoothed nodal flux density via patch averaging
// Ported from FemmviewDoc.cpp:2168-2406
// ================================================================

void ResultsDoc::GetNodalB(CComplex *b1out, CComplex *b2out, RElement &elm)
{
    // Ported from OF FemmviewDoc.cpp GetNodalB — full boundary handling.
    double lc = LengthConv();

    for (int i = 0; i < 3; i++) {
        int k = elm.p[i];
        CComplex p(meshnode[k].x, meshnode[k].y);
        b1out[i] = CComplex(0, 0);
        b2out[i] = CComplex(0, 0);

        // Count how many connected elements share the same material region
        int m = 0;
        for (int j = 0; j < NumList[k]; j++) {
            int ej = ConList[k][j];
            if (elm.lbl == meshelem[ej].lbl) m++;
            else if (Frequency == 0) {
                if (blockproplist[elm.blk].mu_x == blockproplist[meshelem[ej].blk].mu_x &&
                    blockproplist[elm.blk].mu_y == blockproplist[meshelem[ej].blk].mu_y &&
                    blockproplist[elm.blk].H_c == blockproplist[meshelem[ej].blk].H_c &&
                    elm.magdir == meshelem[ej].magdir)
                    m++;
                else if (elm.blk == meshelem[ej].blk && elm.magdir == meshelem[ej].magdir)
                    m++;
            }
            else if (blockproplist[elm.blk].mu_fdx == blockproplist[meshelem[ej].blk].mu_fdx &&
                     blockproplist[elm.blk].mu_fdy == blockproplist[meshelem[ej].blk].mu_fdy)
                m++;
        }

        if (m == NumList[k]) {
            // All connected elements share same material — inverse distance weighted average
            double R = 0;
            for (int j = 0; j < NumList[k]; j++) {
                int ej = ConList[k][j];
                double z = 1.0 / abs(p - Ctr(ej));
                R += z;
                b1out[i] = b1out[i] + meshelem[ej].B1 * z;
                b2out[i] = b2out[i] + meshelem[ej].B2 * z;
            }
            b1out[i] = b1out[i] / R;
            b2out[i] = b2out[i] / R;
        }
        else {
            // At a material interface — scan CCW and CW for interface edges
            // and compute B from normal (dA/ds) and tangential components.
            double R = 0;
            CComplex v1(0, 0), v2(0, 0);

            // Scan CCW
            RElement *e = &elm;
            for (int q = 0; q < NumList[k]; q++) {
                // Find CCW side of the element
                int pt = -1;
                for (int j = 0; j < 3; j++) if (e->p[j] == k) pt = j;
                pt--; if (pt < 0) pt = 2;
                pt = e->p[pt];

                // Find element adjacent to this side
                int nxt = -1;
                for (int j = 0; j < NumList[k]; j++) {
                    if (&meshelem[ConList[k][j]] != e) {
                        for (int l = 0; l < 3; l++)
                            if (meshelem[ConList[k][j]].p[l] == pt)
                                nxt = ConList[k][j];
                    }
                }

                if (nxt == -1) {
                    // Special-case punt (mesh boundary)
                    b1out[i] = e->B1;
                    b2out[i] = e->B2;
                    v1 = CComplex(1, 0); v2 = CComplex(1, 0);
                    break;
                }
                else if (elm.lbl != meshelem[nxt].lbl) {
                    // Found interface — compute B from edge dA and element Bt
                    CComplex tn(meshnode[pt].x - meshnode[k].x,
                                meshnode[pt].y - meshnode[k].y);
                    double r = (meshnode[pt].x + meshnode[k].x) * lc / 2.0;
                    CComplex bn = (meshnode[pt].A - meshnode[k].A) /
                                  (abs(tn) * lc);
                    if (ProblemType == 1) bn /= (-2.0 * PI * r);
                    double z = 0.5 / abs(tn);
                    tn = tn / abs(tn);

                    CComplex bt = e->B1 * tn.re + e->B2 * tn.im;
                    R += z;
                    b1out[i] = b1out[i] + z * tn.re * bt;
                    b2out[i] = b2out[i] + z * tn.im * bt;
                    b1out[i] = b1out[i] + z * tn.im * bn;
                    b2out[i] = b2out[i] - z * tn.re * bn;
                    v1 = tn;
                    break;
                }
                else e = &meshelem[nxt];
            }

            // Scan CW (only if v2 hasn't been set by the punt case)
            if (abs(v2) == 0) {
                e = &elm;
                for (int q = 0; q < NumList[k]; q++) {
                    int pt = -1;
                    for (int j = 0; j < 3; j++) if (e->p[j] == k) pt = j;
                    pt++; if (pt > 2) pt = 0;
                    pt = e->p[pt];

                    int nxt = -1;
                    for (int j = 0; j < NumList[k]; j++) {
                        if (&meshelem[ConList[k][j]] != e) {
                            for (int l = 0; l < 3; l++)
                                if (meshelem[ConList[k][j]].p[l] == pt)
                                    nxt = ConList[k][j];
                        }
                    }

                    if (nxt == -1) {
                        b1out[i] = e->B1;
                        b2out[i] = e->B2;
                        v1 = CComplex(1, 0); v2 = CComplex(1, 0);
                        break;
                    }
                    else if (elm.lbl != meshelem[nxt].lbl) {
                        CComplex tn(meshnode[pt].x - meshnode[k].x,
                                    meshnode[pt].y - meshnode[k].y);
                        double r = (meshnode[pt].x + meshnode[k].x) * lc / 2.0;
                        CComplex bn = (meshnode[pt].A - meshnode[k].A) /
                                      (abs(tn) * lc);
                        if (ProblemType == 1) bn /= (-2.0 * PI * r);
                        double z = 0.5 / abs(tn);
                        tn = tn / abs(tn);

                        CComplex bt = e->B1 * tn.re + e->B2 * tn.im;
                        R += z;
                        b1out[i] = b1out[i] + z * tn.re * bt;
                        b2out[i] = b2out[i] + z * tn.im * bt;
                        b1out[i] = b1out[i] + z * tn.im * bn;
                        b2out[i] = b2out[i] - z * tn.re * bn;
                        v2 = tn;
                        break;
                    }
                    else e = &meshelem[nxt];
                }
                if (R > 0) {
                    b1out[i] = b1out[i] / R;
                    b2out[i] = b2out[i] / R;
                }
            }

            // Check if corner is too sharp — if so, fall back to max |B|
            // from same-material elements, scaled by element B direction
            bool useCornerFix = true;
            if (abs(v1) < 0.9 || abs(v2) < 0.9) useCornerFix = false;
            if ((-v1.re * v2.re - v1.im * v2.im) > 0.985) useCornerFix = false;

            if (useCornerFix) {
                CComplex bmax(0, 0);
                for (int j = 0; j < NumList[k]; j++) {
                    if (elm.lbl == meshelem[ConList[k][j]].lbl) {
                        int mj = ConList[k][j];
                        CComplex bt;
                        bt.re = sqrt(meshelem[mj].B1.re * meshelem[mj].B1.re +
                                     meshelem[mj].B2.re * meshelem[mj].B2.re);
                        bt.im = sqrt(meshelem[mj].B1.im * meshelem[mj].B1.im +
                                     meshelem[mj].B2.im * meshelem[mj].B2.im);
                        if (bt.re > bmax.re) bmax.re = bt.re;
                        if (bt.im > bmax.im) bmax.im = bt.im;
                    }
                }
                double Rmag = sqrt(elm.B1.re * elm.B1.re + elm.B2.re * elm.B2.re);
                if (Rmag != 0) {
                    b1out[i].re = bmax.re / Rmag * elm.B1.re;
                    b2out[i].re = bmax.re / Rmag * elm.B2.re;
                } else { b1out[i].re = 0; b2out[i].re = 0; }
                Rmag = sqrt(elm.B1.im * elm.B1.im + elm.B2.im * elm.B2.im);
                if (Rmag != 0) {
                    b1out[i].im = bmax.im / Rmag * elm.B1.im;
                    b2out[i].im = bmax.im / Rmag * elm.B2.im;
                } else { b1out[i].im = 0; b2out[i].im = 0; }
            }
        }

        // Check for point current sources — if so, fall back to element average
        // to avoid singularity contamination in the smoothed field
        if (!nodeproplist.empty()) {
            for (int j = 0; j < (int)nodelist.size(); j++) {
                if (abs(p - CComplex(nodelist[j].x, nodelist[j].y)) < 1.e-08) {
                    if (!nodelist[j].BoundaryMarker.empty()) {
                        // Find the property index to check for nonzero current
                        for (int pi = 0; pi < (int)nodeproplist.size(); pi++) {
                            if (nodeproplist[pi].PointName == nodelist[j].BoundaryMarker) {
                                if (nodeproplist[pi].Jr != 0 || nodeproplist[pi].Ji != 0) {
                                    b1out[i] = elm.B1;
                                    b2out[i] = elm.B2;
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // Check for r=0 axis in axisymmetric — set Br=0
        if (fabs(meshnode[k].x) < 1e-6 && ProblemType == 1)
            b1out[i] = CComplex(0, 0);
    }
}

// ================================================================
// Post-load processing pipeline
// ================================================================

void ResultsDoc::BuildConnectivity()
{
    int nn = (int)meshnode.size();
    int ne = (int)meshelem.size();

    NumList.assign(nn, 0);
    ConList.clear();
    ConList.resize(nn);

    // Count connections
    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int idx = meshelem[i].p[j];
            if (idx >= 0 && idx < nn) NumList[idx]++;
        }

    // Allocate
    for (int i = 0; i < nn; i++)
        ConList[i].reserve(NumList[i]);

    // Reset counts for building
    NumList.assign(nn, 0);

    // Build lists
    for (int i = 0; i < ne; i++)
        for (int j = 0; j < 3; j++) {
            int k = meshelem[i].p[j];
            if (k >= 0 && k < nn) {
                ConList[k].push_back(i);
                NumList[k]++;
            }
        }
}

void ResultsDoc::FindBoundaryEdges()
{
    // Identify boundary edges (edges with no neighbor element).
    // Ported from OF FemmviewDoc.cpp — uses connectivity lists.
    // Convention: n[j]=0 means unfinished, n[j]=1 means boundary edge.
    // Edge j is opposite vertex j, between vertices plus1mod3[j] and minus1mod3[j].
    static int plus1mod3[3] = {1, 2, 0};
    static int minus1mod3[3] = {2, 0, 1};

    int NumEls = (int)meshelem.size();

    // Init all neighbor flags to 0 (unfinished)
    for (int i = 0; i < NumEls; i++)
        for (int j = 0; j < 3; j++)
            meshelem[i].n[j] = 0;

    // For each element edge, search for a neighbor sharing both vertices
    for (int i = 0; i < NumEls; i++) {
        for (int j = 0; j < 3; j++) {
            if (meshelem[i].n[j] == 0) {
                int orgi = meshelem[i].p[plus1mod3[j]];
                int desti = meshelem[i].p[minus1mod3[j]];
                bool done = false;
                // Search org node's element list for one containing dest
                for (int ni = 0; ni < NumList[orgi]; ni++) {
                    int ei = ConList[orgi][ni];
                    if (ei == i) continue;
                    if (meshelem[ei].p[0] == desti ||
                        meshelem[ei].p[1] == desti ||
                        meshelem[ei].p[2] == desti) {
                        done = true;
                        break;
                    }
                }
                if (!done) {
                    // No neighbor found — this is a boundary edge
                    meshelem[i].n[j] = 1;
                }
            }
        }
    }
}

void ResultsDoc::ComputeMagDir()
{
    for (auto &el : meshelem) {
        if (el.lbl >= 0 && el.lbl < (int)labellist.size()) {
            if (labellist[el.lbl].MagDirFctn.empty())
                el.magdir = labellist[el.lbl].MagDir;
            else {
                // Would need Lua evaluation for MagDirFctn expressions
                // For now, use the constant value
                el.magdir = labellist[el.lbl].MagDir;
            }
        }
    }
}

void ResultsDoc::ComputeElementFields()
{
    for (int i = 0; i < (int)meshelem.size(); i++) {
        // Centroid and bounding radius
        meshelem[i].ctr = Ctr(i);
        meshelem[i].rsqr = 0;
        for (int j = 0; j < 3; j++) {
            double d = sqr(meshnode[meshelem[i].p[j]].x - meshelem[i].ctr.re) +
                       sqr(meshnode[meshelem[i].p[j]].y - meshelem[i].ctr.im);
            if (d > meshelem[i].rsqr) meshelem[i].rsqr = d;
        }

        // Compute B for this element
        GetElementB(meshelem[i]);
    }
}

void ResultsDoc::ComputeNodalFields()
{
    for (auto &el : meshelem)
        GetNodalB(el.b1, el.b2, el);
}

void ResultsDoc::ComputeFreqDepPermeability()
{
    if (Frequency == 0) return;

    double w = 2.0 * PI * Frequency;
    CComplex deg45(1, 1);

    for (auto &mat : blockproplist) {
        if (mat.LamType == 0) {
            mat.mu_fdx = mat.mu_x * CComplex(cos(-mat.Theta_hx * PI / 180.0),
                                               sin(-mat.Theta_hx * PI / 180.0));
            mat.mu_fdy = mat.mu_y * CComplex(cos(-mat.Theta_hy * PI / 180.0),
                                               sin(-mat.Theta_hy * PI / 180.0));

            if (mat.Lam_d != 0 && mat.Cduct != 0) {
                CComplex halflag = CComplex(cos(-mat.Theta_hx * PI / 360.0),
                                             sin(-mat.Theta_hx * PI / 360.0));
                double ds = sqrt(2.0 / (0.4 * PI * w * mat.Cduct * mat.mu_x));
                CComplex K = halflag * deg45 * mat.Lam_d * 0.001 / (2.0 * ds);
                mat.mu_fdx = (mat.mu_fdx * tanh(K) / K) *
                    mat.LamFill + (1.0 - mat.LamFill);

                halflag = CComplex(cos(-mat.Theta_hy * PI / 360.0),
                                    sin(-mat.Theta_hy * PI / 360.0));
                ds = sqrt(2.0 / (0.4 * PI * w * mat.Cduct * mat.mu_y));
                K = halflag * deg45 * mat.Lam_d * 0.001 / (2.0 * ds);
                mat.mu_fdy = (mat.mu_fdy * tanh(K) / K) *
                    mat.LamFill + (1.0 - mat.LamFill);
            }
            else {
                mat.mu_fdx = mat.mu_fdx * mat.LamFill + (1.0 - mat.LamFill);
                mat.mu_fdy = mat.mu_fdy * mat.LamFill + (1.0 - mat.LamFill);
            }
        }
    }
}

void ResultsDoc::ComputeBounds()
{
    if (meshnode.empty() || meshelem.empty()) return;

    // A bounds
    A_low = meshnode[0].A.re;
    A_high = A_low;
    for (auto &nd : meshnode) {
        if (nd.A.re > A_high) A_high = nd.A.re;
        if (nd.A.re < A_low) A_low = nd.A.re;
        if (Frequency != 0) {
            if (nd.A.im < A_low) A_low = nd.A.im;
            if (nd.A.im > A_high) A_high = nd.A.im;
        }
    }
    A_lb = A_low;
    A_ub = A_high;

    // Mark external region elements for exclusion from B bounds
    std::vector<bool> isExt(meshelem.size(), false);
    int extCount = 0;
    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl >= 0 && meshelem[i].lbl < (int)labellist.size()) {
            if (labellist[meshelem[i].lbl].IsExternal) {
                isExt[i] = true;
                extCount++;
            }
        }
        // Also check for exterior region materials (names starting with u1..u9)
        if (meshelem[i].blk >= 0 && meshelem[i].blk < (int)blockproplist.size()) {
            auto &name = blockproplist[meshelem[i].blk].BlockName;
            if (name.size() > 1 && name[0] == 'u' && name[1] >= '1' && name[1] <= '9') {
                isExt[i] = true;
                extCount++;
            }
        }
    }
    // If everything is marked external, unmark all
    if (extCount == (int)meshelem.size())
        std::fill(isExt.begin(), isExt.end(), false);

    // Find first non-external element
    int first = 0;
    while (first < (int)meshelem.size() && isExt[first]) first++;
    if (first >= (int)meshelem.size()) first = 0;

    // B and H bounds — ported from OF with heuristic and AC Re/Im tracking
    double br, bi, b;
    double Br_low, Br_high, Bi_low, Bi_high;
    double Hr_low, Hr_high, Hi_low, Hi_high;

    // Initialize from first non-external element
    {
        auto &fe = meshelem[first];
        br = sqrt(fe.b1[0].re*fe.b1[0].re + fe.b2[0].re*fe.b2[0].re);
        bi = sqrt(fe.b1[0].im*fe.b1[0].im + fe.b2[0].im*fe.b2[0].im);
        B_low = B_high = sqrt(br*br + bi*bi);
        Br_low = Br_high = br;
        Bi_low = Bi_high = bi;
    }
    double a0 = sqrt(meshelem[first].rsqr) * B_high * B_high;

    // H init from element average
    {
        CComplex h1, h2;
        if (Frequency != 0) GetH(meshelem[first].B1, meshelem[first].B2, h1, h2, first);
        else { h1 = h2 = CComplex(0,0); GetH(meshelem[first].B1.re, meshelem[first].B2.re, h1.re, h2.re, first); }
        Hr_low = Hr_high = sqrt(h1.re*h1.re + h2.re*h2.re);
        Hi_low = Hi_high = sqrt(h1.im*h1.im + h2.im*h2.im);
        H_low = H_high = sqrt(Hr_low*Hr_low + Hi_low*Hi_low);
    }

    for (int i = 0; i < (int)meshelem.size(); i++) {
        // B bounds from smoothed nodal values
        for (int j = 0; j < 3; j++) {
            br = sqrt(meshelem[i].b1[j].re*meshelem[i].b1[j].re +
                      meshelem[i].b2[j].re*meshelem[i].b2[j].re);
            bi = sqrt(meshelem[i].b1[j].im*meshelem[i].b1[j].im +
                      meshelem[i].b2[j].im*meshelem[i].b2[j].im);
            b = sqrt(br*br + bi*bi);

            // Heuristic: weight by element size to discount corner spikes
            double a1 = sqrt(meshelem[i].rsqr) * b * b;
            if (a1 > a0 && !isExt[i]) { B_high = b; a0 = a1; }

            if (!isExt[i]) {
                if (b < B_low) B_low = b;
                if (br > Br_high) Br_high = br;
                if (br < Br_low) Br_low = br;
                if (bi > Bi_high) Bi_high = bi;
                if (bi < Bi_low) Bi_low = bi;
            }
        }

        // H bounds from element averages
        if (!isExt[i]) {
            CComplex h1, h2;
            if (Frequency != 0) GetH(meshelem[i].B1, meshelem[i].B2, h1, h2, i);
            else { h1 = h2 = CComplex(0,0); GetH(meshelem[i].B1.re, meshelem[i].B2.re, h1.re, h2.re, i); }
            br = sqrt(h1.re*h1.re + h2.re*h2.re);
            bi = sqrt(h1.im*h1.im + h2.im*h2.im);
            b = sqrt(br*br + bi*bi);
            if (b > H_high) H_high = b;
            if (b < H_low) H_low = b;
            if (br > Hr_high) Hr_high = br;
            if (br < Hr_low) Hr_low = br;
            if (bi > Hi_high) Hi_high = bi;
            if (bi < Hi_low) Hi_low = bi;
        }
    }

    // Compute J bounds by scanning all elements
    double J_low = 0, J_high = 0;
    double Jr_low = 0, Jr_high = 0;
    double Ji_low = 0, Ji_high = 0;
    {
        CComplex Jelm[3], Aelm[3];
        bool first = true;
        for (int i = 0; i < (int)meshelem.size(); i++) {
            GetJA(i, Jelm, Aelm);
            for (int j = 0; j < 3; j++) {
                double jm = abs(Jelm[j]);
                double jr = fabs(Jelm[j].re);
                double ji = fabs(Jelm[j].im);
                if (first) {
                    J_low = J_high = jm;
                    Jr_low = Jr_high = jr;
                    Ji_low = Ji_high = ji;
                    first = false;
                } else {
                    if (jm > J_high) J_high = jm;
                    if (jm < J_low) J_low = jm;
                    if (jr > Jr_high) Jr_high = jr;
                    if (jr < Jr_low) Jr_low = jr;
                    if (ji > Ji_high) Ji_high = ji;
                    if (ji < Ji_low) Ji_low = ji;
                }
            }
        }
        J_low *= 1e-6; J_high *= 1e-6;
        Jr_low *= 1e-6; Jr_high *= 1e-6;
        Ji_low *= 1e-6; Ji_high *= 1e-6;
    }

    // Compute log|B| bounds
    double logBL = (B_low > 0) ? log10(B_low) : -12.0;
    if (logBL < -12.0) logBL = -12.0;
    logBL = floor(logBL * 10.0) / 10.0;
    double logBH = (B_high > 0) ? log10(B_high) : -12.0;
    if (logBH < -12.0) logBH = -12.0;
    logBH = ceil(logBH * 10.0) / 10.0;

    // Set plot bounds per type
    // DC: 0=|B|, 1=|H|, 2=|J|, 3=log|B|
    // AC: 0=|B|, 1=Re|B|, 2=Im|B|, 3=|H|, 4=Re|H|, 5=Im|H|, 6=|J|, 7=Re|J|, 8=Im|J|, 9=log|B|
    if (Frequency == 0) {
        PlotBounds[0][0] = B_low;  PlotBounds[0][1] = B_high;
        PlotBounds[1][0] = H_low;  PlotBounds[1][1] = H_high;
        PlotBounds[2][0] = J_low;  PlotBounds[2][1] = J_high;
        PlotBounds[3][0] = logBL;  PlotBounds[3][1] = logBH;
    } else {
        PlotBounds[0][0] = B_low;    PlotBounds[0][1] = B_high;    // |B|
        PlotBounds[1][0] = Br_low;   PlotBounds[1][1] = Br_high;   // Re|B|
        PlotBounds[2][0] = Bi_low;   PlotBounds[2][1] = Bi_high;   // Im|B|
        PlotBounds[3][0] = H_low;    PlotBounds[3][1] = H_high;    // |H|
        PlotBounds[4][0] = Hr_low;   PlotBounds[4][1] = Hr_high;   // Re|H|
        PlotBounds[5][0] = Hi_low;   PlotBounds[5][1] = Hi_high;   // Im|H|
        PlotBounds[6][0] = J_low;   PlotBounds[6][1] = J_high;   // |J|
        PlotBounds[7][0] = Jr_low;  PlotBounds[7][1] = Jr_high;  // Re|J|
        PlotBounds[8][0] = Ji_low;  PlotBounds[8][1] = Ji_high;  // Im|J|
        PlotBounds[9][0] = logBL;   PlotBounds[9][1] = logBH;    // log|B|
    }
    // Copy to defaults
    for (int i = 0; i < 10; i++)
        for (int k = 0; k < 2; k++)
            d_PlotBounds[i][k] = PlotBounds[i][k];
}

// ================================================================
// ComputeAGEHarmonics — harmonic decomposition of air gap fields
// Ported from FemmviewDoc.cpp:1054-1276
// ================================================================

void ResultsDoc::ComputeAGEHarmonics()
{
    for (auto &age : agelist) {
        double R = (age.ri + age.ro) / 2.0;
        double dr = age.ro - age.ri;
        double dt = (PI / 180.0) * age.totalArcLength / (double)age.totalArcElements;
        int m;

        if (age.BdryFormat == 0) {
            age.nn = (age.totalArcElements / 2) + 1;
            m = (int)round(360.0 / age.totalArcLength);
        }
        else {
            age.nn = (age.totalArcElements + 1) / 2;
            m = (int)round(180.0 / age.totalArcLength);
        }

        // Allocate harmonic arrays
        age.brc.resize(age.nn); age.brs.resize(age.nn);
        age.btc.resize(age.nn); age.bts.resize(age.nn);
        age.br.resize(age.totalArcElements);
        age.bt.resize(age.totalArcElements);
        age.nh.resize(age.nn);

        if (bIncremental) {
            age.brcPrev.resize(age.nn, 0); age.brsPrev.resize(age.nn, 0);
            age.btcPrev.resize(age.nn, 0); age.btsPrev.resize(age.nn, 0);
            age.brPrev.resize(age.totalArcElements, 0);
            age.btPrev.resize(age.totalArcElements, 0);
        }

        // Compute B at center of each gap element using cubic interpolation
        age.aco = CComplex(0, 0);
        double ci = age.InnerShift;
        double co = age.OuterShift;

        for (int k = 0; k < age.totalArcElements; k++) {
            int nn[10];
            double ww[10];
            CComplex a[10];

            // Inner nodes
            if (k - 1 < 0) {
                nn[0] = age.qp[age.totalArcElements - 1].n0;
                ww[0] = age.qp[age.totalArcElements - 1].w0;
            } else {
                nn[0] = age.qp[k - 1].n0;
                ww[0] = age.qp[k - 1].w0;
            }
            nn[1] = age.qp[k].n0; ww[1] = age.qp[k].w0;
            nn[2] = age.qp[k].n1; ww[2] = age.qp[k].w1;
            nn[3] = age.qp[k + 1].n1; ww[3] = age.qp[k + 1].w1;
            if (k + 2 > age.totalArcElements) {
                nn[4] = age.qp[1].n1; ww[4] = age.qp[1].w1;
            } else {
                nn[4] = age.qp[k + 2].n1; ww[4] = age.qp[k + 2].w1;
            }

            // Outer nodes
            if (k - 1 < 0) {
                nn[5] = age.qp[age.totalArcElements - 1].n2;
                ww[5] = age.qp[age.totalArcElements - 1].w2;
            } else {
                nn[5] = age.qp[k - 1].n2; ww[5] = age.qp[k - 1].w2;
            }
            nn[6] = age.qp[k].n2; ww[6] = age.qp[k].w2;
            nn[7] = age.qp[k].n3; ww[7] = age.qp[k].w3;
            nn[8] = age.qp[k + 1].n3; ww[8] = age.qp[k + 1].w3;
            if (k + 2 > age.totalArcElements) {
                nn[9] = age.qp[1].n3; ww[9] = age.qp[1].w3;
            } else {
                nn[9] = age.qp[k + 2].n3; ww[9] = age.qp[k + 2].w3;
            }

            // Fix antiperiodic weights
            if (k == 0 && age.BdryFormat == 1) { ww[0] = -ww[0]; ww[5] = -ww[5]; }
            if (k + 1 == age.totalArcElements && age.BdryFormat == 1) { ww[4] = -ww[4]; ww[9] = -ww[9]; }

            for (int kk = 0; kk < 10; kk++)
                a[kk] = meshnode[nn[kk]].A * ww[kk];

            // A at center (periodic only)
            if (age.BdryFormat == 0) {
                CComplex ac = (2*a[2]+2*a[3]+2*a[7]+2*a[8]+a[1]*ci+(a[2]-a[3]-a[4])*ci-(a[0]-3*a[1]+a[2]+3*a[3]-2*a[4])*ci*ci+(a[0]-2*a[1]+2*a[3]-a[4])*ci*ci*ci+(a[6]+a[7]-a[8]-a[9])*co-
                    (a[5]-3*a[6]+a[7]+3*a[8]-2*a[9])*co*co+(a[5]-2*a[6]+2*a[8]-a[9])*co*co*co)/8.0;
                age.aco = age.aco + ac / (double)age.totalArcElements;
            }

            // Flux density
            age.br[k] = (-(ci*a[1])-2*a[2]+2*a[3]+ci*(a[2]+a[3]-a[4])-ci*ci*ci*(a[0]-4*a[1]+6*a[2]-4*a[3]+a[4])+ci*ci*(a[0]-5*a[1]+9*a[2]-7*a[3]+2*a[4])-2*a[7]+
                2*a[8]+co*(-a[6]+a[7]+a[8]-a[9])-co*co*co*(a[5]-4*a[6]+6*a[7]-4*a[8]+a[9])+co*co*(a[5]-5*a[6]+9*a[7]-7*a[8]+2*a[9]))/(4*dt*R);
            age.bt[k] = (ci*a[1]+2*a[2]+2*a[3]-ci*ci*(a[0]-3*a[1]+a[2]+3*a[3]-2*a[4])+ci*(a[2]-a[3]-a[4])+ci*ci*ci*(a[0]-2*a[1]+2*a[3]-a[4])-co*a[6]+
                (-2+co)*(1+co)*a[7]-2*a[8]+co*(a[8]+co*(a[5]-3*a[6]+3*a[8]-2*a[9])+a[9]+co*co*(-a[5]+2*a[6]-2*a[8]+a[9])))/(4*dr);

            if (bIncremental) {
                for (int kk = 0; kk < 10; kk++)
                    a[kk] = CComplex(meshnode[nn[kk]].Aprev * ww[kk], 0);
                age.brPrev[k] = (-(ci*a[1])-2*a[2]+2*a[3]+ci*(a[2]+a[3]-a[4])-ci*ci*ci*(a[0]-4*a[1]+6*a[2]-4*a[3]+a[4])+ci*ci*(a[0]-5*a[1]+9*a[2]-7*a[3]+2*a[4])-2*a[7]+
                    2*a[8]+co*(-a[6]+a[7]+a[8]-a[9])-co*co*co*(a[5]-4*a[6]+6*a[7]-4*a[8]+a[9])+co*co*(a[5]-5*a[6]+9*a[7]-7*a[8]+2*a[9])).re/(4*dt*R);
                age.btPrev[k] = (ci*a[1]+2*a[2]+2*a[3]-ci*ci*(a[0]-3*a[1]+a[2]+3*a[3]-2*a[4])+ci*(a[2]-a[3]-a[4])+ci*ci*ci*(a[0]-2*a[1]+2*a[3]-a[4])-co*a[6]+
                    (-2+co)*(1+co)*a[7]-2*a[8]+co*(a[8]+co*(a[5]-3*a[6]+3*a[8]-2*a[9])+a[9]+co*co*(-a[5]+2*a[6]-2*a[8]+a[9]))).re/(4*dr);
            }
        }

        // Fourier decomposition
        for (int j = 0; j < age.nn; j++) {
            if (age.BdryFormat == 0) age.nh[j] = m * j;
            else age.nh[j] = m * (2 * j + 1);

            double n = age.nh[j];
            CComplex brc(0,0), brs(0,0), btc(0,0), bts(0,0);
            double brcP=0, brsP=0, btcP=0, btsP=0;

            for (int k = 0; k < age.totalArcElements; k++) {
                double tta = ((double)k + 0.5) * dt * n;
                brc = brc + age.br[k] * cos(tta);
                brs = brs + age.br[k] * sin(tta);
                btc = btc + age.bt[k] * cos(tta);
                bts = bts + age.bt[k] * sin(tta);
                if (bIncremental) {
                    brcP += age.brPrev[k] * cos(tta);
                    brsP += age.brPrev[k] * sin(tta);
                    btcP += age.btPrev[k] * cos(tta);
                    btsP += age.btPrev[k] * sin(tta);
                }
            }

            double div;
            if (age.nh[j] == 0 ||
                (j == age.nn - 1 && age.BdryFormat == 0 && age.totalArcElements % 2 == 0))
                div = age.totalArcElements;
            else
                div = age.totalArcElements / 2.0;

            age.brc[j] = brc / div; age.brs[j] = brs / div;
            age.btc[j] = btc / div; age.bts[j] = bts / div;
            if (bIncremental) {
                age.brcPrev[j] = brcP / div; age.brsPrev[j] = brsP / div;
                age.btcPrev[j] = btcP / div; age.btsPrev[j] = btsP / div;
            }
        }
    }
}

void ResultsDoc::GetGapValues(std::vector<XYPlotPoint> &pts, int plotType, int numPoints, int j)
{
    pts.clear();
    if (j < 0 || j >= (int)agelist.size() || numPoints < 2) return;

    auto &age = agelist[j];
    double dz = 360.0 / (numPoints - 1);
    double R = (age.ri + age.ro) / 2.0;
    int ncols = (Frequency == 0) ? 1 : 3;
    // |B| and |H| are always single-column
    if (plotType == 1 || plotType == 4) ncols = 1;

    pts.resize(numPoints);

    for (int i = 0; i < numPoints; i++) {
        double z = dz * i;
        double tta = z * PI / 180.0;
        pts[i].x = z;
        pts[i].vals.resize(ncols, 0);

        CComplex ac(0, 0), br(0, 0), bt(0, 0);
        for (int k = 0; k < age.nn; k++) {
            int n = age.nh[k];
            if (n > 0)
                ac += R * (-age.brs[k] * cos(n * tta) + age.brc[k] * sin(n * tta)) / (double)n;
            else
                ac += age.aco;
            br += age.brc[k] * cos(n * tta) + age.brs[k] * sin(n * tta);
            bt += age.btc[k] * cos(n * tta) + age.bts[k] * sin(n * tta);
        }
        CComplex hr = br / muo, ht = bt / muo;

        if (Frequency == 0) {
            switch (plotType) {
            case 0: pts[i].vals[0] = Re(ac); break;
            case 1: pts[i].vals[0] = Re(sqrt(br * conj(br) + bt * conj(bt))); break;
            case 2: pts[i].vals[0] = Re(br); break;
            case 3: pts[i].vals[0] = Re(bt); break;
            case 4: pts[i].vals[0] = Re(sqrt(hr * conj(hr) + ht * conj(ht))); break;
            case 5: pts[i].vals[0] = Re(hr); break;
            case 6: pts[i].vals[0] = Re(ht); break;
            }
        } else {
            switch (plotType) {
            case 0: pts[i].vals[0] = abs(ac); pts[i].vals[1] = Re(ac); pts[i].vals[2] = Im(ac); break;
            case 1: pts[i].vals[0] = Re(sqrt(br * conj(br) + bt * conj(bt))); break;
            case 2: pts[i].vals[0] = abs(br); pts[i].vals[1] = Re(br); pts[i].vals[2] = Im(br); break;
            case 3: pts[i].vals[0] = abs(bt); pts[i].vals[1] = Re(bt); pts[i].vals[2] = Im(bt); break;
            case 4: pts[i].vals[0] = Re(sqrt(hr * conj(hr) + ht * conj(ht))); break;
            case 5: pts[i].vals[0] = abs(hr); pts[i].vals[1] = Re(hr); pts[i].vals[2] = Im(hr); break;
            case 6: pts[i].vals[0] = abs(ht); pts[i].vals[1] = Re(ht); pts[i].vals[2] = Im(ht); break;
            }
        }
    }
}

void ResultsDoc::PostProcessing()
{
    // Scale depth to meters
    if (Depth == -1) Depth = 1;
    else Depth *= LengthConv();

    // Compute element centroids, bounding radii, and B fields
    ComputeElementFields();

    // Compute magnetization directions
    ComputeMagDir();

    // Set MuMax flag for DC incremental permeability problems
    if (bIncremental == 1 && Frequency == 0) {
        for (auto &mat : blockproplist)
            if (mat.BHpoints > 0) mat.MuMax = 1;
    }

    ComputeFreqDepPermeability();
    ComputeAGEHarmonics();

    // Compute PM energy offset (Nrg) for nonlinear PM materials
    for (auto &mat : blockproplist) {
        if (mat.H_c > 0 && mat.BHpoints > 0)
            mat.Nrg = mat.GetCoEnergy(mat.GetB(mat.H_c));
    }

    // Compute fill factor, effective conductivity, and permeability
    // for each block label (stranded/wound conductor regions)
    for (int k = 0; k < (int)labellist.size(); k++)
        GetFillFactor(k);

    BuildConnectivity();
    FindBoundaryEdges();
    ComputeNodalFields();
    // Compute nodal H (from nodal B via GetH) and nodal J (from GetJA)
    for (int i = 0; i < (int)meshelem.size(); i++) {
        auto &el = meshelem[i];
        // Bounds check
        if (el.blk < 0 || el.blk >= (int)blockproplist.size()) continue;
        if (el.lbl < 0 || el.lbl >= (int)labellist.size()) continue;
        for (int j = 0; j < 3; j++) {
            if (el.p[j] < 0 || el.p[j] >= (int)meshnode.size()) continue;
            if (Frequency != 0)
                GetH(el.b1[j], el.b2[j], el.h1[j], el.h2[j], i);
            else {
                el.h1[j] = CComplex(0, 0);
                el.h2[j] = CComplex(0, 0);
                GetH(el.b1[j].re, el.b2[j].re, el.h1[j].re, el.h2[j].re, i);
            }
        }
        CComplex Jelm[3], Aelm[3];
        GetJA(i, Jelm, Aelm);
        for (int j = 0; j < 3; j++) el.J[j] = Jelm[j];
    }

    // Compute A, B, H bounds and plot ranges
    ComputeBounds();
}

// ================================================================
// OpenDocument — read .ans file
// ================================================================

bool ResultsDoc::OpenDocument(const std::string &pathname)
{
    FILE *fp = femm_fopen(pathname, "rt");
    if (!fp) return false;

    nodelist.clear();
    linelist.clear();
    arclist.clear();
    blocklist_geom.clear();
    meshnode.clear();
    meshelem.clear();
    nodeproplist.clear();
    blockproplist.clear();
    circproplist.clear();
    labellist.clear();

    char s[1024], q[1024];
    char *v;
    bool inSolution = false;

    RPointProp PProp;
    RMaterialProp MProp;
    RBlockLabel BLabel;
    RCircuit CProp;

    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) q[0] = 0;

        if (_strnicmp(q, "[solution]", 10) == 0) {
            inSolution = true;
            continue;
        }

        if (!inSolution) {
            // Parse header
            if (_strnicmp(q, "[frequency]", 11) == 0) {
                v = StripKey(s); sscanf(v, "%lf", &Frequency);
            }
            if (_strnicmp(q, "[depth]", 7) == 0) {
                v = StripKey(s); sscanf(v, "%lf", &Depth);
            }
            if (_strnicmp(q, "[lengthunits]", 13) == 0) {
                v = StripKey(s); sscanf(v, "%s", q);
                if (_strnicmp(q, "inches", 6) == 0) LengthUnits = 0;
                else if (_strnicmp(q, "millimeters", 11) == 0) LengthUnits = 1;
                else if (_strnicmp(q, "centimeters", 1) == 0) LengthUnits = 2;
                else if (_strnicmp(q, "mils", 4) == 0) LengthUnits = 4;
                else if (_strnicmp(q, "microns", 6) == 0) LengthUnits = 5;
                else if (_strnicmp(q, "meters", 6) == 0) LengthUnits = 3;
            }
            if (_strnicmp(q, "[problemtype]", 13) == 0) {
                v = StripKey(s); sscanf(v, "%s", q);
                if (_strnicmp(q, "planar", 6) == 0) ProblemType = 0;
                if (_strnicmp(q, "axisymmetric", 3) == 0) ProblemType = 1;
            }
            if (_strnicmp(q, "[coordinates]", 13) == 0) {
                v = StripKey(s); sscanf(v, "%s", q);
                if (_strnicmp(q, "cartesian", 4) == 0) Coords = 0;
                if (_strnicmp(q, "polar", 5) == 0) Coords = 1;
            }

            // External region parameters (axisymmetric)
            if (_strnicmp(q, "[extzo]", 7) == 0) {
                v = StripKey(s); sscanf(v, "%lf", &extZo);
            }
            if (_strnicmp(q, "[extro]", 7) == 0) {
                v = StripKey(s); sscanf(v, "%lf", &extRo);
            }
            if (_strnicmp(q, "[extri]", 7) == 0) {
                v = StripKey(s); sscanf(v, "%lf", &extRi);
            }

            // Previous solution (incremental/frozen permeability)
            if (_strnicmp(q, "[prevsoln]", 10) == 0) {
                v = StripKey(s);
                PrevSoln = ExtractQuoted(v);
                if (!PrevSoln.empty()) bIncremental = PrevType;
                else bIncremental = 0;
            }
            if (_strnicmp(q, "[prevtype]", 10) == 0) {
                v = StripKey(s);
                sscanf(v, "%i", &PrevType);
                if (!PrevSoln.empty()) bIncremental = PrevType;
            }

            // Point properties
            if (_strnicmp(q, "<beginpoint>", 11) == 0) {
                PProp = RPointProp();
            }
            if (_strnicmp(q, "<pointname>", 11) == 0) {
                v = StripKey(s); PProp.PointName = ExtractQuoted(v);
            }
            if (_strnicmp(q, "<I_re>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.Jr); }
            if (_strnicmp(q, "<I_im>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.Ji); }
            if (_strnicmp(q, "<A_re>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.Ar); }
            if (_strnicmp(q, "<A_im>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &PProp.Ai); }
            if (_strnicmp(q, "<endpoint>", 9) == 0) {
                nodeproplist.push_back(PProp);
            }

            // Block properties
            if (_strnicmp(q, "<beginblock>", 11) == 0) MProp = RMaterialProp();
            if (_strnicmp(q, "<blockname>", 11) == 0) {
                v = StripKey(s); MProp.BlockName = ExtractQuoted(v);
            }
            if (_strnicmp(q, "<Mu_x>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.mu_x); }
            if (_strnicmp(q, "<Mu_y>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.mu_y); }
            if (_strnicmp(q, "<H_c>", 5) == 0 && _strnicmp(q, "<H_cAngle>", 10) != 0) {
                v = StripKey(s); sscanf(v, "%lf", &MProp.H_c);
            }
            if (_strnicmp(q, "<J_re>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.re); }
            if (_strnicmp(q, "<J_im>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.Jsrc.im); }
            if (_strnicmp(q, "<Sigma>", 7) == 0 && _strnicmp(q, "<Sigma_ssd>", 11) != 0) {
                v = StripKey(s); sscanf(v, "%lf", &MProp.Cduct);
            }
            if (_strnicmp(q, "<d_lam>", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.Lam_d); }
            if (_strnicmp(q, "<LamType>", 9) == 0) { v = StripKey(s); sscanf(v, "%i", &MProp.LamType); }
            if (_strnicmp(q, "<LamFill>", 9) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.LamFill); }
            if (_strnicmp(q, "<Phi_h>", 7) == 0 && _strnicmp(q, "<Phi_hx>", 8) != 0 && _strnicmp(q, "<Phi_hy>", 8) != 0) {
                v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hn);
            }
            if (_strnicmp(q, "<Phi_hx>", 8) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hx); }
            if (_strnicmp(q, "<Phi_hy>", 8) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.Theta_hy); }
            if (_strnicmp(q, "<NStrands>", 10) == 0) { v = StripKey(s); sscanf(v, "%i", &MProp.NStrands); }
            if (_strnicmp(q, "<WireD>", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &MProp.WireD); }
            if (_strnicmp(q, "<BHPoints>", 10) == 0) {
                v = StripKey(s);
                sscanf(v, "%i", &MProp.BHpoints);
                MProp.Bdata.resize(MProp.BHpoints);
                MProp.Hdata.resize(MProp.BHpoints);
                for (int j = 0; j < MProp.BHpoints; j++) {
                    fgets(s, 1024, fp);
                    double bval, hval;
                    sscanf(s, "%lf %lf", &bval, &hval);
                    MProp.Bdata[j] = bval;
                    MProp.Hdata[j] = CComplex(hval, 0);
                }
            }
            if (_strnicmp(q, "<endblock>", 9) == 0) {
                MProp.GetSlopes(Frequency * 2.0 * PI);
                blockproplist.push_back(MProp);
            }

            // Circuit properties
            if (_strnicmp(q, "<begincircuit>", 13) == 0) CProp = RCircuit();
            if (_strnicmp(q, "<circuitname>", 13) == 0) {
                v = StripKey(s); CProp.CircName = ExtractQuoted(v);
            }
            if (_strnicmp(q, "<TotalAmps_re>", 14) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.Amps.re); }
            if (_strnicmp(q, "<TotalAmps_im>", 14) == 0) { v = StripKey(s); sscanf(v, "%lf", &CProp.Amps.im); }
            if (_strnicmp(q, "<CircuitType>", 13) == 0) { v = StripKey(s); sscanf(v, "%i", &CProp.CircType); }
            if (_strnicmp(q, "<endcircuit>", 11) == 0) circproplist.push_back(CProp);

            // Input geometry
            int numItems;
            if (_strnicmp(q, "[numpoints]", 11) == 0) {
                v = StripKey(s); sscanf(v, "%i", &numItems);
                for (int i = 0; i < numItems; i++) {
                    fgets(s, 1024, fp);
                    FNode node;
                    int propIdx = 0;
                    sscanf(s, "%lf %lf %i %i", &node.x, &node.y, &propIdx, &node.InGroup);
                    // Store 1-based propIdx; OF uses BoundaryMarker >= 0 as index
                    // into nodeproplist. We store the name for consistency with
                    // other physics types; empty = no point property.
                    if (propIdx > 0 && propIdx <= (int)nodeproplist.size())
                        node.BoundaryMarker = nodeproplist[propIdx - 1].PointName;
                    nodelist.push_back(node);
                }
                q[0] = 0;
            }
            if (_strnicmp(q, "[numsegments]", 13) == 0) {
                v = StripKey(s); sscanf(v, "%i", &numItems);
                for (int i = 0; i < numItems; i++) {
                    fgets(s, 1024, fp);
                    FSegment seg; int propIdx = 0; double msl; int hidden = 0;
                    sscanf(s, "%i %i %lf %i %i %i",
                        &seg.n0, &seg.n1, &msl, &propIdx, &hidden, &seg.InGroup);
                    seg.MaxSideLength = msl;
                    seg.Hidden = (hidden != 0);
                    linelist.push_back(seg);
                }
                q[0] = 0;
            }
            if (_strnicmp(q, "[numarcsegments]", 16) == 0) {
                v = StripKey(s); sscanf(v, "%i", &numItems);
                for (int i = 0; i < numItems; i++) {
                    fgets(s, 1024, fp);
                    FArcSegment arc; int propIdx = 0; int hidden = 0;
                    sscanf(s, "%i %i %lf %lf %i %i %i %lf",
                        &arc.n0, &arc.n1, &arc.ArcLength, &arc.MaxSideLength,
                        &propIdx, &hidden, &arc.InGroup, &arc.mySideLength);
                    arc.Hidden = (hidden != 0);
                    arclist.push_back(arc);
                }
                q[0] = 0;
            }
            if (_strnicmp(q, "[numholes]", 10) == 0) {
                v = StripKey(s); sscanf(v, "%i", &numItems);
                for (int i = 0; i < numItems; i++) fgets(s, 1024, fp);
                q[0] = 0;
            }
            if (_strnicmp(q, "[numblocklabels]", 16) == 0) {
                v = StripKey(s); sscanf(v, "%i", &numItems);
                for (int i = 0; i < numItems; i++) {
                    fgets(s, 1024, fp);
                    RBlockLabel blk;
                    int blockIdx = 0, circIdx = 0, extFlags = 0;
                    double meshSize;
                    char magDirFctn[256] = "";
                    int nRead = sscanf(s, "%lf %lf %i %lf %i %lf %i %i %i %255[^\n]",
                        &blk.x, &blk.y, &blockIdx, &meshSize,
                        &circIdx, &blk.MagDir, &blk.InGroup, &blk.Turns,
                        &extFlags, magDirFctn);
                    if (meshSize > 0) blk.MaxArea = PI * meshSize * meshSize / 4.0;
                    blk.BlockType = blockIdx - 1;
                    blk.InCircuit = circIdx - 1;
                    blk.IsExternal = (extFlags & 1) != 0;
                    blk.IsDefault = (extFlags & 2) != 0;
                    if (nRead >= 10 && magDirFctn[0]) {
                        std::string fctn(magDirFctn);
                        if (fctn.size() >= 2 && fctn.front() == '"') {
                            fctn = fctn.substr(1);
                            auto pos = fctn.rfind('"');
                            if (pos != std::string::npos) fctn = fctn.substr(0, pos);
                        }
                        blk.MagDirFctn = fctn;
                    }
                    labellist.push_back(blk);
                }
                q[0] = 0;
            }

            continue;
        }

        // ---- Solution section ----
        int numNodes;
        sscanf(s, "%i", &numNodes);
        meshnode.resize(numNodes);

        for (int i = 0; i < numNodes; i++) {
            fgets(s, 1024, fp);
            auto &nd = meshnode[i];
            if (Frequency != 0) {
                if (!bIncremental)
                    sscanf(s, "%lf %lf %lf %lf", &nd.x, &nd.y, &nd.A.re, &nd.A.im);
                else {
                    int bc;
                    sscanf(s, "%lf %lf %lf %lf %i %lf",
                        &nd.x, &nd.y, &nd.A.re, &nd.A.im, &bc, &nd.Aprev);
                }
            }
            else {
                if (!bIncremental)
                    sscanf(s, "%lf %lf %lf", &nd.x, &nd.y, &nd.A.re);
                else {
                    int bc;
                    sscanf(s, "%lf %lf %lf %i %lf",
                        &nd.x, &nd.y, &nd.A.re, &bc, &nd.Aprev);
                }
                nd.A.im = 0;
            }
        }

        // Elements
        fgets(s, 1024, fp);
        int numElem;
        sscanf(s, "%i", &numElem);
        meshelem.resize(numElem);

        for (int i = 0; i < numElem; i++) {
            fgets(s, 1024, fp);
            auto &el = meshelem[i];
            if (!bIncremental)
                sscanf(s, "%i %i %i %i", &el.p[0], &el.p[1], &el.p[2], &el.lbl);
            else
                sscanf(s, "%i %i %i %i %lf", &el.p[0], &el.p[1], &el.p[2], &el.lbl, &el.Jp);
            // Note: lbl is already 0-based in the .ans file (solver uses Triangle -z flag)
            if (el.lbl >= 0 && el.lbl < (int)labellist.size())
                el.blk = labellist[el.lbl].BlockType;
        }

        // Circuit solution data — read Case and J/dVolts into labellist
        fgets(s, 1024, fp);
        int numCirc;
        sscanf(s, "%i", &numCirc);
        for (int i = 0; i < numCirc && i < (int)labellist.size(); i++) {
            fgets(s, 1024, fp);
            int caseVal;
            double zr = 0, zi = 0;
            if (Frequency == 0) {
                sscanf(s, "%i %lf", &caseVal, &zr);
                labellist[i].Case = caseVal;
                if (caseVal == 0) labellist[i].dVolts = CComplex(zr, 0);
                else labellist[i].J = CComplex(zr, 0);
            }
            else {
                sscanf(s, "%i %lf %lf", &caseVal, &zr, &zi);
                labellist[i].Case = caseVal;
                if (caseVal == 0) labellist[i].dVolts = CComplex(zr, zi);
                else labellist[i].J = CComplex(zr, zi);
            }
        }
        // Skip any remaining circuit lines
        for (int i = (int)labellist.size(); i < numCirc; i++)
            fgets(s, 1024, fp);

        // Skip PBC data (not used by postprocessor, but must read past it)
        if (fgets(s, 1024, fp) != NULL) {
            int numPBC;
            sscanf(s, "%i", &numPBC);
            for (int i = 0; i < numPBC; i++)
                fgets(s, 1024, fp);
        }

        // Read air gap elements
        if (fgets(s, 1024, fp) != NULL) {
            int numAGE;
            sscanf(s, "%i", &numAGE);
            double lc = LengthConvTable[LengthUnits];

            for (int ai = 0; ai < numAGE; ai++) {
                RAirGapElement age;

                // Boundary name
                fgets(s, 1024, fp);
                age.BdryName = s;
                // Strip quotes and newlines
                while (!age.BdryName.empty() &&
                       (age.BdryName.back() == '\n' || age.BdryName.back() == '\r' || age.BdryName.back() == '"'))
                    age.BdryName.pop_back();
                if (!age.BdryName.empty() && age.BdryName.front() == '"')
                    age.BdryName = age.BdryName.substr(1);

                // Parameters
                fgets(s, 1024, fp);
                sscanf(s, "%i %lf %lf %lf %lf %lf %lf %lf %i %lf %lf",
                    &age.BdryFormat, &age.InnerAngle, &age.OuterAngle,
                    &age.ri, &age.ro, &age.totalArcLength,
                    &age.agc.re, &age.agc.im, &age.totalArcElements,
                    &age.InnerShift, &age.OuterShift);

                age.ri *= lc;
                age.ro *= lc;

                // Read quad points
                age.qp.resize(age.totalArcElements + 1);
                for (int j = 0; j <= age.totalArcElements; j++) {
                    fgets(s, 1024, fp);
                    sscanf(s, "%i %lf %i %lf %i %lf %i %lf",
                        &age.qp[j].n0, &age.qp[j].w0,
                        &age.qp[j].n1, &age.qp[j].w1,
                        &age.qp[j].n2, &age.qp[j].w2,
                        &age.qp[j].n3, &age.qp[j].w3);
                }

                if (age.totalArcElements > 0)
                    agelist.push_back(age);
            }
        }

        break;  // done reading
    }

    fclose(fp);
    filePath = pathname;

    // Run full post-processing pipeline
    PostProcessing();

    return true;
}

// ================================================================
// Utility methods
// ================================================================

double ResultsDoc::GetElementBmag(int e) const
{
    if (e < 0 || e >= (int)meshelem.size()) return 0;
    double bx = meshelem[e].B1.re, by = meshelem[e].B2.re;
    return sqrt(bx * bx + by * by);
}

// InTriangleTest — orientation-consistent point-in-triangle test
// Works correctly with Cuthill-McKee sorted node ordering.
// Ported from FemmviewDoc.cpp:2823-2853
bool ResultsDoc::InTriangleTest(double x, double y, int i) const
{
    if (i < 0 || i >= (int)meshelem.size()) return false;

    for (int j = 0; j < 3; j++) {
        int k = j + 1; if (k == 3) k = 0;
        double z;
        if (meshelem[i].p[k] > meshelem[i].p[j]) {
            z = (meshnode[meshelem[i].p[k]].x - meshnode[meshelem[i].p[j]].x) *
                (y - meshnode[meshelem[i].p[j]].y) -
                (meshnode[meshelem[i].p[k]].y - meshnode[meshelem[i].p[j]].y) *
                (x - meshnode[meshelem[i].p[j]].x);
            if (z < 0) return false;
        }
        else {
            z = (meshnode[meshelem[i].p[j]].x - meshnode[meshelem[i].p[k]].x) *
                (y - meshnode[meshelem[i].p[k]].y) -
                (meshnode[meshelem[i].p[j]].y - meshnode[meshelem[i].p[k]].y) *
                (x - meshnode[meshelem[i].p[k]].x);
            if (z > 0) return false;
        }
    }
    return true;
}

// InTriangle — banded search exploiting Cuthill-McKee element ordering.
// Elements are sorted by the sum of their node indices, which spatially
// clusters nearby elements at nearby indices. The search starts from the
// last found element and expands outward, using rsqr for quick rejection.
// Ported from FemmviewDoc.cpp:1684-1731
int ResultsDoc::InTriangle(double x, double y)
{
    static int k = 0;
    int sz = (int)meshelem.size();
    if (sz == 0) return -1;
    if (k < 0 || k >= sz) k = 0;

    // Check the last-found element first (spatial locality)
    if (InTriangleTest(x, y, k)) return k;

    int hi = k, lo = k;

    for (int j = 0; j < sz; j += 2) {
        hi++; if (hi >= sz) hi = 0;
        lo--; if (lo < 0)   lo = sz - 1;

        // Quick reject: check if point is within bounding circle of element
        double z = (meshelem[hi].ctr.re - x) * (meshelem[hi].ctr.re - x) +
                   (meshelem[hi].ctr.im - y) * (meshelem[hi].ctr.im - y);
        if (z <= meshelem[hi].rsqr) {
            if (InTriangleTest(x, y, hi)) {
                k = hi;
                return k;
            }
        }

        z = (meshelem[lo].ctr.re - x) * (meshelem[lo].ctr.re - x) +
            (meshelem[lo].ctr.im - y) * (meshelem[lo].ctr.im - y);
        if (z <= meshelem[lo].rsqr) {
            if (InTriangleTest(x, y, lo)) {
                k = lo;
                return k;
            }
        }
    }

    return -1;
}

// Keep the simple version as a private helper for PointInTri
// (used by GetLineValues for neighbor search)
bool ResultsDoc::PointInTri(double px, double py, int e) const
{
    return InTriangleTest(px, py, e);
}

void ResultsDoc::GetCircle(const FArcSegment &asegm, CComplex &c, double &R) const
{
    CComplex a0(nodelist[asegm.n0].x, nodelist[asegm.n0].y);
    CComplex a1(nodelist[asegm.n1].x, nodelist[asegm.n1].y);
    double d = abs(a1 - a0);
    double alpha = asegm.ArcLength * PI / 180.0;
    R = d / (2.0 * sin(alpha / 2.0));
    CComplex mid = (a0 + a1) / 2.0;
    CComplex dir = (a1 - a0) / d;
    CComplex perp(-dir.im, dir.re);
    double h = R * cos(alpha / 2.0);
    if (asegm.NormalDirection)
        c = mid + perp * h;
    else
        c = mid - perp * h;
}

// ================================================================
// GetPointB — interpolate smoothed nodal B to arbitrary point
// Ported from FemmviewDoc.cpp:2135-2166
// ================================================================

void ResultsDoc::GetPointB(double x, double y, CComplex &B1, CComplex &B2, RElement &elm)
{
    if (!Smooth) {
        B1 = elm.B1;
        B2 = elm.B2;
        return;
    }

    int n[3];
    double a[3], b[3], c[3], da;
    for (int i = 0; i < 3; i++) n[i] = elm.p[i];

    a[0] = meshnode[n[1]].x * meshnode[n[2]].y - meshnode[n[2]].x * meshnode[n[1]].y;
    a[1] = meshnode[n[2]].x * meshnode[n[0]].y - meshnode[n[0]].x * meshnode[n[2]].y;
    a[2] = meshnode[n[0]].x * meshnode[n[1]].y - meshnode[n[1]].x * meshnode[n[0]].y;
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    B1 = CComplex(0, 0);
    B2 = CComplex(0, 0);
    for (int i = 0; i < 3; i++) {
        double w = (a[i] + b[i] * x + c[i] * y) / da;
        B1 = B1 + elm.b1[i] * w;
        B2 = B2 + elm.b2[i] * w;
    }
}

// ================================================================
// GetPointValues — compute all field values at arbitrary point
// Ported from FemmviewDoc.cpp:1733-1972 (DC case)
// ================================================================

bool ResultsDoc::GetPointValues(double x, double y, PointVals &u)
{
    int k = InTriangle(x, y);
    if (k < 0) return false;
    return GetPointValues(x, y, k, u);
}

bool ResultsDoc::GetPointValues(double x, double y, int k, PointVals &u)
{
    int n[3];
    double a[3], b[3], c[3], da;

    for (int i = 0; i < 3; i++) n[i] = meshelem[k].p[i];
    a[0] = meshnode[n[1]].x * meshnode[n[2]].y - meshnode[n[2]].x * meshnode[n[1]].y;
    a[1] = meshnode[n[2]].x * meshnode[n[0]].y - meshnode[n[0]].x * meshnode[n[2]].y;
    a[2] = meshnode[n[0]].x * meshnode[n[1]].y - meshnode[n[1]].x * meshnode[n[0]].y;
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    // Interpolate B at the point
    GetPointB(x, y, u.B1, u.B2, meshelem[k]);

    u.Hc = CComplex(0, 0);
    u.mu12 = CComplex(0, 0);
    u.ff = labellist[meshelem[k].lbl].FillFactor;

    if (Frequency == 0) {
        // DC case — interpolate A
        u.A = CComplex(0, 0);
        if (ProblemType == 0) {
            for (int i = 0; i < 3; i++)
                u.A.re += meshnode[n[i]].A.re * (a[i] + b[i] * x + c[i] * y) / da;
        }
        else {
            // Axisymmetric: quadratic interpolation
            double v[6], R[3];
            for (int i = 0; i < 3; i++) R[i] = meshnode[n[i]].x;
            v[0] = meshnode[n[0]].A.re;
            v[2] = meshnode[n[1]].A.re;
            v[4] = meshnode[n[2]].A.re;

            if (R[0] < 1e-6 && R[1] < 1e-6) v[1] = (v[0] + v[2]) / 2.0;
            else v[1] = (R[1] * (3 * v[0] + v[2]) + R[0] * (v[0] + 3 * v[2])) / (4 * (R[0] + R[1]));
            if (R[1] < 1e-6 && R[2] < 1e-6) v[3] = (v[2] + v[4]) / 2.0;
            else v[3] = (R[2] * (3 * v[2] + v[4]) + R[1] * (v[2] + 3 * v[4])) / (4 * (R[1] + R[2]));
            if (R[2] < 1e-6 && R[0] < 1e-6) v[5] = (v[4] + v[0]) / 2.0;
            else v[5] = (R[0] * (3 * v[4] + v[0]) + R[2] * (v[4] + 3 * v[0])) / (4 * (R[2] + R[0]));

            double p = (b[1] * x + c[1] * y + a[1]) / da;
            double q = (b[2] * x + c[2] * y + a[2]) / da;
            u.A.re = v[0] - p * (3 * v[0] - 4 * v[1] + v[2]) +
                     2 * p * p * (v[0] - 2 * v[1] + v[2]) -
                     q * (3 * v[0] + v[4] - 4 * v[5]) +
                     2 * q * q * (v[0] + v[4] - 2 * v[5]) +
                     4 * p * q * (v[0] - v[1] + v[3] - v[5]);
        }

        // Permeability and H field
        u.mu1.im = 0; u.mu2.im = 0;
        GetMu(u.B1.re, u.B2.re, u.mu1.re, u.mu2.re, k);
        u.H1.re = u.B1.re / (u.mu1.re * muo);
        u.H2.re = u.B2.re / (u.mu2.re * muo);
        u.H1.im = 0; u.H2.im = 0;

        // Current density
        u.Je = CComplex(0, 0);
        u.Js = CComplex(blockproplist[meshelem[k].blk].Jsrc.re, 0);
        int lbl = meshelem[k].lbl;
        int j = labellist[lbl].InCircuit;
        if (j >= 0) {
            if (labellist[lbl].Case == 0) {
                if (ProblemType == 0)
                    u.Js.re -= labellist[lbl].dVolts.re * labellist[lbl].o.re;
                else {
                    // Axisymmetric voltage-driven: interpolate 1/r weighting
                    double ravg = Re(Ctr(k)) * LengthConv();
                    double R[3];
                    for (int tn = 0; tn < 3; tn++) {
                        R[tn] = meshnode[n[tn]].x;
                        if (R[tn] < 1e-6) R[tn] = ravg;
                        else R[tn] *= LengthConv();
                    }
                    double rinterp = 0;
                    for (int tn = 0; tn < 3; tn++)
                        rinterp += (1.0 / R[tn]) * (a[tn] + b[tn] * x + c[tn] * y) / da;
                    u.Js.re -= labellist[lbl].o.re * labellist[lbl].dVolts.re * rinterp;
                }
            }
            else
                u.Js = u.Js + labellist[lbl].J;
        }

        // Conductivity
        u.c = labellist[meshelem[k].lbl].o.re;

        // Energy density — use proper BH-curve-based computation
        u.E = blockproplist[meshelem[k].blk].DoEnergy(u.B1.re, u.B2.re);

        // PM correction — compute Hc, correct H and energy
        if (blockproplist[meshelem[k].blk].H_c != 0) {
            int bk = meshelem[k].blk;
            u.Hc = blockproplist[bk].H_c *
                CComplex(cos(meshelem[k].magdir * PI / 180.0),
                         sin(meshelem[k].magdir * PI / 180.0));
            u.H1.re -= u.Hc.re;
            u.H2.re -= u.Hc.im;

            // Corrected energy for PM
            if (blockproplist[bk].BHpoints == 0)
                u.E = 0.5 * muo * (u.mu1.re * u.H1.re * u.H1.re + u.mu2.re * u.H2.re * u.H2.re);
            else
                u.E = u.E + blockproplist[bk].Nrg
                      - blockproplist[bk].H_c * Re((u.B1.re + CComplex(0,1) * u.B2.re) /
                        exp(CComplex(0,1) * PI * meshelem[k].magdir / 180.0));

            // If not shifting H in PMs, add Hc back into H and zero it
            if (!ShiftH) {
                u.H1.re += u.Hc.re;
                u.H2.re += u.Hc.im;
                u.Hc = CComplex(0, 0);
            }
        }

        // Local stored energy for wound conductors with proximity/skin effect
        if (blockproplist[meshelem[k].blk].LamType > 2) {
            CComplex Jlocal = u.Js * 1e6;
            u.E += Re(Jlocal * Jlocal) * labellist[meshelem[k].lbl].LocalEnergy / 2.0;
        }

        u.Ph = 0;
        u.Pe = 0;
    }
    else {
        // AC case — complex fields
        u.A = CComplex(0, 0);
        if (ProblemType == 0) {
            for (int i = 0; i < 3; i++)
                u.A = u.A + meshnode[n[i]].A * ((a[i] + b[i] * x + c[i] * y) / da);
        }
        else {
            // Axisymmetric: quadratic interpolation (same as DC case but complex)
            CComplex vc[6];
            double R[3];
            for (int i = 0; i < 3; i++) R[i] = meshnode[n[i]].x;
            vc[0] = meshnode[n[0]].A; vc[2] = meshnode[n[1]].A; vc[4] = meshnode[n[2]].A;
            if (R[0] < 1e-6 && R[1] < 1e-6) vc[1] = (vc[0] + vc[2]) / 2.0;
            else vc[1] = (R[1] * (3 * vc[0] + vc[2]) + R[0] * (vc[0] + 3 * vc[2])) / (4 * (R[0] + R[1]));
            if (R[1] < 1e-6 && R[2] < 1e-6) vc[3] = (vc[2] + vc[4]) / 2.0;
            else vc[3] = (R[2] * (3 * vc[2] + vc[4]) + R[1] * (vc[2] + 3 * vc[4])) / (4 * (R[1] + R[2]));
            if (R[2] < 1e-6 && R[0] < 1e-6) vc[5] = (vc[4] + vc[0]) / 2.0;
            else vc[5] = (R[0] * (3 * vc[4] + vc[0]) + R[2] * (vc[4] + 3 * vc[0])) / (4 * (R[2] + R[0]));
            double p = (b[1] * x + c[1] * y + a[1]) / da;
            double q = (b[2] * x + c[2] * y + a[2]) / da;
            u.A = vc[0] - (3 * vc[0] - 4 * vc[1] + vc[2]) * p +
                  2 * (vc[0] - 2 * vc[1] + vc[2]) * p * p -
                  (3 * vc[0] + vc[4] - 4 * vc[5]) * q +
                  2 * (vc[0] + vc[4] - 2 * vc[5]) * q * q +
                  4 * (vc[0] - vc[1] + vc[3] - vc[5]) * p * q;
        }

        // Permeability and H
        if (!bIncremental) {
            GetMu(u.B1, u.B2, u.mu1, u.mu2, k);
            u.mu12 = CComplex(0, 0);
            u.H1 = u.B1 / (u.mu1 * muo);
            u.H2 = u.B2 / (u.mu2 * muo);
        }
        else {
            // Incremental permeability about DC operating point
            CComplex muinc, murel;
            double B1p = meshelem[k].B1p, B2p = meshelem[k].B2p;
            double B = sqrt(B1p * B1p + B2p * B2p);
            GetMu(CComplex(B1p, 0), CComplex(B2p, 0), muinc, murel, k);
            if (B == 0) {
                u.mu1 = murel; u.mu2 = murel; u.mu12 = CComplex(0, 0);
            }
            else {
                u.mu1 = (B1p * B1p * muinc + B2p * B2p * murel) / (B * B);
                u.mu12 = (B1p * B2p * (muinc - murel)) / (B * B);
                u.mu2 = (B2p * B2p * muinc + B1p * B1p * murel) / (B * B);
            }
            u.H1 = (u.B2 * u.mu12 - u.B1 * u.mu2) / (u.mu12 * u.mu12 - u.mu1 * u.mu2);
            u.H2 = (u.B2 * u.mu1 - u.B1 * u.mu12) / (u.mu1 * u.mu2 - u.mu12 * u.mu12);
        }

        // Current density
        u.Js = blockproplist[meshelem[k].blk].Jsrc;
        int lbl = meshelem[k].lbl;
        int j = labellist[lbl].InCircuit;
        if (j >= 0) {
            if (labellist[lbl].Case == 0) {
                if (ProblemType == 0)
                    u.Js = u.Js - labellist[lbl].o * labellist[lbl].dVolts;
                else {
                    // Axisymmetric voltage-driven: interpolate 1/r weighting
                    double ravg = Re(Ctr(k)) * LengthConv();
                    double R[3];
                    for (int tn = 0; tn < 3; tn++) {
                        R[tn] = meshnode[n[tn]].x;
                        if (R[tn] < 1e-6) R[tn] = ravg;
                        else R[tn] *= LengthConv();
                    }
                    double rinterp = 0;
                    for (int tn = 0; tn < 3; tn++)
                        rinterp += (1.0 / R[tn]) * (a[tn] + b[tn] * x + c[tn] * y) / da;
                    u.Js = u.Js - labellist[lbl].o * labellist[lbl].dVolts * rinterp;
                }
            }
            else
                u.Js = u.Js + labellist[lbl].J;
        }

        // Conductivity — report loss-related part (real part of reciprocal of complex o)
        if (blockproplist[meshelem[k].blk].Cduct != 0)
            u.c = 1.0 / Re(1.0 / labellist[meshelem[k].lbl].o);
        else
            u.c = 0;
        if (blockproplist[meshelem[k].blk].Lam_d != 0) u.c = 0;

        // Eddy current (only for solid regions with FillFactor < 0)
        u.Je = CComplex(0, 0);
        if (labellist[meshelem[k].lbl].FillFactor < 0) {
            u.Je = CComplex(0, -1) * Frequency * 2.0 * PI * u.c * u.A;
            if (ProblemType != 0) {
                if (fabs(x) > 1e-6)
                    u.Je = u.Je / (2.0 * PI * x * LengthConv());
                else
                    u.Je = CComplex(0, 0);
            }
        }

        // Energy and losses
        CComplex zz = u.H1 * conj(u.B1) + u.H2 * conj(u.B2);
        u.E = 0.25 * zz.re;

        // Local stored energy for wound conductors (divide by 4 for AC, not 2)
        if (blockproplist[meshelem[k].blk].LamType > 2) {
            CComplex Jlocal = u.Js * 1e6;
            u.E += Re(Jlocal * conj(Jlocal)) * labellist[meshelem[k].lbl].LocalEnergy / 4.0;
        }

        u.Ph = Frequency * PI * zz.im;
        u.Pe = 0;
        if (u.c != 0) {
            CComplex Jtot = u.Js + u.Je;
            u.Pe = 1e6 * (Jtot.re * Jtot.re + Jtot.im * Jtot.im) / (u.c * 2.0);
        }
    }

    return true;
}

// ================================================================
// ElmArea — element area in mesh coordinates
// ================================================================

double ResultsDoc::ElmArea(int i) const
{
    int n[3];
    for (int j = 0; j < 3; j++) n[j] = meshelem[i].p[j];
    double b0 = meshnode[n[1]].y - meshnode[n[2]].y;
    double b1 = meshnode[n[2]].y - meshnode[n[0]].y;
    double c0 = meshnode[n[2]].x - meshnode[n[1]].x;
    double c1 = meshnode[n[0]].x - meshnode[n[2]].x;
    return (b0 * c1 - b1 * c0) / 2.0;
}

// ================================================================
// GetFillFactor — compute fill factor, effective conductivity,
// and permeability for stranded/wound conductor regions.
// Ported from OF's FemmviewDoc.cpp.
// ================================================================

void ResultsDoc::GetFillFactor(int lbl)
{
    const CComplex I(0, 1);  // imaginary unit (can't use macro — clashes with Qt)
    RMaterialProp &bp = blockproplist[labellist[lbl].BlockType];
    RBlockLabel &bl = labellist[lbl];
    double lc = LengthConv() * LengthConv();

    // Default values
    bl.FillFactor = (abs(bl.Turns) > 1) ? 1 : -1;
    bl.o = bp.Cduct;
    bl.mu = 0;

    if (bp.LamType < 3) return;

    // Compute total area of associated block
    double atot = 0;
    for (int i = 0; i < (int)meshelem.size(); i++)
        if (meshelem[i].lbl == lbl) atot += ElmArea(i) * lc;
    if (atot == 0) return;

    int wiretype = bp.LamType - 3;
    double w = 2.0 * PI * Frequency;
    double R, awire, o, fill, d, dd, W;
    CComplex ufd, ueff, ofd;

    if (wiretype == 3) {
        // Rectangular wire
        d = bp.WireD * 0.001;
        bl.FillFactor = fabs(d * d * ((double)bl.Turns) / atot);
        dd = d / sqrt(bl.FillFactor);
        fill = d / dd;
        o = bp.Cduct * fill * 1e6;
        W = w * o * muo * d * d * fill / 4.0;
        if (Frequency == 0 || bp.Cduct == 0) {
            bl.o = bp.Cduct * bl.FillFactor;
            bl.LocalEnergy = (dd - d) * dd * muo / 6.0;
            bl.mu = 1;
            return;
        }
        ufd = muo * tanh(sqrt(I * W)) / sqrt(I * W);
        ueff = fill * ufd + (1.0 - fill) * muo;
        bl.o = 1.0 / (muo / (fill * o * ufd) + I * dd * dd * (1.0 - fill) * muo * w / 4.0
                       - I * dd * dd * ueff * w / 12.0);
        bl.o *= 1e-6;
        bl.mu = ueff / muo;
        if (W > 0.1)
            bl.LocalEnergy = 1e-6 * Im(1.0 / bl.o) / w;
        else
            bl.LocalEnergy = muo * ((dd - d) * dd / 6.0 + (2.0 / 189.0) * d * dd * W * W);
        return;
    }

    // Round wires
    switch (wiretype) {
        case 1: // stranded non-litz
            R = bp.WireD * 0.0005 * sqrt((double)bp.NStrands);
            awire = PI * R * R * ((double)bl.Turns);
            break;
        default: // magnet wire, litz, CCA
            R = bp.WireD * 0.0005;
            awire = PI * R * R * ((double)bp.NStrands) * ((double)bl.Turns);
            break;
    }
    bl.FillFactor = fabs(awire / atot);
    fill = bl.FillFactor;

    w = 2.0 * PI * Frequency;
    o = bp.Cduct * 1e6;
    W = w * o * muo * R * R / 2.0;
    dd = (1.6494541661869013 * R) / sqrt(fill);

    double c1, c2, c3, c4;
    switch (wiretype) {
        case 0: case 1: case 2: // magnet wire, plain stranded, litz
            c1 = 0.7756067409818643 + fill * (0.6873854335408803 + fill * (0.06841584481674128 - 0.07143732702512284 * fill));
            c2 = 1.5 * fill / c1;
            c3 = 0.8824642871525136 + fill * (-0.008605512994838827 + fill * (0.7223208744682307 - 0.2157183942377177 * fill));
            c4 = log(1.5299240194394943 / sqrt(fill)) - c3 / 3.0;
            break;
        case 4: // 10% CCA
            c1 = 0.7270741505617485 + 0.8902950067721367 * fill + 0.11894736885885195 * fill * fill - 0.12247276254503957 * fill * fill * fill;
            c2 = 0.006784920229549677 + 1.8942880489198526 * fill - 1.3631438759519217 * fill * fill + 0.504431701685587 * fill * fill * fill;
            c3 = 0.7879151117538901 - 0.7216375417404758 * fill + 2.266562622770727 * fill * fill - 1.1349603497265566 * fill * fill * fill;
            c4 = 1.6190358786364027 - 3.967461431323324 * fill + 4.068458362148543 * fill * fill - 1.7975534375694646 * fill * fill * fill;
            break;
        case 5: // 15% CCA
            c1 = 0.7486913529860821 + 0.9042845510838825 * fill + 0.1361040321433224 * fill * fill - 0.10652380745682069 * fill * fill * fill;
            c2 = 0.006790468527313965 + 1.8945509985370095 * fill - 1.3643501010185972 * fill * fill + 0.5036765577982594 * fill * fill * fill;
            c3 = 0.7519323306379909 - 0.7184130853903536 * fill + 2.2551599408130594 * fill * fill - 1.1439852083597326 * fill * fill * fill;
            c4 = 1.6204876714534728 - 3.966250827502031 * fill + 4.064413095233383 * fill * fill - 1.7940297437703776 * fill * fill * fill;
            break;
        default: c1 = c2 = c3 = c4 = 0; break;
    }

    if (Frequency == 0 || bp.Cduct == 0) {
        bl.o = bp.Cduct * fill;
        bl.LocalEnergy = muo * (2 * (c3 + 3 * c4) * R * R / fill - dd * dd) / 12.0;
        bl.mu = 1;
        return;
    }

    ufd = c2 * (tanh(sqrt(c1 * I * W)) / sqrt(c1 * I * W)) + (1.0 - c2);
    bl.mu = ufd;
    ofd = o * fill / (I * c4 * W + sqrt(I * c3 * W) * (1.0 / tanh(sqrt(I * c3 * W))));
    ofd = 1.0 / (1.0 / ofd - I * w * ufd * muo * dd * dd / 12.0);
    bl.o = ofd * 1e-6;

    if (W > 0.1)
        bl.LocalEnergy = 1e-6 * Im(1.0 / bl.o) / w;
    else
        bl.LocalEnergy = (muo * (21 * dd * dd * fill * (-15 + 2 * c1 * c1 * c2 * W * W)
                         + 2 * R * R * (315 * (c3 + 3 * c4) - 2 * c3 * c3 * c3 * W * W))) / (3780.0 * fill);
}

// ================================================================
// Integration helpers
// ================================================================

CComplex ResultsDoc::PlnInt(double a, CComplex *u, CComplex *v) const
{
    CComplex z[3], x;
    z[0] = 2.0 * u[0] + u[1] + u[2];
    z[1] = u[0] + 2.0 * u[1] + u[2];
    z[2] = u[0] + u[1] + 2.0 * u[2];
    x = CComplex(0, 0);
    for (int i = 0; i < 3; i++) x = x + v[i] * z[i];
    return x * (a / 12.0);
}

CComplex ResultsDoc::AxiInt(double a, CComplex *u, CComplex *v, double *r) const
{
    CComplex M[3][3], z[3], x;
    M[0][0] = 6 * r[0] + 2 * r[1] + 2 * r[2];
    M[0][1] = 2 * r[0] + 2 * r[1] + 1 * r[2];
    M[0][2] = 2 * r[0] + 1 * r[1] + 2 * r[2];
    M[1][1] = 2 * r[0] + 6 * r[1] + 2 * r[2];
    M[1][2] = 1 * r[0] + 2 * r[1] + 2 * r[2];
    M[2][2] = 2 * r[0] + 2 * r[1] + 6 * r[2];
    M[1][0] = M[0][1]; M[2][0] = M[0][2]; M[2][1] = M[1][2];

    for (int i = 0; i < 3; i++)
        z[i] = M[i][0] * u[0] + M[i][1] * u[1] + M[i][2] * u[2];
    x = CComplex(0, 0);
    for (int i = 0; i < 3; i++) x = x + v[i] * z[i];
    return x * (PI * a / 30.0);
}

// ================================================================
// MakeMask — solve Laplace equation for weighted stress tensor mask
// Ported from OF makemask.cpp
// ================================================================

bool ResultsDoc::IsKosher(int k) const
{
    // For axisymmetric: a node on r=0 is "kosher" (can be set to zero)
    // only if it doesn't have a neighbor that's in a selected block
    for (int j = 0; j < NumList[k]; j++) {
        int elm = ConList[k][j];
        if (labellist[meshelem[elm].lbl].IsSelected) return false;
    }
    return true;
}

bool ResultsDoc::MakeMask()
{
    if (bHasMask) return true;

    int i, j, k, d;
    CBigLinProb L;
    double v;
    double Me[3][3], be[3];
    double p[3], q[3];
    double a;
    int n[3];

    int NumNodes = (int)meshnode.size();
    int NumEls = (int)meshelem.size();
    bool bOnAxis = false;

    static int plus1mod3[3] = {1, 2, 0};
    static int minus1mod3[3] = {2, 0, 1};

    // Figure out bandwidth
    int bw = 0;
    for (i = 0; i < NumEls; i++)
        for (j = 0; j < 3; j++) {
            k = (j + 1) % 3;
            d = abs(meshelem[i].p[j] - meshelem[i].p[k]);
            if (d > bw) bw = d;
        }
    bw++;

    L.Create(NumNodes, bw);

    // Check if axisymmetric selection lies along r=0
    if (ProblemType == 1)
        for (i = 0; i < NumEls; i++)
            if (labellist[meshelem[i].lbl].IsSelected) {
                for (j = 0; j < 3; j++)
                    if (meshnode[meshelem[i].p[j]].x < 1e-6) { bOnAxis = true; break; }
                if (bOnAxis) break;
            }

    // Sort materials: 0=air, 1=not air
    std::vector<int> matflag(blockproplist.size(), 0);
    for (i = 0; i < (int)blockproplist.size(); i++) {
        k = 0;
        if (blockproplist[i].mu_x != 1 || blockproplist[i].mu_y != 1) k = 1;
        if (blockproplist[i].BHpoints != 0) k = 1;
        if (blockproplist[i].LamType != 0) k = 1;
        if (blockproplist[i].H_c != 0) k = 1;
        if (blockproplist[i].Jsrc.re != 0 || blockproplist[i].Jsrc.im != 0) k = 1;
        if (blockproplist[i].Cduct != 0) k = 1;
        if (blockproplist[i].Theta_hn != 0 ||
            blockproplist[i].Theta_hx != 0 ||
            blockproplist[i].Theta_hy != 0) k = 1;
        matflag[i] = k;
    }
    std::vector<int> lblflag(labellist.size(), 0);
    for (i = 0; i < (int)labellist.size(); i++) {
        lblflag[i] = matflag[labellist[i].BlockType];
        if (labellist[i].InCircuit >= 0) lblflag[i] = 1;
    }

    // Set boundary conditions
    for (i = 0; i < NumNodes; i++) L.V[i] = -1;

    // Exterior edges → 0
    for (i = 0; i < NumEls; i++)
        for (j = 0; j < 3; j++)
            if (meshelem[i].n[j] == 1) {
                k = meshelem[i].p[plus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
                k = meshelem[i].p[minus1mod3[j]];
                if (!bOnAxis || IsKosher(k)) L.V[k] = 0;
            }

    // Selected blocks → 1; non-air non-selected → 0
    for (i = 0; i < NumEls; i++) {
        if (labellist[meshelem[i].lbl].IsSelected)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 1;
        else if (lblflag[meshelem[i].lbl] != 0)
            for (j = 0; j < 3; j++) L.V[meshelem[i].p[j]] = 0;
    }

    // Nodes with point currents not in selected region should be zero
    {
        std::vector<CComplex> pp;
        for (i = 0; i < (int)nodelist.size(); i++)
            if (!nodelist[i].BoundaryMarker.empty())
                pp.push_back(CComplex(nodelist[i].x, nodelist[i].y));

        int npts = (int)pp.size();
        if (npts > 0)
            for (i = 0; i < NumNodes; i++)
                for (j = 0; j < npts; j++)
                    if (abs(pp[j] - CComplex(meshnode[i].x, meshnode[i].y)) < 1.e-8) {
                        if (L.V[i] < 0) L.V[i] = 0;
                        npts--;
                        if (npts > 0) { pp[j] = pp[npts]; j = npts; }
                        else { j = npts; i = NumNodes; }
                    }
    }

    // Individual selected nodes → 1
    for (i = 0; i < NumNodes; i++)
        if (meshnode[i].IsSelected) L.V[i] = 1;

    // Build element matrices (Laplace stiffness)
    for (i = 0; i < NumEls; i++) {
        for (j = 0; j < 3; j++) { for (k = 0; k < 3; k++) Me[j][k] = 0; be[j] = 0; }
        for (k = 0; k < 3; k++) n[k] = meshelem[i].p[k];
        p[0] = meshnode[n[1]].y - meshnode[n[2]].y;
        p[1] = meshnode[n[2]].y - meshnode[n[0]].y;
        p[2] = meshnode[n[0]].y - meshnode[n[1]].y;
        q[0] = meshnode[n[2]].x - meshnode[n[1]].x;
        q[1] = meshnode[n[0]].x - meshnode[n[2]].x;
        q[2] = meshnode[n[1]].x - meshnode[n[0]].x;
        a = (p[0] * q[1] - p[1] * q[0]) / 2.0;

        // Validity check
        if (!labellist[meshelem[i].lbl].IsSelected && lblflag[meshelem[i].lbl]) {
            int cnt = 0;
            for (j = 0; j < 3; j++) if (L.V[n[j]] == 0) cnt++;
            if (cnt < 3) return false;  // invalid selection
        }

        // Weighting
        switch (WeightingScheme) {
        case 1: v = 1; break;
        case 2: v = sqrt(a); break;
        case 3: case 4: v = 1; break;
        default:
            v = labellist[meshelem[i].lbl].MaxArea;
            if (v <= 0) v = sqrt(a); else v = sqrt(v);
            break;
        }

        // Stiffness matrix
        for (j = 0; j < 3; j++)
            for (k = 0; k < 3; k++)
                Me[j][k] += v * (p[j] * p[k] + q[j] * q[k]) / a;

        // Apply prescribed values
        for (j = 0; j < 3; j++) {
            if (L.V[n[j]] >= 0) {
                for (k = 0; k < 3; k++) {
                    if (j != k) { be[k] -= Me[k][j] * L.V[n[j]]; Me[k][j] = 0; Me[j][k] = 0; }
                }
                be[j] = L.V[n[j]] * Me[j][j];
            }
        }

        // Assemble into global
        for (j = 0; j < 3; j++) {
            for (k = j; k < 3; k++)
                if (Me[j][k] != 0) L.Put(L.Get(n[j], n[k]) - Me[j][k], n[j], n[k]);
            L.b[n[j]] -= be[j];
        }
    }

    // Solve — PCGSolve needs a non-null TheView. Quiet: this in-process solve
    // must not render its progress bar to the GUI/headless terminal; GUI
    // progress (if wanted) arrives via progressCallback instead.
    CFknDlg dummyView(true);
    if (progressCallback)
        dummyView.m_prog1.onProgress = progressCallback;
    L.TheView = &dummyView;
    if (L.PCGSolve(0) == 0) return false;

    // Copy solution to mesh nodes with optional warping
    for (i = 0; i < NumNodes; i++) {
        switch (WeightingScheme) {
        case 0:
            meshnode[i].msk = (L.V[i] > 0.5) ? 1.0 : 0.0;
            break;
        case 3: {
            double val = L.V[i];
            meshnode[i].msk = (tanh(2.5) + tanh((-0.5 + val) * 5.0)) / (2.0 * tanh(2.5));
            break;
        }
        case 4: {
            double val = L.V[i];
            meshnode[i].msk = 3.0 * val * val - 2.0 * val * val * val;
            break;
        }
        default:
            meshnode[i].msk = L.V[i];
            break;
        }
    }
    bHasMask = true;
    return true;
}

// ================================================================
// HenrotteVector — gradient of mask function for weighted stress tensor
// ================================================================

CComplex ResultsDoc::HenrotteVector(int k) const
{
    int n[3];
    double b[3], c[3], da;
    for (int i = 0; i < 3; i++) n[i] = meshelem[k].p[i];
    b[0] = meshnode[n[1]].y - meshnode[n[2]].y;
    b[1] = meshnode[n[2]].y - meshnode[n[0]].y;
    b[2] = meshnode[n[0]].y - meshnode[n[1]].y;
    c[0] = meshnode[n[2]].x - meshnode[n[1]].x;
    c[1] = meshnode[n[0]].x - meshnode[n[2]].x;
    c[2] = meshnode[n[1]].x - meshnode[n[0]].x;
    da = (b[0] * c[1] - b[1] * c[0]);

    CComplex v(0, 0);
    double lc = LengthConv();
    for (int i = 0; i < 3; i++)
        v = v - meshnode[n[i]].msk * CComplex(b[i], c[i]) / (da * lc);
    return v;
}

// ================================================================
// AECF — air-core equivalent conductivity factor
// ================================================================

double ResultsDoc::AECF(int k) const
{
    if (!ProblemType) return 1.0;
    if (k < 0 || k >= (int)meshelem.size()) return 1.0;
    if (meshelem[k].lbl < 0 || meshelem[k].lbl >= (int)labellist.size()) return 1.0;
    if (!labellist[meshelem[k].lbl].IsExternal) return 1.0;

    double r = abs(meshelem[k].ctr - CComplex(0, extZo));
    return (r * r * extRi) / (extRo * extRo * extRo);
}

// ================================================================
// GetJA — current density and vector potential at element nodes
// ================================================================

void ResultsDoc::GetJA(int k, CComplex *J, CComplex *A)
{
    // Ported from OF FemmviewDoc.cpp — returns J in A/m^2
    int n[3];
    for (int i = 0; i < 3; i++) n[i] = meshelem[k].p[i];

    int blk = meshelem[k].blk;
    int lbl = meshelem[k].lbl;
    double lc = LengthConv();

    // Vector potential at nodes
    for (int i = 0; i < 3; i++) {
        if (ProblemType == 0)
            A[i] = meshnode[n[i]].A;
        else {
            double rn = meshnode[n[i]].x * lc;
            if (fabs(meshnode[n[i]].x) < 1e-6)
                A[i] = CComplex(0, 0);
            else
                A[i] = meshnode[n[i]].A / (2.0 * PI * rn);
        }
    }

    double r = 0;
    if (ProblemType == 1) r = Re(Ctr(k)) * lc;

    // Contribution from explicitly specified J
    CComplex Jsrc = blockproplist[blk].Jsrc;
    for (int i = 0; i < 3; i++) J[i] = Jsrc;

    // Determine effective conductivity
    double c = blockproplist[blk].Cduct;
    if (blockproplist[blk].Lam_d != 0 && blockproplist[blk].LamType == 0) c = 0;
    if (lbl >= 0 && lbl < (int)labellist.size() && labellist[lbl].FillFactor > 0) c = 0;

    // Contribution from eddy currents
    if (Frequency != 0)
        for (int i = 0; i < 3; i++)
            J[i] = J[i] - CComplex(0, 1) * Frequency * 2.0 * PI * c * A[i];

    // Contribution from circuit currents
    if (lbl >= 0 && lbl < (int)labellist.size()) {
        int crc = labellist[lbl].InCircuit;
        if (crc >= 0) {
            if (labellist[lbl].Case == 0) {
                // Voltage-driven
                if (ProblemType == 0) {
                    for (int i = 0; i < 3; i++)
                        J[i] = J[i] - c * labellist[lbl].dVolts;
                } else {
                    for (int i = 0; i < 3; i++) {
                        double rn = meshnode[n[i]].x * lc;
                        if (fabs(meshnode[n[i]].x) < 1e-6)
                            J[i] = J[i] - c * labellist[lbl].dVolts / r;
                        else
                            J[i] = J[i] - c * labellist[lbl].dVolts / rn;
                    }
                }
            }
            else {
                // Current-driven
                for (int i = 0; i < 3; i++)
                    J[i] = J[i] + labellist[lbl].J;
            }
        }
    }

    // Convert to A/m^2
    for (int i = 0; i < 3; i++) J[i] = J[i] * 1e6;
}

// ================================================================
// Material energy functions
// ================================================================

double RMaterialProp::GetB(double hc) const
{
    if (BHpoints == 0) return muo * mu_x * hc;
    if (Bdata.empty() || slope.empty()) return 0;

    // Newton iteration: find b such that GetH(b) = hc
    double b = 0, bo;
    int iter = 0;
    do {
        bo = b;
        double dHdB = Re(GetdHdB(bo));  // GetdHdB is now complex; DC path uses Re()
        if (fabs(dHdB) < 1e-30) break;  // avoid division by zero
        b = bo + (hc - GetH(bo)) / dHdB;
        if (++iter > 200) break;  // safety limit
    } while (fabs(b - bo) > 1e-8);
    return b;
}

CComplex RMaterialProp::GetdHdB(double B) const
{
    // Analytic derivative dH/dB of the cubic-Hermite spline. Complex-valued for
    // the apparent AC B-H curve. Ported from OF Problem.cpp. Real callers (e.g.
    // GetB) take Re().
    double b = fabs(B);

    if (BHpoints == 0) return CComplex(b / (mu_x * muo));

    if (b > Bdata[BHpoints - 1])
        return slope[BHpoints - 1];

    for (int i = 0; i < BHpoints - 1; i++) {
        if (b >= Bdata[i] && b <= Bdata[i + 1]) {
            double l = Bdata[i + 1] - Bdata[i];
            double z = (b - Bdata[i]) / l;
            CComplex h = 6.0 * z * (z - 1.0) * Hdata[i] / l +
                         (1.0 - 4.0 * z + 3.0 * z * z) * slope[i] +
                         6.0 * z * (1.0 - z) * Hdata[i + 1] / l +
                         z * (3.0 * z - 2.0) * slope[i + 1];
            return h;
        }
    }
    return CComplex(0);
}

double RMaterialProp::GetEnergy(double b) const
{
    if (BHpoints == 0 || b <= 0) return b * b / (2.0 * mu_x * muo);
    // Integrate H dB from 0 to b using trapezoidal rule on BH curve
    double nrg = 0;
    double bprev = 0, hprev = 0;
    for (int i = 0; i < BHpoints && Bdata[i] <= b; i++) {
        double bi = Bdata[i], hi = Hdata[i].re;
        nrg += 0.5 * (hi + hprev) * (bi - bprev);
        bprev = bi; hprev = hi;
    }
    // Add remainder from last BH point to b
    double hb = GetH(b);
    nrg += 0.5 * (hb + hprev) * (b - bprev);
    return nrg;
}

double RMaterialProp::GetCoEnergy(double b) const
{
    // CoEnergy = B*H(B) - Energy(B)
    if (BHpoints == 0) return GetEnergy(b);  // linear: E = coE
    return b * GetH(b) - GetEnergy(b);
}

double RMaterialProp::DoEnergy(double b1, double b2) const
{
    if (BHpoints == 0) {
        // Linear material
        double h1, h2;
        if (LamType == 0) {
            h1 = b1 / ((1 + LamFill * (mu_x - 1)) * muo);
            h2 = b2 / ((1 + LamFill * (mu_y - 1)) * muo);
        }
        else if (LamType == 1) {
            h1 = b1 / ((1 + LamFill * (mu_x - 1)) * muo);
            h2 = b2 * (LamFill / (mu_y * muo) + (1 - LamFill) / muo);
        }
        else {
            h1 = b1 * (LamFill / (mu_x * muo) + (1 - LamFill) / muo);
            h2 = b2 / ((1 + LamFill * (mu_y - 1)) * muo);
        }
        return (h1 * b1 + h2 * b2) / 2.0;
    }

    // Nonlinear
    double biron;
    if (LamType == 0) return GetEnergy(sqrt(b1 * b1 + b2 * b2));
    if (LamType == 1) {
        biron = sqrt(sqr(b1 / LamFill) + b2 * b2);
        return LamFill * GetEnergy(biron) + (1 - LamFill) * b2 * b2 / (2 * muo);
    }
    biron = sqrt(sqr(b2 / LamFill) + b1 * b1);
    return LamFill * GetEnergy(biron) + (1 - LamFill) * b1 * b1 / (2 * muo);
}

double RMaterialProp::DoCoEnergy(double b1, double b2) const
{
    if (BHpoints == 0) return DoEnergy(b1, b2);
    double biron;
    if (LamType == 0) return GetCoEnergy(sqrt(b1 * b1 + b2 * b2));
    if (LamType == 1) {
        biron = sqrt(sqr(b1 / LamFill) + b2 * b2);
        return LamFill * GetCoEnergy(biron) + (1 - LamFill) * b2 * b2 / (2 * muo);
    }
    biron = sqrt(sqr(b2 / LamFill) + b1 * b1);
    return LamFill * GetCoEnergy(biron) + (1 - LamFill) * b1 * b1 / (2 * muo);
}

double RMaterialProp::DoCoEnergy(CComplex b1, CComplex b2) const
{
    return DoEnergy(b1, b2);
}

double RMaterialProp::DoEnergy(CComplex b1, CComplex b2) const
{
    CComplex mu1c, mu2c, h1, h2;
    GetMu(b1, b2, mu1c, mu2c);
    h1 = b1 / (mu1c * muo);
    h2 = b2 / (mu2c * muo);
    return (h1.re * b1.re + h1.im * b1.im + h2.re * b2.re + h2.im * b2.im) / 4.0;
}

// ================================================================
// BlockIntegral — integrate over selected blocks
// Ported from FemmviewDoc.cpp:3046-3545
// ================================================================

CComplex ResultsDoc::BlockIntegral(int type)
{
    // Handle recursive total losses case
    if (type == 6) return BlockIntegral(3) + BlockIntegral(4);

    // Weighted stress tensor integrals need the mask
    static const int maskTypes[] = {18,19,20,21,22,23,25,26,27};
    bool bMaskIntegral = false;
    for (int mt : maskTypes)
        if (mt == type) { bMaskIntegral = true; break; }
    if (bMaskIntegral) {
        if (!MakeMask()) return CComplex(0, 0);
    }

    CComplex z(0, 0);
    double lc = LengthConv();
    double lc2 = lc * lc;

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl < 0 || meshelem[i].lbl >= (int)labellist.size()) continue;
        // Mask integrals iterate over ALL elements; regular integrals only selected
        if (!bMaskIntegral && !labellist[meshelem[i].lbl].IsSelected) continue;

        double a = ElmArea(i) * lc2;
        double R = meshelem[i].ctr.re * lc;
        // For mask (Henrotte) integrals, a needs depth/2piR scaling upfront
        // (regular integrals apply it per-case)
        double ah = a;
        if (bMaskIntegral) {
            if (ProblemType == 1) {
                double Rh = 0;
                for (int j = 0; j < 3; j++) Rh += meshnode[meshelem[i].p[j]].x * lc;
                Rh /= 3.0;
                ah *= 2.0 * PI * Rh;
            } else ah *= Depth;
        }
        int n[3];
        for (int j = 0; j < 3; j++) n[j] = meshelem[i].p[j];
        double r[3];
        for (int j = 0; j < 3; j++) r[j] = meshnode[n[j]].x * lc;

        CComplex Jn[3], An[3];
        GetJA(i, Jn, An);

        CComplex J(0, 0);
        for (int j = 0; j < 3; j++) J = J + Jn[j] / 3.0;

        CComplex U[3] = { CComplex(1,0), CComplex(1,0), CComplex(1,0) };
        CComplex V[3];

        CComplex B1 = meshelem[i].B1;
        CComplex B2 = meshelem[i].B2;
        CComplex c, y, F1, F2;
        CComplex mu1c, mu2c, H1, H2;

        switch (type) {
        case 0: { // A * J (flux linkage-related)
            CComplex Vc[3];
            for (int k = 0; k < 3; k++) Vc[k] = conj(Jn[k]);
            if (ProblemType == 0)
                y = PlnInt(a, An, Vc) * Depth;
            else
                y = AxiInt(a, An, Vc, r);
            z = z + y;
            break;
        }

        case 1: // Integral of A
            if (ProblemType == 0)
                y = PlnInt(a, U, An) * Depth;
            else
                y = AxiInt(a, U, An, r);
            z = z + y;
            break;

        case 2: { // Stored energy
            // ============================================================
            // NOTE: Energy computation for block integrals.
            // The "local stored energy" part for wound conductors (LamType>2)
            // is super-small and may interfere with future transient solution
            // capability. It's kept here for now to match OF, but may be a
            // candidate for removal later. — DM
            // ============================================================
            if (ProblemType == 1) a *= 2.0 * PI * R; else a *= Depth;

            if (Frequency != 0) {
                // AC: special handling for wound regions with prox/skin effects
                if (blockproplist[meshelem[i].blk].LamType > 2) {
                    CComplex mu = muo * labellist[meshelem[i].lbl].mu;
                    double localE = labellist[meshelem[i].lbl].LocalEnergy;
                    y = a * Re(B1 * conj(B1) + B2 * conj(B2)) * Re(1.0 / mu) / 4.0;
                    y = y + a * Re(J * conj(J)) * localE / 4.0;
                } else {
                    y = a * blockproplist[meshelem[i].blk].DoEnergy(B1, B2);
                }
            }
            else {
                // DC: correct energy for PM second-quadrant representation
                int bk = meshelem[i].blk;
                if (blockproplist[bk].H_c != 0) {
                    if (blockproplist[bk].BHpoints == 0) {
                        // Linear PM
                        double mu1v = blockproplist[bk].mu_x;
                        double mu2v = blockproplist[bk].mu_y;
                        double h1v = B1.re / (mu1v * muo);
                        double h2v = B2.re / (mu2v * muo);
                        CComplex Hc = blockproplist[bk].H_c *
                            CComplex(cos(meshelem[i].magdir * PI / 180.0),
                                     sin(meshelem[i].magdir * PI / 180.0));
                        h1v -= Hc.re;
                        h2v -= Hc.im;
                        y = a * 0.5 * muo * (mu1v * h1v * h1v + mu2v * h2v * h2v);
                    } else {
                        // Nonlinear PM
                        y = blockproplist[bk].DoEnergy(B1.re, B2.re);
                        y = y + blockproplist[bk].Nrg
                              - blockproplist[bk].H_c * Re((B1.re + CComplex(0,1) * B2.re) /
                                exp(CComplex(0,1) * PI * meshelem[i].magdir / 180.0));
                        y = y * a;
                    }
                } else {
                    y = a * blockproplist[bk].DoEnergy(B1.re, B2.re);
                }
                // Add "local" stored energy for wound conductors (DC prox/skin)
                if (blockproplist[meshelem[i].blk].LamType > 2) {
                    double localE = labellist[meshelem[i].lbl].LocalEnergy;
                    y += a * Re(J * J) * localE / 2.0;
                }
            }
            y = y * AECF(i);  // correction for axisymmetric external region
            z = z + y;
            break;
        }

        case 3: // Hysteresis & laminated eddy losses
            if (Frequency != 0) {
                if (ProblemType == 1) a *= 2.0 * PI * R; else a *= Depth;
                GetMu(B1, B2, mu1c, mu2c, i);
                H1 = B1 / (mu1c * muo);
                H2 = B2 / (mu2c * muo);
                // y = a * pi * f * Im(H1*B1* + H2*B2*)
                y = CComplex(a * PI * Frequency *
                    (H1.re * B1.im - H1.im * B1.re + H2.re * B2.im - H2.im * B2.re), 0);
                z = z + y;
            }
            break;

        case 4: { // Resistive losses
            double sig = 0;
            if (abs(labellist[meshelem[i].lbl].o) != 0)
                sig = 1e6 / Re(1.0 / labellist[meshelem[i].lbl].o);
            if (blockproplist[meshelem[i].blk].Lam_d != 0 &&
                blockproplist[meshelem[i].blk].LamType == 0) sig = 0;
            if (sig != 0) {
                if (ProblemType == 0) {
                    for (int k = 0; k < 3; k++) V[k] = conj(Jn[k]) / sig;
                    y = PlnInt(a, Jn, V) * Depth;
                }
                else
                    y = 2.0 * PI * R * a * J * conj(J) / sig;
                if (Frequency != 0) y = y * 0.5;
                z = z + y;
            }
            break;
        }

        case 5: // Cross-section area
            z.re += ElmArea(i) * lc2;
            break;

        case 7: // Total current in block
            z = z + a * J;
            break;

        case 8: // Integral of B1
            if (ProblemType == 1) z = z + a * 2.0 * PI * R * B1;
            else z = z + a * Depth * B1;
            break;

        case 9: // Integral of B2
            if (ProblemType == 1) z = z + a * 2.0 * PI * R * B2;
            else z = z + a * Depth * B2;
            break;

        case 10: // Volume
            if (ProblemType == 0) z.re += a * Depth;
            else z.re += a * 2.0 * PI * R;
            break;

        case 11: { // x Lorentz force, SS
            y = CComplex(-(B2.re * J.re + B2.im * J.im), 0);
            if (ProblemType == 1) y = CComplex(0, 0); else y = y * Depth;
            if (Frequency != 0) y = y * 0.5;
            z = z + a * y;
            break;
        }

        case 12: { // y Lorentz force, SS
            for (int k = 0; k < 3; k++) V[k] = CComplex(B1.re * Jn[k].re + B1.im * Jn[k].im, 0);
            if (ProblemType == 0) y = PlnInt(a, U, V) * Depth;
            else y = AxiInt(a * (-1), U, V, r);
            if (Frequency != 0) y = y * 0.5;
            z = z + y;
            break;
        }

        case 13: // x Lorentz force, 2x
            if (Frequency != 0 && ProblemType == 0) {
                y = CComplex(-(B2.re * J.re - B2.im * J.im), -(B2.re * J.im + B2.im * J.re));
                z = z + 0.5 * a * y * Depth;
            }
            break;

        case 14: // y Lorentz force, 2x
            if (Frequency != 0) {
                y = CComplex(B1.re * J.re - B1.im * J.im, B1.re * J.im + B1.im * J.re);
                if (ProblemType == 1) y = y * (-2.0 * PI * R); else y = y * Depth;
                z = z + a * y * 0.5;
            }
            break;

        case 15: // Lorentz torque, SS
            if (ProblemType == 0) {
                c = Ctr(i) * lc;
                y = CComplex(c.im * (B2.re * J.re + B2.im * J.im) +
                             c.re * (B1.re * J.re + B1.im * J.im), 0);
                if (Frequency != 0) y = y * 0.5;
                z = z + a * y * Depth;
            }
            break;

        case 16: // Lorentz torque, 2x
            if (Frequency != 0 && ProblemType == 0) {
                c = Ctr(i) * lc;
                y = c.re * CComplex(B1.re * J.re - B1.im * J.im, B1.re * J.im + B1.im * J.re) +
                    c.im * CComplex(B2.re * J.re - B2.im * J.im, B2.re * J.im + B2.im * J.re);
                z = z + 0.5 * a * y * Depth;
            }
            break;

        case 17: { // Coenergy
            // ============================================================
            // NOTE: Same "local stored energy" caveat as case 2 above.
            // The LocalEnergy contribution for wound conductors (LamType>2)
            // is kept to match OF but may be removed for transient. — DM
            // ============================================================
            if (ProblemType == 1) a *= 2.0 * PI * R; else a *= Depth;

            if (Frequency != 0) {
                if (blockproplist[meshelem[i].blk].LamType > 2) {
                    CComplex mu = muo * labellist[meshelem[i].lbl].mu;
                    double localE = labellist[meshelem[i].lbl].LocalEnergy;
                    y = a * Re(B1 * conj(B1) + B2 * conj(B2)) * Re(1.0 / mu) / 4.0;
                    y = y + a * Re(J * conj(J)) * localE / 4.0;
                } else {
                    y = a * blockproplist[meshelem[i].blk].DoCoEnergy(B1, B2);
                }
            } else {
                y = a * blockproplist[meshelem[i].blk].DoCoEnergy(B1.re, B2.re);
                // Local stored energy for wound conductors (DC case)
                if (blockproplist[meshelem[i].blk].LamType > 2) {
                    double localE = labellist[meshelem[i].lbl].LocalEnergy;
                    y = y + a * Re(J * J) * localE / 2.0;
                }
            }
            y = y * AECF(i);  // correction for axisymmetric external region
            z = z + y;
            break;
        }

        case 18: // x Henrotte force, SS
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            y = ((B1 * conj(B1) - B2 * conj(B2)) * c.re + 2.0 * CComplex(B1.re*B2.re + B1.im*B2.im, 0) * c.im) / (2.0 * muo);
            if (Frequency != 0) y = y * 0.5;
            y = y * AECF(i);
            z = z + ah * y;
            break;

        case 19: // y Henrotte force, SS
            c = HenrotteVector(i);
            y = ((B2 * conj(B2) - B1 * conj(B1)) * c.im + 2.0 * CComplex(B1.re*B2.re + B1.im*B2.im, 0) * c.re) / (2.0 * muo);
            y = y * AECF(i);
            if (Frequency != 0) y = y * 0.5;
            z = z + ah * y;
            break;

        case 20: // x Henrotte force, 2x
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            z = z + ah * (((B1 * B1 - B2 * B2) * c.re + 2.0 * B1 * B2 * c.im) / (4.0 * muo)) * AECF(i);
            break;

        case 21: // y Henrotte force, 2x
            c = HenrotteVector(i);
            z = z + ah * (((B2 * B2 - B1 * B1) * c.im + 2.0 * B1 * B2 * c.re) / (4.0 * muo)) * AECF(i);
            break;

        case 22: // Henrotte torque, SS
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            F1 = ((B1 * conj(B1) - B2 * conj(B2)) * c.re + 2.0 * CComplex(B1.re*B2.re + B1.im*B2.im, 0) * c.im) / (2.0 * muo);
            F2 = ((B2 * conj(B2) - B1 * conj(B1)) * c.im + 2.0 * CComplex(B1.re*B2.re + B1.im*B2.im, 0) * c.re) / (2.0 * muo);
            c = Ctr(i) * lc;
            y = c.re * F2 - c.im * F1;
            if (Frequency != 0) y = y * 0.5;
            y = y * AECF(i);
            z = z + ah * y;
            break;

        case 23: // Henrotte torque, 2x
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            F1 = ((B1 * B1 - B2 * B2) * c.re + 2.0 * B1 * B2 * c.im) / (4.0 * muo);
            F2 = ((B2 * B2 - B1 * B1) * c.im + 2.0 * B1 * B2 * c.re) / (4.0 * muo);
            c = Ctr(i) * lc;
            z = z + ah * (c.re * F2 - c.im * F1) * AECF(i);
            break;

        case 25: // x Henrotte force, 1x incremental
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            z = z + ah * ((B1 * meshelem[i].B1p - B2 * meshelem[i].B2p) * c.re +
                         (meshelem[i].B1p * B2 + B1 * meshelem[i].B2p) * c.im) / muo * AECF(i);
            break;

        case 26: // y Henrotte force, 1x incremental
            c = HenrotteVector(i);
            z = z + ah * ((meshelem[i].B1p * B2 + B1 * meshelem[i].B2p) * c.re +
                         (-(B1 * meshelem[i].B1p) + B2 * meshelem[i].B2p) * c.im) / muo * AECF(i);
            break;

        case 27: // Henrotte torque, 1x incremental
            if (ProblemType != 0) break;
            c = HenrotteVector(i);
            F1 = ((B1 * meshelem[i].B1p - B2 * meshelem[i].B2p) * c.re +
                  (meshelem[i].B1p * B2 + B1 * meshelem[i].B2p) * c.im) / muo;
            F2 = ((meshelem[i].B1p * B2 + B1 * meshelem[i].B2p) * c.re +
                  (-(B1 * meshelem[i].B1p) + B2 * meshelem[i].B2p) * c.im) / muo;
            c = Ctr(i) * lc;
            z = z + ah * (c.re * F2 - c.im * F1) * AECF(i);
            break;

        case 24: { // Moment of inertia
            if (ProblemType == 1) {
                // Axisymmetric: moment of inertia about r=0 axis
                for (int k = 0; k < 3; k++) V[k] = CComplex(r[k], 0);
                y = AxiInt(a, V, V, r);
            } else {
                // Planar: moment of inertia about z-axis (x^2 + y^2)
                CComplex Ux[3], Vy[3];
                for (int k = 0; k < 3; k++) {
                    Ux[k] = CComplex(meshnode[meshelem[i].p[k]].x * lc, 0);
                    Vy[k] = CComplex(meshnode[meshelem[i].p[k]].y * lc, 0);
                }
                y = Ux[0]*Ux[0] + Ux[1]*Ux[1] + Ux[2]*Ux[2];
                y = y + Ux[0]*Ux[1] + Ux[0]*Ux[2] + Ux[1]*Ux[2];
                y = y + Vy[0]*Vy[0] + Vy[1]*Vy[1] + Vy[2]*Vy[2];
                y = y + Vy[0]*Vy[1] + Vy[0]*Vy[2] + Vy[1]*Vy[2];
                y = y * a * Depth / 6.0;
            }
            z = z + y;
            break;
        }

        case 28: { // x Lorentz force, 1x incremental
            double B2p = meshelem[i].B2p;
            double Jp = meshelem[i].Jp;
            y = -(CComplex(B2p, 0) * J + B2 * Jp);
            if (ProblemType == 1) y = CComplex(0, 0); else y = y * Depth;
            z = z + a * y;
            break;
        }

        case 29: { // y Lorentz force, 1x incremental
            double B1p = meshelem[i].B1p;
            double Jp = meshelem[i].Jp;
            for (int k = 0; k < 3; k++) {
                U[k] = CComplex(1, 0);
                V[k] = CComplex(B1p, 0) * J + B1 * Jp;
            }
            if (ProblemType == 0) y = PlnInt(a, U, V) * Depth;
            else y = AxiInt(-a, U, V, r);
            z = z + y;
            break;
        }

        case 30: // Lorentz torque, 1x incremental
            if (ProblemType == 0) {
                double B1p = meshelem[i].B1p;
                double B2p = meshelem[i].B2p;
                double Jp = meshelem[i].Jp;
                c = Ctr(i) * lc;
                y = c.im * (B2 * Jp + CComplex(B2p, 0) * J) +
                    c.re * (B1 * Jp + CComplex(B1p, 0) * J);
                z = z + a * y * Depth;
            }
            break;

        default:
            break;
        }
    }
    return z;
}

bool ResultsDoc::BlockIntegralIsComplex(int type) const {
    // Genuinely complex (phasor) block-integral types in an AC solve: phasor
    // field integrals (0 A.J, 1 integral-of-A, 7 total current, 8/9 integral-of-B)
    // and the oscillating force/torque components (2x parts 13/14/16/20/21/23,
    // 1x-incremental parts 25/26/27/28/29/30). Everything else — energy, losses,
    // area, volume, moment of inertia, and the steady-state (time-averaged)
    // force/torque parts — is real. See mo_blockintegral_cases.xlsx.
    switch (type) {
        case 0:  case 1:  case 7:  case 8:  case 9:
        case 13: case 14: case 16: case 20: case 21: case 23:
        case 25: case 26: case 27: case 28: case 29: case 30:
            return true;
        default:
            return false;
    }
}

// ================================================================
// LineIntegral — integrate along contour path
// Ported from FemmviewDoc.cpp:3547-3895
// ================================================================

void ResultsDoc::LineIntegral(int type, CComplex *z)
{
    // Ported from OF FemmviewDoc.cpp LineIntegral
    // Types: 0=B.n, 1=H.t, 2=contour length, 3=force, 4=torque, 5=(B.n)^2
    z[0] = z[1] = z[2] = z[3] = CComplex(0, 0);
    if (contour.size() < 2) return;

    double lc = LengthConv();
    int kk = (int)contour.size();

    // Type 0: B.n (normal flux from A difference at endpoints)
    if (type == 0) {
        PointVals u0, u1;
        GetPointValues(contour[0].re, contour[0].im, u0);
        GetPointValues(contour[kk - 1].re, contour[kk - 1].im, u1);

        if (ProblemType == 0) {
            double l = 0;
            for (int i = 0; i < kk - 1; i++) l += abs(contour[i + 1] - contour[i]);
            l *= lc;
            z[0] = (u0.A - u1.A) * Depth;
            if (l != 0) z[1] = z[0] / (l * Depth);
        } else {
            double l = 0;
            for (int i = 0; i < kk - 1; i++)
                l += PI * (contour[i].re + contour[i + 1].re) * abs(contour[i + 1] - contour[i]);
            l *= lc * lc;
            z[0] = u1.A - u0.A;  // reversed sign for axisymmetric
            if (l != 0) z[1] = z[0] / l;
        }
        return;
    }

    // Type 2: Contour length and surface area
    if (type == 2) {
        for (int i = 0; i < kk - 1; i++)
            z[0].re += abs(contour[i + 1] - contour[i]);
        z[0].re *= lc;
        if (ProblemType == 1) {
            for (int i = 0; i < kk - 1; i++)
                z[0].im += PI * (contour[i].re + contour[i + 1].re) * abs(contour[i + 1] - contour[i]);
            z[0].im *= lc * lc;
        }
        return;
    }

    // Types 1, 3, 4, 5: point sampling along contour with element caching
    int NumPlotPoints = 400;

    if (type == 1) {
        // H.t (MMF drop along contour)
        for (int seg = 1; seg < kk; seg++) {
            double dz = abs(contour[seg] - contour[seg - 1]) / (double)NumPlotPoints;
            int elm = -1;
            for (int j = 0; j < NumPlotPoints; j++) {
                double u = (j + 0.5) / (double)NumPlotPoints;
                CComplex pt = contour[seg - 1] + u * (contour[seg] - contour[seg - 1]);
                CComplex t = (contour[seg] - contour[seg - 1]) / abs(contour[seg] - contour[seg - 1]);
                CComplex n = CComplex(0, 1) * t;
                pt = pt + n * 1e-6;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    bool found = false;
                    for (int jj = 0; jj < 3 && !found; jj++)
                        for (int m = 0; m < NumList[meshelem[elm].p[jj]] && !found; m++) {
                            int test = ConList[meshelem[elm].p[jj]][m];
                            if (InTriangleTest(pt.re, pt.im, test)) { elm = test; found = true; }
                        }
                    if (!found) elm = InTriangle(pt.re, pt.im);
                }

                if (elm >= 0) {
                    PointVals v;
                    if (GetPointValues(pt.re, pt.im, elm, v)) {
                        CComplex Ht = t.re * v.H1 + t.im * v.H2;
                        z[0] = z[0] + Ht * dz * lc;
                    }
                }
            }
        }
        // Average H.t
        double l = 0;
        for (int i = 0; i < kk - 1; i++) l += abs(contour[i + 1] - contour[i]);
        l *= lc;
        if (l != 0) z[1] = z[0] / l;
        return;
    }

    if (type == 3) {
        // Force from stress tensor (DC: z[0],z[1]; AC adds z[2],z[3])
        for (int seg = 1; seg < kk; seg++) {
            double dz = abs(contour[seg] - contour[seg - 1]) / (double)NumPlotPoints;
            int elm = -1;
            for (int j = 0; j < NumPlotPoints; j++) {
                double u = (j + 0.5) / (double)NumPlotPoints;
                CComplex pt = contour[seg - 1] + u * (contour[seg] - contour[seg - 1]);
                CComplex t = (contour[seg] - contour[seg - 1]) / abs(contour[seg] - contour[seg - 1]);
                CComplex n = CComplex(0, 1) * t;
                pt = pt + n * 1e-6;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    bool found = false;
                    for (int jj = 0; jj < 3 && !found; jj++)
                        for (int m = 0; m < NumList[meshelem[elm].p[jj]] && !found; m++) {
                            int test = ConList[meshelem[elm].p[jj]][m];
                            if (InTriangleTest(pt.re, pt.im, test)) { elm = test; found = true; }
                        }
                    if (!found) elm = InTriangle(pt.re, pt.im);
                }

                if (elm >= 0) {
                    PointVals v;
                    if (GetPointValues(pt.re, pt.im, elm, v)) {
                        CComplex Hn = n.re * v.H1 + n.im * v.H2;
                        CComplex Bn = n.re * v.B1 + n.im * v.B2;
                        CComplex BH = v.B1 * v.H1 + v.B2 * v.H2;
                        CComplex dF1 = v.H1 * Bn + v.B1 * Hn - n.re * BH;
                        CComplex dF2 = v.H2 * Bn + v.B2 * Hn - n.im * BH;

                        double dza = dz * lc;
                        if (ProblemType == 1) { dza *= 2.0 * PI * pt.re * lc; dF1 = 0; }
                        else dza *= Depth;

                        if (Frequency == 0) {
                            z[0] = z[0] + dF1 * dza / 2.0;
                            z[1] = z[1] + dF2 * dza / 2.0;
                        } else {
                            z[0] = z[0] + dF1 * dza / 4.0;
                            z[1] = z[1] + dF2 * dza / 4.0;
                            // 2X component
                            BH = v.B1 * conj(v.H1) + v.B2 * conj(v.H2);
                            if (ProblemType != 1)
                                dF1 = v.H1 * conj(Bn) + v.B1 * conj(Hn) - n.re * BH;
                            dF2 = v.H2 * conj(Bn) + v.B2 * conj(Hn) - n.im * BH;
                            z[2] = z[2] + dF1 * dza / 4.0;
                            z[3] = z[3] + dF2 * dza / 4.0;
                        }
                    }
                }
            }
        }
        return;
    }

    if (type == 4) {
        // Torque from stress tensor
        for (int seg = 1; seg < kk; seg++) {
            double dz = abs(contour[seg] - contour[seg - 1]) / (double)NumPlotPoints;
            int elm = -1;
            for (int j = 0; j < NumPlotPoints; j++) {
                double u = (j + 0.5) / (double)NumPlotPoints;
                CComplex pt = contour[seg - 1] + u * (contour[seg] - contour[seg - 1]);
                CComplex t = (contour[seg] - contour[seg - 1]) / abs(contour[seg] - contour[seg - 1]);
                CComplex n = CComplex(0, 1) * t;
                pt = pt + n * 1e-6;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    bool found = false;
                    for (int jj = 0; jj < 3 && !found; jj++)
                        for (int m = 0; m < NumList[meshelem[elm].p[jj]] && !found; m++) {
                            int test = ConList[meshelem[elm].p[jj]][m];
                            if (InTriangleTest(pt.re, pt.im, test)) { elm = test; found = true; }
                        }
                    if (!found) elm = InTriangle(pt.re, pt.im);
                }

                if (elm >= 0) {
                    PointVals v;
                    if (GetPointValues(pt.re, pt.im, elm, v)) {
                        CComplex Hn = n.re * v.H1 + n.im * v.H2;
                        CComplex Bn = n.re * v.B1 + n.im * v.B2;
                        CComplex BH = v.B1 * v.H1 + v.B2 * v.H2;
                        CComplex dF1 = v.H1 * Bn + v.B1 * Hn - n.re * BH;
                        CComplex dF2 = v.H2 * Bn + v.B2 * Hn - n.im * BH;
                        CComplex dT = pt.re * lc * dF2 - pt.im * lc * dF1;

                        double dza = dz * lc;
                        if (ProblemType == 1) dza *= 2.0 * PI * pt.re * lc;
                        else dza *= Depth;

                        if (Frequency == 0) {
                            z[0] = z[0] + dT * dza / 2.0;
                        } else {
                            z[0] = z[0] + dT * dza / 4.0;
                            // 2X component
                            BH = v.B1 * conj(v.H1) + v.B2 * conj(v.H2);
                            dF1 = v.H1 * conj(Bn) + v.B1 * conj(Hn) - n.re * BH;
                            dF2 = v.H2 * conj(Bn) + v.B2 * conj(Hn) - n.im * BH;
                            dT = pt.re * lc * dF2 - pt.im * lc * dF1;
                            z[1] = z[1] + dT * dza / 4.0;
                        }
                    }
                }
            }
        }
        return;
    }

    if (type == 5) {
        // (B.n)^2 integral
        for (int seg = 1; seg < kk; seg++) {
            double dz = abs(contour[seg] - contour[seg - 1]) / (double)NumPlotPoints;
            int elm = -1;
            for (int j = 0; j < NumPlotPoints; j++) {
                double u = (j + 0.5) / (double)NumPlotPoints;
                CComplex pt = contour[seg - 1] + u * (contour[seg] - contour[seg - 1]);
                CComplex t = (contour[seg] - contour[seg - 1]) / abs(contour[seg] - contour[seg - 1]);
                CComplex n = CComplex(0, 1) * t;
                pt = pt + n * 1e-6;

                if (elm < 0) elm = InTriangle(pt.re, pt.im);
                else if (!InTriangleTest(pt.re, pt.im, elm)) {
                    bool found = false;
                    for (int jj = 0; jj < 3 && !found; jj++)
                        for (int m = 0; m < NumList[meshelem[elm].p[jj]] && !found; m++) {
                            int test = ConList[meshelem[elm].p[jj]][m];
                            if (InTriangleTest(pt.re, pt.im, test)) { elm = test; found = true; }
                        }
                    if (!found) elm = InTriangle(pt.re, pt.im);
                }

                if (elm >= 0) {
                    PointVals v;
                    if (GetPointValues(pt.re, pt.im, elm, v)) {
                        CComplex Bn = n.re * v.B1 + n.im * v.B2;
                        z[0] = z[0] + Bn * conj(Bn) * dz * lc;
                    }
                }
            }
        }
        // Average (B.n)^2
        double l = 0;
        for (int i = 0; i < kk - 1; i++) l += abs(contour[i + 1] - contour[i]);
        l *= lc;
        if (l != 0) z[1] = z[0] / l;
        return;
    }
}

// ================================================================
// BendContour — bend last contour segment into an arc
// Ported from FemmviewDoc.cpp:4033-4068
// ================================================================

void ResultsDoc::BendContour(double angle, double anglestep)
{
    if (angle == 0) return;
    if (anglestep == 0) anglestep = 1;

    int k, n;
    double d, tta, dtta, R;
    CComplex c, a0, a1;

    k = (int)contour.size() - 1;
    if (k < 1) return;
    if (angle < -180.0 || angle > 180.0) return;

    n = (int)ceil(fabs(angle / anglestep));
    tta = angle * PI / 180.0;
    dtta = tta / (double)n;

    a1 = contour[k];
    contour.erase(contour.begin() + k);
    a0 = contour[k - 1];

    d = abs(a1 - a0);
    R = d / (2.0 * sin(fabs(tta / 2.0)));
    // Center side depends on the sign of the bend angle — the previous magnetics
    // rewrite hardcoded one side (and dropped the +/-180 guard above), which
    // traced negative-angle bends backwards. This restores the original
    // FemmviewDoc logic (matches curr/heat/elec).
    if (tta > 0)
        c = a0 + (R / d) * (a1 - a0) * exp(CComplex(0, 1) * (PI - tta) / 2.0);
    else
        c = a0 + (R / d) * (a1 - a0) * exp(CComplex(0, -1) * (PI + tta) / 2.0);

    for (k = 1; k <= n; k++)
        contour.push_back(c + (a0 - c) * exp(CComplex(0, k * dtta)));
}

void ResultsDoc::GetInputCircle(int arcIdx, CComplex &c, double &R) const
{
    CComplex a0(nodelist[arclist[arcIdx].n0].x, nodelist[arclist[arcIdx].n0].y);
    CComplex a1(nodelist[arclist[arcIdx].n1].x, nodelist[arclist[arcIdx].n1].y);
    double d = abs(a1 - a0);
    if (d == 0) { c = a0; R = 0; return; }
    CComplex t = (a1 - a0) / d;
    double tta = arclist[arcIdx].ArcLength * PI / 180.0;
    R = d / (2.0 * sin(tta / 2.0));
    c = a0 + (d / 2.0 + CComplex(0, 1) * sqrt(std::max(0.0, R * R - d * d / 4.0))) * t;
}

double ResultsDoc::ShortestDistanceFromInputArc(CComplex p, int arcIdx) const
{
    CComplex a0(nodelist[arclist[arcIdx].n0].x, nodelist[arclist[arcIdx].n0].y);
    CComplex a1(nodelist[arclist[arcIdx].n1].x, nodelist[arclist[arcIdx].n1].y);
    CComplex c; double R;
    GetInputCircle(arcIdx, c, R);
    double d = abs(p - c);
    if (d == 0) return R;
    CComplex t = (p - c) / d;
    double l = abs(p - c - R * t);
    double z = arg(t / (a0 - c)) * 180.0 / PI;
    if (z > 0 && z < arclist[arcIdx].ArcLength) return l;
    double d0 = abs(p - a0), d1 = abs(p - a1);
    return (d0 < d1) ? d0 : d1;
}

int ResultsDoc::ClosestInputNode(double x, double y) const
{
    if (nodelist.empty()) return -1;
    int best = 0;
    double bestDist = nodelist[0].GetDistance(x, y);
    for (int i = 1; i < (int)nodelist.size(); i++) {
        double d = nodelist[i].GetDistance(x, y);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

double ResultsDoc::ShortestDistanceFromSegment(double p, double q, int segm) const
{
    double x0 = nodelist[linelist[segm].n0].x;
    double y0 = nodelist[linelist[segm].n0].y;
    double x1 = nodelist[linelist[segm].n1].x;
    double y1 = nodelist[linelist[segm].n1].y;

    double dx = x1 - x0, dy = y1 - y0;
    double len2 = dx * dx + dy * dy;
    if (len2 == 0) return sqrt((p - x0) * (p - x0) + (q - y0) * (q - y0));

    double t = ((p - x0) * dx + (q - y0) * dy) / len2;
    if (t > 1.0) t = 1.0;
    if (t < 0.0) t = 0.0;

    double cx = x0 + t * dx, cy = y0 + t * dy;
    return sqrt((p - cx) * (p - cx) + (q - cy) * (q - cy));
}

// ================================================================
// Circuit results
// ================================================================

// ================================================================
// Circuit voltage/flux/linkage — ported from OF FemmviewDoc.cpp
// ================================================================

CComplex ResultsDoc::GetStrandedVoltageDrop(int lbl)
{
    CComplex dVolts(0, 0), rho;
    CComplex A[3], J[3], U[3], V[3];
    double a, atot = 0, r[3];
    double lc = LengthConv();

    U[0] = 1; U[1] = 1; U[2] = 1;

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl == lbl) {
            rho = labellist[meshelem[i].lbl].o * 1.e6;
            if (Frequency == 0) rho = CComplex(rho.re, 0);
            if (abs(rho) != 0) rho = 1.0 / rho;

            GetJA(i, J, A);
            a = ElmArea(i) * lc * lc;
            atot += a;

            if (ProblemType == 1)
                for (int k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;

            for (int k = 0; k < 3; k++)
                V[k] = 2.0 * PI * CComplex(0, 1) * Frequency * A[k] + rho * J[k];
            if (ProblemType == 0) dVolts += PlnInt(a, V, U) * Depth;
            else dVolts += AxiInt(a, V, U, r);
        }
    }
    if (atot > 0) dVolts *= ((double)labellist[lbl].Turns / atot);
    return dVolts;
}

CComplex ResultsDoc::GetStrandedLinkage(int lbl)
{
    CComplex FluxLinkage(0, 0);
    CComplex A[3], J[3], U[3];
    double a, atot = 0, r[3];
    double lc = LengthConv();

    U[0] = 1; U[1] = 1; U[2] = 1;

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl == lbl) {
            GetJA(i, J, A);
            a = ElmArea(i) * lc * lc;
            atot += a;

            if (ProblemType == 1)
                for (int k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;

            if (ProblemType == 0) FluxLinkage += PlnInt(a, A, U) * Depth;
            else FluxLinkage += AxiInt(a, A, U, r);
        }
    }
    if (atot > 0) FluxLinkage *= ((double)labellist[lbl].Turns / atot);
    return FluxLinkage;
}

CComplex ResultsDoc::GetSolidAxisymmetricLinkage(int lbl)
{
    CComplex FluxLinkage(0, 0), Aa;
    CComplex A[3], J[3];
    double a, atot = 0, R, r[3];
    double lc = LengthConv();

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (meshelem[i].lbl == lbl) {
            GetJA(i, J, A);
            Aa = (A[0] + A[1] + A[2]) / 3.0;
            a = ElmArea(i) * lc * lc;

            for (int k = 0; k < 3; k++)
                r[k] = meshnode[meshelem[i].p[k]].x * lc;
            R = (r[0] + r[1] + r[2]) / 3.0;

            if (R > 0) {
                atot += a / R;
                FluxLinkage += 2.0 * PI * R * a * (Aa / R);
            }
        }
    }
    if (atot > 0) FluxLinkage *= ((double)labellist[lbl].Turns / atot);
    return FluxLinkage;
}

CComplex ResultsDoc::GetParallelLinkage(int numcirc)
{
    CComplex FluxLinkage(0, 0), Aa;
    CComplex A[3], J[3], U[3];
    double a, atot = 0, R = 0, c, r[3];
    double lc = LengthConv();

    U[0] = 1; U[1] = 1; U[2] = 1;

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (labellist[meshelem[i].lbl].InCircuit == numcirc) {
            c = blockproplist[meshelem[i].blk].Cduct;
            GetJA(i, J, A);
            a = ElmArea(i) * lc * lc;

            if (ProblemType == 1) {
                for (int k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;
                R = (r[0] + r[1] + r[2]) / 3.0;
                Aa = (A[0] + A[1] + A[2]) / 3.0;
            }

            if (ProblemType == 0) {
                FluxLinkage += PlnInt(a, A, U) * Depth * c;
                atot += a * c;
            } else {
                if (R > 0) {
                    FluxLinkage += 2.0 * PI * R * c * (Aa / R);
                    atot += a * c / R;
                }
            }
        }
    }
    if (atot > 0) FluxLinkage /= atot;
    return FluxLinkage;
}

CComplex ResultsDoc::GetParallelLinkageAlt(int numcirc)
{
    CComplex FluxLinkage(0, 0);
    CComplex A[3], J[3], U[3];
    double a, atot = 0, r[3];
    double lc = LengthConv();

    U[0] = 1; U[1] = 1; U[2] = 1;

    for (int i = 0; i < (int)meshelem.size(); i++) {
        if (labellist[meshelem[i].lbl].InCircuit == numcirc) {
            GetJA(i, J, A);
            a = ElmArea(i) * lc * lc;
            atot += a;

            if (ProblemType == 1)
                for (int k = 0; k < 3; k++)
                    r[k] = meshnode[meshelem[i].p[k]].x * lc;

            if (ProblemType == 0) FluxLinkage += PlnInt(a, A, U) * Depth;
            else FluxLinkage += AxiInt(a, A, U, r);
        }
    }
    if (atot > 0) FluxLinkage /= atot;
    return FluxLinkage;
}

CComplex ResultsDoc::GetVoltageDrop(int circNum)
{
    CComplex Volts(0, 0);

    if (circproplist[circNum].CircType == 1) {
        // Series circuit
        for (int i = 0; i < (int)labellist.size(); i++) {
            if (labellist[i].InCircuit == circNum) {
                if (labellist[i].Case == 0) {
                    // Solid conductor
                    if (ProblemType == 1)
                        Volts -= 2.0 * PI * labellist[i].dVolts * (double)labellist[i].Turns;
                    else
                        Volts -= Depth * labellist[i].dVolts * (double)labellist[i].Turns;
                } else {
                    // Stranded conductor
                    Volts += GetStrandedVoltageDrop(i);
                }
            }
        }
    } else {
        // Parallel circuit — find a solid conductor block
        bool found = false;
        for (int i = 0; i < (int)labellist.size(); i++) {
            if (labellist[i].InCircuit == circNum && labellist[i].Case == 0) {
                if (ProblemType == 1)
                    Volts -= 2.0 * PI * labellist[i].dVolts;
                else
                    Volts -= Depth * labellist[i].dVolts;
                found = true;
                break;
            }
        }
        // If no solid conductor found, compute from flux linkage
        if (!found) {
            CComplex FluxLinkage(0, 0);
            CComplex A[3], J[3], U[3];
            double a, atot = 0, r[3];
            double lc = LengthConv();
            U[0] = 1; U[1] = 1; U[2] = 1;

            for (int i = 0; i < (int)meshelem.size(); i++) {
                if (labellist[meshelem[i].lbl].InCircuit == circNum) {
                    GetJA(i, J, A);
                    a = ElmArea(i) * lc * lc;
                    atot += a;
                    if (ProblemType == 1)
                        for (int k = 0; k < 3; k++)
                            r[k] = meshnode[meshelem[i].p[k]].x * lc;
                    if (ProblemType == 0) FluxLinkage += PlnInt(a, A, U) * Depth;
                    else FluxLinkage += AxiInt(a, A, U, r);
                }
            }
            if (atot > 0)
                Volts = (2.0 * PI * Frequency / atot) * FluxLinkage;
        }
    }
    return Volts;
}

CComplex ResultsDoc::GetFluxLinkage(int circNum)
{
    CComplex FluxLinkage(0, 0);
    double lc = LengthConv();

    // Normal case: current is nonzero — use integral of A.J / I*
    if (abs(circproplist[circNum].Amps) > 0) {
        CComplex A[3], J[3];
        double a, r[3];

        for (int i = 0; i < (int)meshelem.size(); i++) {
            if (labellist[meshelem[i].lbl].InCircuit == circNum) {
                GetJA(i, J, A);
                a = ElmArea(i) * lc * lc;
                if (ProblemType == 1)
                    for (int k = 0; k < 3; k++)
                        r[k] = meshnode[meshelem[i].p[k]].x * lc;

                // Account for local energy in multiturn regions
                for (int k = 0; k < 3; k++)
                    A[k] += J[k] * labellist[meshelem[i].lbl].LocalEnergy;

                CComplex Jc[3];
                for (int k = 0; k < 3; k++) Jc[k] = conj(J[k]);
                if (ProblemType == 0) FluxLinkage += PlnInt(a, A, Jc) * Depth;
                else FluxLinkage += AxiInt(a, A, Jc, r);
            }
        }
        FluxLinkage /= conj(circproplist[circNum].Amps);
    } else {
        // Zero current — back out flux linkage from other info
        if (Frequency != 0)
            FluxLinkage = GetVoltageDrop(circNum) / (2.0 * CComplex(0, 1) * PI * Frequency);
        else {
            // DC, zero current
            if (circproplist[circNum].CircType == 1) {
                // Series
                for (int i = 0; i < (int)labellist.size(); i++) {
                    if (labellist[i].InCircuit == circNum) {
                        if (labellist[i].Case == 1 || ProblemType == 0)
                            FluxLinkage += GetStrandedLinkage(i);
                        else
                            FluxLinkage += GetSolidAxisymmetricLinkage(i);
                    }
                }
            } else {
                // Parallel — check if any blocks have nonzero conductivity
                bool hasConductor = false;
                for (int i = 0; i < (int)labellist.size(); i++)
                    if (labellist[i].Case == 0 && labellist[i].InCircuit == circNum)
                        hasConductor = true;
                if (hasConductor) FluxLinkage = GetParallelLinkage(circNum);
                else FluxLinkage = GetParallelLinkageAlt(circNum);
            }
        }
    }
    return FluxLinkage;
}

// ================================================================
// GetLineValues — sample field values along contour for X-Y plots
// Ported from FemmviewDoc.cpp:2553-2821
// ================================================================

void ResultsDoc::GetLineValues(std::vector<XYPlotPoint> &pts, int plotType, int numPoints)
{
    pts.clear();
    if (contour.size() < 2 || numPoints < 2) return;

    // Compute cumulative arc length along contour
    std::vector<double> q(contour.size(), 0);
    for (size_t i = 1; i < contour.size(); i++)
        q[i] = q[i - 1] + abs(contour[i] - contour[i - 1]);
    double totalLen = q.back();
    double dz = totalLen / (numPoints - 1);

    pts.resize(numPoints);
    int ncols = (Frequency == 0) ? 1 : 3;  // DC: 1 value; AC: |val|, Re, Im
    // Some types always return 1 column
    if (plotType == 1 || plotType == 4) ncols = 1;

    int seg = 1;
    int elm = -1;

    for (int i = 0; i < numPoints; i++) {
        double z = dz * i;
        while (seg < (int)contour.size() - 1 && z > q[seg]) seg++;

        double u = (z - q[seg - 1]) / (q[seg] - q[seg - 1] + 1e-30);
        CComplex pt = contour[seg - 1] + (contour[seg] - contour[seg - 1]) * u;

        // Tangent and normal
        CComplex t = contour[seg] - contour[seg - 1];
        t = t / abs(t);
        CComplex n = CComplex(0, 1) * t;  // I * t
        pt = pt + n * 1e-6;  // slight offset to avoid boundary

        // Find element (use cached search)
        if (elm < 0 || !PointInTri(pt.re, pt.im, elm)) {
            // Try neighbors first
            bool found = false;
            if (elm >= 0) {
                for (int j = 0; j < 3 && !found; j++) {
                    int nd = meshelem[elm].p[j];
                    for (int m = 0; m < NumList[nd] && !found; m++) {
                        int trial = ConList[nd][m];
                        if (PointInTri(pt.re, pt.im, trial)) {
                            elm = trial;
                            found = true;
                        }
                    }
                }
            }
            if (!found) elm = InTriangle(pt.re, pt.im);
        }

        pts[i].x = z;
        pts[i].vals.resize(ncols, 0);

        if (elm >= 0) {
            PointVals v;
            GetPointValues(pt.re, pt.im, elm, v);

            if (Frequency == 0) {
                switch (plotType) {
                case 0: pts[i].vals[0] = v.A.re; break;
                case 1: pts[i].vals[0] = sqrt(v.B1.re*v.B1.re + v.B2.re*v.B2.re); break;
                case 2: pts[i].vals[0] = n.re*v.B1.re + n.im*v.B2.re; break;
                case 3: pts[i].vals[0] = t.re*v.B1.re + t.im*v.B2.re; break;
                case 4: pts[i].vals[0] = sqrt(v.H1.re*v.H1.re + v.H2.re*v.H2.re); break;
                case 5: pts[i].vals[0] = n.re*v.H1.re + n.im*v.H2.re; break;
                case 6: pts[i].vals[0] = t.re*v.H1.re + t.im*v.H2.re; break;
                }
            }
            else {
                double r1, i1;
                switch (plotType) {
                case 0: // |A|, Re[A], Im[A]
                    pts[i].vals[0] = abs(v.A);
                    pts[i].vals[1] = v.A.re;
                    pts[i].vals[2] = v.A.im;
                    break;
                case 1: // |B|
                    pts[i].vals[0] = sqrt(abs(v.B1)*abs(v.B1) + abs(v.B2)*abs(v.B2));
                    break;
                case 2: // B.n
                    r1 = n.re*v.B1.re + n.im*v.B2.re;
                    i1 = n.re*v.B1.im + n.im*v.B2.im;
                    pts[i].vals[0] = sqrt(r1*r1 + i1*i1);
                    pts[i].vals[1] = r1;
                    pts[i].vals[2] = i1;
                    break;
                case 3: // B.t
                    r1 = t.re*v.B1.re + t.im*v.B2.re;
                    i1 = t.re*v.B1.im + t.im*v.B2.im;
                    pts[i].vals[0] = sqrt(r1*r1 + i1*i1);
                    pts[i].vals[1] = r1;
                    pts[i].vals[2] = i1;
                    break;
                case 4: // |H|
                    pts[i].vals[0] = sqrt(abs(v.H1)*abs(v.H1) + abs(v.H2)*abs(v.H2));
                    break;
                case 5: // H.n
                    r1 = n.re*v.H1.re + n.im*v.H2.re;
                    i1 = n.re*v.H1.im + n.im*v.H2.im;
                    pts[i].vals[0] = sqrt(r1*r1 + i1*i1);
                    pts[i].vals[1] = r1;
                    pts[i].vals[2] = i1;
                    break;
                case 6: // H.t
                    r1 = t.re*v.H1.re + t.im*v.H2.re;
                    i1 = t.re*v.H1.im + t.im*v.H2.im;
                    pts[i].vals[0] = sqrt(r1*r1 + i1*i1);
                    pts[i].vals[1] = r1;
                    pts[i].vals[2] = i1;
                    break;
                case 7: // Je
                    pts[i].vals[0] = abs(v.Je);
                    pts[i].vals[1] = v.Je.re;
                    pts[i].vals[2] = v.Je.im;
                    break;
                case 8: // Js+Je
                    pts[i].vals[0] = abs(v.Je + v.Js);
                    pts[i].vals[1] = v.Je.re + v.Js.re;
                    pts[i].vals[2] = v.Je.im + v.Js.im;
                    break;
                }
            }
        }
    }
}
