// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// postprocessor_handler.h — Abstract base for physics-specific postprocessor dialogs
// Each physics type (magnetics, electrostatics, heat, current) provides a concrete
// handler so that new physics can be added without modifying the widget classes.
#ifndef POSTPROCESSOR_HANDLER_H
#define POSTPROCESSOR_HANDLER_H

#include <QString>
#include <QMessageBox>

class QWidget;
class OutputDialog;

class PostprocessorHandler {
public:
    virtual ~PostprocessorHandler() = default;

    virtual void showPrefsDialog(QWidget *parent) = 0;
    virtual void showIntegrateDialog(QWidget *parent, OutputDialog *out) = 0;
    virtual void showXYPlotDialog(QWidget *parent) = 0;
    virtual void showCircuitDialog(QWidget *parent) = 0;
    virtual void showContourPlotDialog(QWidget *parent) = 0;
    virtual void showDensityPlotDialog(QWidget *parent) = 0;
    virtual void showVectorPlotDialog(QWidget *parent) = 0;
    virtual void showProblemInfo(QWidget *parent) = 0;

protected:
    // Shared problem info formatting for all physics types.
    // Pass frequency < 0 to omit the frequency line (EO, HO).
    static void showProblemInfoDialog(QWidget *parent, int lengthUnits,
                                      int problemType, double depth,
                                      double frequency, int numNodes, int numElements)
    {
        static const char *names[] = { "Inches", "Millimeters", "Centimeters",
                                       "Meters", "Mils", "Micrometers" };
        static const char *abbrevs[] = { " in", " mm", " cm", " m", " mil", " um" };
        static const double conv[] = { 0.0254, 0.001, 0.01, 1.0, 2.54e-05, 1.e-06 };

        int u = (lengthUnits >= 0 && lengthUnits <= 5) ? lengthUnits : 3;
        QString info;
        info += QString("Length Units: %1\n").arg(names[u]);
        if (problemType == 1)
            info += "Axisymmetric Solution\n";
        else
            info += QString("2-D Planar (Depth: %1%2)\n").arg(depth / conv[u]).arg(abbrevs[u]);
        if (frequency >= 0)
            info += QString("Frequency: %1 Hz\n").arg(frequency);
        info += QString("\n%1 Nodes\n%2 Elements").arg(numNodes).arg(numElements);
        QMessageBox::information(parent, "Problem Info", info);
    }
};

#endif // POSTPROCESSOR_HANDLER_H
