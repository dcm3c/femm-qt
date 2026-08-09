// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// xy_plot.h — the X-Y Plot dialog, shared by all four postprocessors
//
// Every physics offers the same thing: pick a quantity, pick how many points
// to sample along the defined contour, plot it. The four handlers had grown
// four copies, and two of them (electrostatics, heat flow) had drifted into a
// bare QInputDialog that dropped the point count -- so they read
// "eo/plotPoints" / "ho/plotPoints" from QSettings that nothing could write.
//
// Only the quantity list, its default index, and the settings key differ per
// physics. The point-vector type differs too (each results doc defines its own
// XYPlotPoint), which is why the plot builder is a template; all four have .x
// and .vals.
#ifndef XY_PLOT_H
#define XY_PLOT_H

#include "plot_widget.h"
#include "py_line_edit.h"
#include "xy_export.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace xyplot {

// Sampling bounds, applied to whatever the user types.
constexpr int kMinPoints = 10;
constexpr int kMaxPoints = 100000;
constexpr int kDefaultPoints = 1500;

// The prompt. Returns false if the user cancelled; otherwise fills plotType
// (an index into `types`) and npts. The chosen point count is written back to
// settingsKey so it is the default next time -- which is what the reads in
// each handler always assumed.
inline bool prompt(QWidget *parent, const QStringList &types, int defaultType,
                   const QString &settingsKey, int &plotType, int &npts)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QCoreApplication::translate("xyplot", "X-Y Plot"));
    auto *form = new QFormLayout;

    auto *typeCombo = new QComboBox;
    for (const auto &t : types) typeCombo->addItem(t);
    if (defaultType >= 0 && defaultType < types.size())
        typeCombo->setCurrentIndex(defaultType);
    form->addRow(QCoreApplication::translate("xyplot", "Quantity:"), typeCombo);

    auto *nptsEdit = new PyLineEdit;
    nptsEdit->setText(QSettings().value(settingsKey, kDefaultPoints).toString());
    form->addRow(QCoreApplication::translate("xyplot", "Number of points:"), nptsEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return false;

    plotType = typeCombo->currentIndex();
    npts = (int)nptsEdit->evaluateAsDouble(kDefaultPoints);
    if (npts < kMinPoints) npts = kMinPoints;
    if (npts > kMaxPoints) npts = kMaxPoints;
    QSettings().setValue(settingsKey, npts);
    return true;
}

// The quantity list for a physics, as the combo wants it. The table itself
// lives in libfemm (xy_export.h) because *_makeplot needs the same strings for
// its column headings and cannot see a GUI list.
inline QStringList quantityList(const std::vector<std::string> &names)
{
    QStringList out;
    for (const auto &n : names) out << QString::fromStdString(n);
    return out;
}

// Build and show the plot window from a GetLineValues / GetGapValues result.
// A three-column point carries magnitude/real/imaginary of a phasor quantity;
// one column is a plain real value. The series labels become the column
// headings if the user then presses Export..., which is why they come from the
// same helper the file writer uses.
template<class Pts>
void showPlot(QWidget *parent, const QString &type, const Pts &pts,
              const QString &titleSuffix, const QString &xAxisLabel)
{
    const QString name = QString::fromStdString(xyexport::shortName(type.toStdString()));
    const int ncols = pts.empty() ? 0 : (int)pts[0].vals.size();
    const std::vector<std::string> colNames =
        xyexport::columnLabels(name.toStdString(), ncols);

    auto *pdlg = new PlotDialog(name + titleSuffix, parent);
    pdlg->setAttribute(Qt::WA_DeleteOnClose);
    for (int c = 0; c < ncols; c++) {
        PlotSeries series;
        series.label = (c < (int)colNames.size()) ? QString::fromStdString(colNames[c]) : name;
        for (const auto &pt : pts) {
            series.x.push_back(pt.x);
            series.y.push_back(c < (int)pt.vals.size() ? pt.vals[c] : 0);
        }
        pdlg->plot()->addSeries(series);
    }
    pdlg->plot()->setAxisLabels(xAxisLabel, name);
    pdlg->show();
}

// A plot along the defined contour: x is arc length from its start.
template<class Pts>
void showAlongContour(QWidget *parent, const QString &type, const Pts &pts)
{
    showPlot(parent, type, pts,
             QCoreApplication::translate("xyplot", " along contour"),
             QString::fromLatin1(xyexport::contourAxisLabel()));
}

} // namespace xyplot

#endif // XY_PLOT_H
