// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curve_dialog.cpp — shared two-column curve data editor
// The layout here is the one the B-H editor has always had; it is reproduced
// exactly so that the manual's B-H figure stays accurate.

#include "curve_dialog.h"
#include "plot_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <algorithm>

CurveDataDialog::CurveDataDialog(const Spec &spec, QWidget *parent)
    : QDialog(parent), m_spec(spec)
{
    setWindowTitle(m_spec.windowTitle);
    resize(500, 400);

    auto *topLabel = new QLabel(m_spec.prompt);

    m_xEdit = new QTextEdit;
    m_xEdit->setPlaceholderText(m_spec.xPlaceholder);

    m_yEdit = new QTextEdit;
    m_yEdit->setPlaceholderText(m_spec.yPlaceholder);

    auto *dataLayout = new QHBoxLayout;
    auto *xCol = new QVBoxLayout;
    xCol->addWidget(new QLabel(m_spec.xHeader));
    xCol->addWidget(m_xEdit);
    auto *yCol = new QVBoxLayout;
    yCol->addWidget(new QLabel(m_spec.yHeader));
    yCol->addWidget(m_yEdit);
    dataLayout->addLayout(xCol);
    dataLayout->addLayout(yCol);

    auto *importBtn = new QPushButton(tr("Import from file..."));
    connect(importBtn, &QPushButton::clicked, this, &CurveDataDialog::onImport);

    auto *plotBtn = new QPushButton(m_spec.plotButton);
    connect(plotBtn, &QPushButton::clicked, this, [this]() { onPlot(false); });
    auto *logPlotBtn = new QPushButton(m_spec.logPlotButton);
    connect(logPlotBtn, &QPushButton::clicked, this, [this]() { onPlot(true); });

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(importBtn);
    btnRow->addStretch();
    btnRow->addWidget(plotBtn);
    btnRow->addWidget(logPlotBtn);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(topLabel);
    layout->addLayout(dataLayout);
    layout->addLayout(btnRow);
    layout->addWidget(buttons);
}

void CurveDataDialog::setMaterialName(const QString &name)
{
    m_matName = name;
    setWindowTitle(m_spec.namedTitle.arg(name));
}

void CurveDataDialog::setData(const std::vector<double> &x,
                              const std::vector<double> &y, int npts)
{
    QString xText, yText;
    for (int i = 0; i < npts; i++) {
        if (i > 0) { xText += "\n"; yText += "\n"; }
        xText += QString::number(x[i], 'g', 10);
        yText += QString::number(y[i], 'g', 10);
    }
    m_xEdit->setPlainText(xText);
    m_yEdit->setPlainText(yText);
}

void CurveDataDialog::getData(std::vector<double> &x,
                              std::vector<double> &y, int &npts) const
{
    x.clear();
    y.clear();

    QStringList xLines = m_xEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    QStringList yLines = m_yEdit->toPlainText().split('\n', Qt::SkipEmptyParts);

    int n = std::min(xLines.size(), yLines.size());
    for (int i = 0; i < n; i++) {
        bool okX, okY;
        double xv = xLines[i].trimmed().toDouble(&okX);
        double yv = yLines[i].trimmed().toDouble(&okY);
        if (!okX || !okY) continue;
        x.push_back(xv);
        y.push_back(yv);
    }

    conditionData(x, y);
    npts = (int)x.size();
}

void CurveDataDialog::conditionData(std::vector<double> &, std::vector<double> &) const
{
}

void CurveDataDialog::convertImported(double &, double &) const
{
}

bool CurveDataDialog::importOptions()
{
    return true;
}

void CurveDataDialog::onImport()
{
    QString path = QFileDialog::getOpenFileName(this,
        m_spec.importTitle, QString(),
        tr("Text Files (*.txt *.csv *.dat);;All Files (*)"));
    if (path.isEmpty()) return;

    // The file is chosen before the units are asked about, matching Original
    // FEMM: the question is about the file, so the user has usually just been
    // reminded what is in it.
    if (!importOptions()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file"));
        return;
    }

    QString xText, yText;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;

        // Two numbers per line, separated by whitespace, a comma or a tab
        line.replace(',', ' ').replace('\t', ' ');
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool okX, okY;
            double xv = parts[0].toDouble(&okX);
            double yv = parts[1].toDouble(&okY);
            if (okX && okY) {
                convertImported(xv, yv);
                if (!xText.isEmpty()) { xText += "\n"; yText += "\n"; }
                xText += QString::number(xv, 'g', 10);
                yText += QString::number(yv, 'g', 10);
            }
        }
    }

    m_xEdit->setPlainText(xText);
    m_yEdit->setPlainText(yText);
}

// A B-H curve's first point is almost always H = 0, whose log is minus
// infinity: on a log axis that point drags the range down to the plot
// widget's clamp and squeezes the real data into the right-hand edge.
// Original FEMM handles it in CBHPlot::MakePlot by dropping that first
// marker and the leading run of interpolated points that falls below the
// next one, which is done here for the same reason -- and in the same
// place, on the plot's own abscissa, so it covers the T-k curve too.
//
// The one departure from OF: OF tests the first point for exactly zero,
// while this drops every leading point that is not positive. Same result
// on the case that actually occurs, but it is not fooled by a second
// zero or by a negative value.
static void trimForLogX(CurveDataDialog::PlotData &pd)
{
    size_t m = 0;
    while (m < pd.mx.size() && pd.mx[m] <= 0) m++;
    if (m == 0) return;
    pd.mx.erase(pd.mx.begin(), pd.mx.begin() + m);
    pd.my.erase(pd.my.begin(), pd.my.begin() + m);

    // Trim the curve to start no earlier than the first surviving marker
    const double x0 = pd.mx.empty() ? 0.0 : pd.mx.front();
    size_t c = 0;
    while (c < pd.cx.size() && (pd.cx[c] <= 0 || pd.cx[c] < x0)) c++;
    pd.cx.erase(pd.cx.begin(), pd.cx.begin() + c);
    pd.cy.erase(pd.cy.begin(), pd.cy.begin() + c);
}

void CurveDataDialog::onPlot(bool logScale)
{
    std::vector<double> x, y;
    int npts;
    getData(x, y, npts);

    const int need = minimumPlotPoints();
    if (npts < need) {
        QMessageBox::information(this, m_spec.plotButton,
            tr("Must have at least %n pair(s) of data points.", "", need));
        return;
    }

    PlotData pd = buildPlotData(x, y);

    if (logScale) {
        trimForLogX(pd);
        if (pd.cx.size() < 2) {
            QMessageBox::information(this, m_spec.logPlotButton,
                tr("There are not enough positive values to plot on a "
                   "logarithmic axis."));
            return;
        }
    }

    PlotSeries series;
    series.label = m_spec.plotTitle;
    series.x = pd.cx;
    series.y = pd.cy;

    PlotMarkers markers;
    markers.x = pd.mx;
    markers.y = pd.my;

    QString title = m_matName.isEmpty() ? m_spec.plotTitle
                                        : m_spec.namedTitle.arg(m_matName);
    auto *dlg = new PlotDialog(title, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->plot()->addSeries(series);
    dlg->plot()->setMarkers(markers);
    dlg->plot()->setAxisLabels(m_spec.xAxisLabel, m_spec.yAxisLabel);
    dlg->plot()->setLogX(logScale);
    dlg->show();
}
