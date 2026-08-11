// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// bh_dialog.h — B-H curve data editor for nonlinear magnetic materials
#ifndef BH_DIALOG_H
#define BH_DIALOG_H

#include "curve_dialog.h"

struct FMaterialProp;

class BHDataDialog : public CurveDataDialog {
    Q_OBJECT
public:
    BHDataDialog(QWidget *parent = nullptr);

protected:
    // B must increase monotonically, so a point that does not advance B is
    // dropped rather than reordered -- a B-H curve that doubles back is not a
    // curve the solver can invert.
    void conditionData(std::vector<double> &B, std::vector<double> &H) const override;

    // The cubic spline needs three points to be a spline at all.
    int minimumPlotPoints() const override { return 3; }

    PlotData buildPlotData(const std::vector<double> &B,
                           const std::vector<double> &H) const override;
};

#endif // BH_DIALOG_H
