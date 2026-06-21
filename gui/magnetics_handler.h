// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// magnetics_handler.h — Magnetics-specific postprocessor dialogs
#ifndef MAGNETICS_HANDLER_H
#define MAGNETICS_HANDLER_H

#include "postprocessor_handler.h"

class ResultsWidget;

class MagneticsHandler : public PostprocessorHandler {
public:
    explicit MagneticsHandler(ResultsWidget *widget) : m_widget(widget) {}

    void showPrefsDialog(QWidget *parent) override;
    void showIntegrateDialog(QWidget *parent, OutputDialog *out) override;
    void showXYPlotDialog(QWidget *parent) override;
    void showCircuitDialog(QWidget *parent) override;
    void showContourPlotDialog(QWidget *parent) override;
    void showDensityPlotDialog(QWidget *parent) override;
    void showVectorPlotDialog(QWidget *parent) override;

    // Magnetics-only
    void showProblemInfo(QWidget *parent) override;
    void showBHCurves(QWidget *parent);

private:
    ResultsWidget *m_widget;
};

#endif // MAGNETICS_HANDLER_H
