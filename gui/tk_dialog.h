// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// tk_dialog.h — T-k curve data editor for temperature-dependent thermal
// conductivity. Ported from femm42src/femm/KCurve.cpp (CTKData) and
// TKDatafile.cpp (CTKDatafile).
#ifndef TK_DIALOG_H
#define TK_DIALOG_H

#include "curve_dialog.h"

class TKDataDialog : public CurveDataDialog {
    Q_OBJECT
public:
    TKDataDialog(QWidget *parent = nullptr);

protected:
    // Unlike a B-H curve, a T-k curve carries no requirement that the points
    // arrive in order -- data sheets often run hot to cold. Sort rather than
    // discard, as Original FEMM's SortCurve does.
    void conditionData(std::vector<double> &T, std::vector<double> &K) const override;

    // Interpolation is piecewise linear, so two points is a curve.
    int minimumPlotPoints() const override { return 2; }

    PlotData buildPlotData(const std::vector<double> &T,
                           const std::vector<double> &K) const override;

    bool importOptions() override;
    void convertImported(double &T, double &K) const override;

private:
    // Set by importOptions(), consumed by convertImported()
    int m_order = 0;    // 0 = temperature first, 1 = conductivity first
    int m_tUnits = 0;   // 0 K, 1 degC, 2 degF, 3 degR
    int m_kUnits = 0;   // 0 W/(m*K), 1 Btu/(ft*hr*degF), 2 kcal/(m*hr*K)
};

#endif // TK_DIALOG_H
