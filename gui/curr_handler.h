// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_handler.h — Current flow postprocessor dialogs
#ifndef CURR_HANDLER_H
#define CURR_HANDLER_H

#include "postprocessor_handler.h"

class ScalarResultsWidget;
class CurrResultsDoc;

class CurrentFlowHandler : public PostprocessorHandler {
public:
    CurrentFlowHandler(ScalarResultsWidget *widget, CurrResultsDoc *doc);

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
    CurrResultsDoc *m_doc;
};

#endif // CURR_HANDLER_H
