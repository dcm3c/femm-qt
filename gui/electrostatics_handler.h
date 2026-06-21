// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// electrostatics_handler.h — Electrostatics-specific postprocessor dialogs
#ifndef ELECTROSTATICS_HANDLER_H
#define ELECTROSTATICS_HANDLER_H

#include "postprocessor_handler.h"

class ScalarResultsWidget;

class ElectrostaticsHandler : public PostprocessorHandler {
public:
    explicit ElectrostaticsHandler(ScalarResultsWidget *widget) : m_widget(widget) {}

    void showPrefsDialog(QWidget *parent) override;
    void showIntegrateDialog(QWidget *parent, OutputDialog *out) override;
    void showXYPlotDialog(QWidget *parent) override;
    void showCircuitDialog(QWidget *parent) override;
    void showContourPlotDialog(QWidget *parent) override;
    void showDensityPlotDialog(QWidget *parent) override;
    void showVectorPlotDialog(QWidget *parent) override;
    void showProblemInfo(QWidget *parent) override;

private:
    ScalarResultsWidget *m_widget;
};

#endif // ELECTROSTATICS_HANDLER_H
