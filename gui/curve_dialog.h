// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curve_dialog.h — shared two-column curve data editor
//
// The B-H curve editor for magnetic materials and the T-k curve editor for
// heat flow materials are the same dialog with different labels: two columns
// of newline-separated numbers, an import button, and a pair of plot buttons.
// Only three things actually differ, so those are the virtuals: how entered
// points are conditioned (B-H drops non-monotonic points, T-k sorts by
// temperature), how imported values are converted, and what curve gets drawn
// through the points (a cubic spline of H(B) plotted sideways, versus a
// piecewise-linear k(T) plotted the usual way round).
#ifndef CURVE_DIALOG_H
#define CURVE_DIALOG_H

#include <QDialog>
#include <QString>
#include <vector>

class QTextEdit;

class CurveDataDialog : public QDialog {
    Q_OBJECT
public:
    // Everything the two editors label differently, kept in one struct so a
    // subclass declares its identity in a single place and inherits the layout.
    struct Spec {
        QString windowTitle;      // "B-H Curve Data"
        QString namedTitle;       // "B-H Curve: %1", used by setMaterialName
        QString prompt;           // the sentence above the columns
        QString xHeader, yHeader; // column headers
        QString xPlaceholder, yPlaceholder;
        QString plotButton, logPlotButton;
        QString plotTitle;        // used when no material name has been set
        QString xAxisLabel, yAxisLabel;   // as drawn on the plot
        QString importTitle;      // file dialog caption
    };

    // The curve to draw and the entered points to mark, already in plot
    // coordinates -- which column goes on which axis is the subclass's call.
    struct PlotData {
        std::vector<double> cx, cy;   // interpolated curve
        std::vector<double> mx, my;   // markers at the entered points
    };

    explicit CurveDataDialog(const Spec &spec, QWidget *parent = nullptr);

    void setMaterialName(const QString &name);
    void setData(const std::vector<double> &x, const std::vector<double> &y, int npts);
    void getData(std::vector<double> &x, std::vector<double> &y, int &npts) const;

protected:
    // Applied to the parsed columns before they are handed back. The base
    // class neither drops nor reorders anything.
    virtual void conditionData(std::vector<double> &x, std::vector<double> &y) const;

    // Applied to each pair read from a file. The base class converts nothing.
    virtual void convertImported(double &x, double &y) const;

    // Called before the import file is read, so a subclass can ask about the
    // file's column order and units. Returning false cancels the import.
    virtual bool importOptions();

    // How many points the plot needs; a spline needs more than a piecewise
    // linear interpolation does.
    virtual int minimumPlotPoints() const { return 2; }

    virtual PlotData buildPlotData(const std::vector<double> &x,
                                   const std::vector<double> &y) const = 0;

    const Spec &spec() const { return m_spec; }
    QString materialName() const { return m_matName; }

private slots:
    void onImport();

private:
    void onPlot(bool logScale);

    Spec m_spec;
    QTextEdit *m_xEdit;
    QTextEdit *m_yEdit;
    QString m_matName;
};

#endif // CURVE_DIALOG_H
