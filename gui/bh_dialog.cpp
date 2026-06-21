// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// bh_dialog.cpp — B-H curve data editor for nonlinear magnetic materials
// Ported from femm42src/femm/BHData.cpp
// Users enter B and H values as newline-separated lists.

#include "bh_dialog.h"
#include "plot_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <cstdio>
#include <cmath>
#include <sstream>

BHDataDialog::BHDataDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("B-H Curve Data"));
    resize(500, 400);

    auto *topLabel = new QLabel(tr("Enter B and H values (one per line, monotonically increasing B):"));

    m_bEdit = new QTextEdit;
    m_bEdit->setPlaceholderText(tr("B values (Tesla)"));

    m_hEdit = new QTextEdit;
    m_hEdit->setPlaceholderText(tr("H values (A/m)"));

    auto *dataLayout = new QHBoxLayout;
    auto *bCol = new QVBoxLayout;
    bCol->addWidget(new QLabel(tr("B (Tesla)")));
    bCol->addWidget(m_bEdit);
    auto *hCol = new QVBoxLayout;
    hCol->addWidget(new QLabel(tr("H (A/m)")));
    hCol->addWidget(m_hEdit);
    dataLayout->addLayout(bCol);
    dataLayout->addLayout(hCol);

    auto *importBtn = new QPushButton(tr("Import from file..."));
    connect(importBtn, &QPushButton::clicked, this, &BHDataDialog::onImport);

    auto *plotBtn = new QPushButton(tr("Plot B-H Curve"));
    connect(plotBtn, &QPushButton::clicked, this, [this]() { onPlotBH(false); });
    auto *logPlotBtn = new QPushButton(tr("Log Plot B-H Curve"));
    connect(logPlotBtn, &QPushButton::clicked, this, [this]() { onPlotBH(true); });

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

void BHDataDialog::setMaterialName(const QString &name)
{
    m_matName = name;
    setWindowTitle(tr("B-H Curve: %1").arg(name));
}

void BHDataDialog::setData(const std::vector<double> &B, const std::vector<double> &H, int npts)
{
    QString bText, hText;
    for (int i = 0; i < npts; i++) {
        if (i > 0) { bText += "\n"; hText += "\n"; }
        bText += QString::number(B[i], 'g', 10);
        hText += QString::number(H[i], 'g', 10);
    }
    m_bEdit->setPlainText(bText);
    m_hEdit->setPlainText(hText);
}

void BHDataDialog::getData(std::vector<double> &B, std::vector<double> &H, int &npts) const
{
    B.clear();
    H.clear();

    // Parse B values
    QStringList bLines = m_bEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    QStringList hLines = m_hEdit->toPlainText().split('\n', Qt::SkipEmptyParts);

    int n = std::min(bLines.size(), hLines.size());
    double lastB = -1e30;

    for (int i = 0; i < n; i++) {
        bool okB, okH;
        double bv = bLines[i].trimmed().toDouble(&okB);
        double hv = hLines[i].trimmed().toDouble(&okH);
        if (!okB || !okH) continue;
        if (bv <= lastB) continue;  // enforce monotonicity
        B.push_back(bv);
        H.push_back(hv);
        lastB = bv;
    }
    npts = (int)B.size();
}

void BHDataDialog::onImport()
{
    QString path = QFileDialog::getOpenFileName(this,
        tr("Import B-H Data"), QString(),
        tr("Text Files (*.txt *.csv *.dat);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open file"));
        return;
    }

    QString bText, hText;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('%')) continue;

        // Try to parse two numbers per line (B H or B,H)
        line.replace(',', ' ').replace('\t', ' ');
        QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            bool okB, okH;
            double bv = parts[0].toDouble(&okB);
            double hv = parts[1].toDouble(&okH);
            if (okB && okH) {
                if (!bText.isEmpty()) { bText += "\n"; hText += "\n"; }
                bText += QString::number(bv, 'g', 10);
                hText += QString::number(hv, 'g', 10);
            }
        }
    }

    m_bEdit->setPlainText(bText);
    m_hEdit->setPlainText(hText);
}

// ================================================================
// B-H curve plotting with cubic spline interpolation
// Ported from femm42src/femm/BHData.cpp: GetSlopes() + GetH()
// ================================================================

// Solve tridiagonal system in-place (Thomas algorithm)
static void triSolve(std::vector<double> &a, std::vector<double> &b,
                     std::vector<double> &c, std::vector<double> &d, int n)
{
    for (int i = 1; i < n; i++) {
        double m = a[i] / b[i - 1];
        b[i] -= m * c[i - 1];
        d[i] -= m * d[i - 1];
    }
    d[n - 1] /= b[n - 1];
    for (int i = n - 2; i >= 0; i--)
        d[i] = (d[i] - c[i] * d[i + 1]) / b[i];
}

// Compute cubic Hermite spline slopes (matching FEMM GetSlopes)
// with monotonicity-enforcing smoothing loop
static void getSlopes(std::vector<double> &B, std::vector<double> &H,
                      int n, std::vector<double> &slope)
{
    slope.resize(n, 0);
    if (n < 2) return;

    std::vector<double> bn(n), hn(n);
    bool curveOK = false;

    while (!curveOK) {
        std::vector<double> sub(n, 0), diag(n, 0), sup(n, 0), rhs(n, 0);

        double l1 = B[1] - B[0];
        diag[0] = 4.0 / l1;
        sup[0] = 2.0 / l1;
        rhs[0] = 6.0 * (H[1] - H[0]) / (l1 * l1);

        l1 = B[n - 1] - B[n - 2];
        diag[n - 1] = 4.0 / l1;
        sub[n - 1] = 2.0 / l1;
        rhs[n - 1] = 6.0 * (H[n - 1] - H[n - 2]) / (l1 * l1);

        for (int i = 1; i < n - 1; i++) {
            l1 = B[i] - B[i - 1];
            double l2 = B[i + 1] - B[i];
            sub[i] = 2.0 / l1;
            diag[i] = 4.0 * (l1 + l2) / (l1 * l2);
            sup[i] = 2.0 / l2;
            rhs[i] = 6.0 * (H[i] - H[i - 1]) / (l1 * l1) +
                     6.0 * (H[i + 1] - H[i]) / (l2 * l2);
        }

        triSolve(sub, diag, sup, rhs, n);
        for (int i = 0; i < n; i++) slope[i] = rhs[i];

        curveOK = true;
        for (int i = 1; i < n; i++) {
            double d0 = slope[i - 1], d1 = slope[i];
            double u0 = H[i - 1], u1 = H[i];
            double L = B[i] - B[i - 1];
            double c0 = d0;
            double c1 = -(2.0 * (2.0 * d0 * L + d1 * L + 3.0 * u0 - 3.0 * u1)) / (L * L);
            double c2 = (3.0 * (d0 * L + d1 * L + 2.0 * u0 - 2.0 * u1)) / (L * L * L);
            double disc = c1 * c1 - 4.0 * c0 * c2;
            double X0 = -1, X1 = -1;
            if (c2 == 0) { if (c1 != 0) X0 = -c0 / c1; }
            else if (disc > 0) {
                double sd = sqrt(disc);
                X0 = -(c1 + sd) / (2.0 * c2);
                X1 = (-c1 + sd) / (2.0 * c2);
            }
            if ((X0 >= 0 && X0 <= L) || (X1 >= 0 && X1 <= L))
                curveOK = false;
        }

        if (!curveOK) {
            for (int i = 1; i < n - 1; i++) {
                bn[i] = (B[i - 1] + B[i] + B[i + 1]) / 3.0;
                hn[i] = (H[i - 1] + H[i] + H[i + 1]) / 3.0;
            }
            for (int i = 1; i < n - 1; i++) { B[i] = bn[i]; H[i] = hn[i]; }
        }
    }
}

// Evaluate spline at point b (matching FEMM GetH)
static double getH(double b, const std::vector<double> &B,
                   const std::vector<double> &H,
                   const std::vector<double> &slope, int n)
{
    b = fabs(b);
    if (n == 0 || b == 0) return 0;

    if (b > B[n - 1])
        return H[n - 1] + slope[n - 1] * (b - B[n - 1]);

    for (int i = 0; i < n - 1; i++) {
        if (b >= B[i] && b <= B[i + 1]) {
            double l = B[i + 1] - B[i];
            double z = (b - B[i]) / l;
            double z2 = z * z;
            return (1.0 - 3.0 * z2 + 2.0 * z2 * z) * H[i] +
                   z * (1.0 - 2.0 * z + z2) * l * slope[i] +
                   z2 * (3.0 - 2.0 * z) * H[i + 1] +
                   z2 * (z - 1.0) * l * slope[i + 1];
        }
    }
    return 0;
}

void BHDataDialog::onPlotBH(bool logScale)
{
    std::vector<double> B, H;
    int npts;
    getData(B, H, npts);

    if (npts < 3) {
        QMessageBox::information(this, tr("Plot B-H Curve"),
            tr("Must have at least 3 pairs of data points."));
        return;
    }

    // Work on copies for spline computation (smoothing modifies data)
    std::vector<double> Bw = B, Hw = H;
    std::vector<double> slope;
    getSlopes(Bw, Hw, npts, slope);

    // Build interpolated curve (101 points like original FEMM)
    PlotSeries series;
    series.label = tr("B-H Curve");
    double db = (Bw[npts - 1] - Bw[0]) / 100.0;
    for (int i = 0; i <= 100; i++) {
        double b = Bw[0] + i * db;
        double h = getH(b, Bw, Hw, slope, npts);
        series.x.push_back(h);  // H on X axis
        series.y.push_back(b);  // B on Y axis
    }

    // Raw data points as markers
    PlotMarkers markers;
    for (int i = 0; i < npts; i++) {
        markers.x.push_back(H[i]);
        markers.y.push_back(B[i]);
    }

    QString title = m_matName.isEmpty() ? tr("B-H Curve") : tr("B-H Curve: %1").arg(m_matName);
    auto *dlg = new PlotDialog(title, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->plot()->addSeries(series);
    dlg->plot()->setMarkers(markers);
    dlg->plot()->setAxisLabels(tr("H, Amp/Meter"), tr("B, Tesla"));
    dlg->plot()->setLogX(logScale);
    dlg->show();
}
