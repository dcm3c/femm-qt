// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// bh_dialog.cpp — B-H curve data editor for nonlinear magnetic materials
// Ported from femm42src/femm/BHData.cpp
// Users enter B and H values as newline-separated lists. The dialog frame is
// CurveDataDialog, shared with the heat flow T-k editor.

#include "bh_dialog.h"

#include <cmath>
#include <vector>

static CurveDataDialog::Spec bhSpec()
{
    CurveDataDialog::Spec s;
    s.windowTitle    = QObject::tr("B-H Curve Data");
    s.namedTitle     = QObject::tr("B-H Curve: %1");
    s.prompt         = QObject::tr("Enter B and H values (one per line, monotonically increasing B):");
    s.xHeader        = QObject::tr("B (Tesla)");
    s.yHeader        = QObject::tr("H (A/m)");
    s.xPlaceholder   = QObject::tr("B values (Tesla)");
    s.yPlaceholder   = QObject::tr("H values (A/m)");
    s.plotButton     = QObject::tr("Plot B-H Curve");
    s.logPlotButton  = QObject::tr("Log Plot B-H Curve");
    s.plotTitle      = QObject::tr("B-H Curve");
    s.xAxisLabel     = QObject::tr("H, Amp/Meter");
    s.yAxisLabel     = QObject::tr("B, Tesla");
    s.importTitle    = QObject::tr("Import B-H Data");
    return s;
}

BHDataDialog::BHDataDialog(QWidget *parent) : CurveDataDialog(bhSpec(), parent)
{
}

void BHDataDialog::conditionData(std::vector<double> &B, std::vector<double> &H) const
{
    std::vector<double> Bk, Hk;
    double lastB = -1e30;
    for (size_t i = 0; i < B.size(); i++) {
        if (B[i] <= lastB) continue;   // enforce monotonicity
        Bk.push_back(B[i]);
        Hk.push_back(H[i]);
        lastB = B[i];
    }
    B.swap(Bk);
    H.swap(Hk);
}

// ================================================================
// B-H curve plotting with cubic spline interpolation
// Ported from femm42src/femm/BHData.cpp: GetSlopes() + GetH()
// ================================================================

// Solve tridiagonal system in-place (Thomas algorithm)
static void triSolve(std::vector<double> &a, std::vector<double> &b,
                     std::vector<double> &c, std::vector<double> &d, int n)
{
    for (int i = 1; i < n; i++) {
        double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    d[n - 1] /= b[n - 1];
    for (int i = n - 2; i >= 0; i--)
        d[i] = (d[i] - c[i] * d[i + 1]) / b[i];
}

// Compute cubic Hermite spline slopes (matching FEMM GetSlopes)
// with monotonicity-enforcing smoothing loop
static void getSlopes(std::vector<double> &B, std::vector<double> &H,
                      int n, std::vector<double> &slope)
{
    slope.resize(n, 0);
    if (n < 2) return;

    std::vector<double> bn(n), hn(n);
    bool curveOK = false;

    while (!curveOK) {
        std::vector<double> sub(n, 0), diag(n, 0), sup(n, 0), rhs(n, 0);

        double l1 = B[1] - B[0];
        diag[0] = 4.0 / l1;
        sup[0] = 2.0 / l1;
        rhs[0] = 6.0 * (H[1] - H[0]) / (l1 * l1);

        l1 = B[n - 1] - B[n - 2];
        diag[n - 1] = 4.0 / l1;
        sub[n - 1] = 2.0 / l1;
        rhs[n - 1] = 6.0 * (H[n - 1] - H[n - 2]) / (l1 * l1);

        for (int i = 1; i < n - 1; i++) {
            l1 = B[i] - B[i - 1];
            double l2 = B[i + 1] - B[i];
            sub[i] = 2.0 / l1;
            diag[i] = 4.0 * (l1 + l2) / (l1 * l2);
            sup[i] = 2.0 / l2;
            rhs[i] = 6.0 * (H[i] - H[i - 1]) / (l1 * l1) +
                     6.0 * (H[i + 1] - H[i]) / (l2 * l2);
        }

        triSolve(sub, diag, sup, rhs, n);
        for (int i = 0; i < n; i++) slope[i] = rhs[i];

        curveOK = true;
        for (int i = 1; i < n; i++) {
            double d0 = slope[i - 1], d1 = slope[i];
            double u0 = H[i - 1], u1 = H[i];
            double L = B[i] - B[i - 1];
            double c0 = d0;
            double c1 = -(2.0 * (2.0 * d0 * L + d1 * L + 3.0 * u0 - 3.0 * u1)) / (L * L);
            double c2 = (3.0 * (d0 * L + d1 * L + 2.0 * u0 - 2.0 * u1)) / (L * L * L);
            double disc = c1 * c1 - 4.0 * c0 * c2;
            double X0 = -1, X1 = -1;
            if (c2 == 0) { if (c1 != 0) X0 = -c0 / c1; }
            else if (disc > 0) {
                double sd = sqrt(disc);
                X0 = -(c1 + sd) / (2.0 * c2);
                X1 = (-c1 + sd) / (2.0 * c2);
            }
            if ((X0 >= 0 && X0 <= L) || (X1 >= 0 && X1 <= L))
                curveOK = false;
        }

        if (!curveOK) {
            for (int i = 1; i < n - 1; i++) {
                bn[i] = (B[i - 1] + B[i] + B[i + 1]) / 3.0;
                hn[i] = (H[i - 1] + H[i] + H[i + 1]) / 3.0;
            }
            for (int i = 1; i < n - 1; i++) { B[i] = bn[i]; H[i] = hn[i]; }
        }
    }
}

// Evaluate spline at point b (matching FEMM GetH)
static double getH(double b, const std::vector<double> &B,
                   const std::vector<double> &H,
                   const std::vector<double> &slope, int n)
{
    b = fabs(b);
    if (n == 0 || b == 0) return 0;

    if (b > B[n - 1])
        return H[n - 1] + slope[n - 1] * (b - B[n - 1]);

    for (int i = 0; i < n - 1; i++) {
        if (b >= B[i] && b <= B[i + 1]) {
            double l = B[i + 1] - B[i];
            double z = (b - B[i]) / l;
            double z2 = z * z;
            return (1.0 - 3.0 * z2 + 2.0 * z2 * z) * H[i] +
                   z * (1.0 - 2.0 * z + z2) * l * slope[i] +
                   z2 * (3.0 - 2.0 * z) * H[i + 1] +
                   z2 * (z - 1.0) * l * slope[i + 1];
        }
    }
    return 0;
}

CurveDataDialog::PlotData BHDataDialog::buildPlotData(const std::vector<double> &B,
                                                      const std::vector<double> &H) const
{
    const int npts = (int)B.size();

    // Work on copies: getSlopes smooths the data in place when the entered
    // points would give a spline that is not single-valued.
    std::vector<double> Bw = B, Hw = H;
    std::vector<double> slope;
    getSlopes(Bw, Hw, npts, slope);

    PlotData pd;

    // 101 points across the curve, as Original FEMM draws it. H goes on the
    // horizontal axis, so the interpolated H is the plot's x.
    double db = (Bw[npts - 1] - Bw[0]) / 100.0;
    for (int i = 0; i <= 100; i++) {
        double b = Bw[0] + i * db;
        pd.cx.push_back(getH(b, Bw, Hw, slope, npts));
        pd.cy.push_back(b);
    }

    // Markers sit on the points as entered, not as smoothed
    for (int i = 0; i < npts; i++) {
        pd.mx.push_back(H[i]);
        pd.my.push_back(B[i]);
    }

    return pd;
}
