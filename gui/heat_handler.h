// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_handler.h — Heat flow postprocessor dialogs
#ifndef HEAT_HANDLER_H
#define HEAT_HANDLER_H

#include "postprocessor_handler.h"

class ScalarResultsWidget;
class HeatResultsDoc;

class HeatFlowHandler : public PostprocessorHandler {
public:
    HeatFlowHandler(ScalarResultsWidget *widget, HeatResultsDoc *doc);

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
    HeatResultsDoc *m_doc;
};

#endif // HEAT_HANDLER_H
