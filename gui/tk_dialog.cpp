// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// tk_dialog.cpp — T-k curve data editor for temperature-dependent thermal
// conductivity. Ported from femm42src/femm/KCurve.cpp (CTKData) and
// TKDatafile.cpp (CTKDatafile).

#include "tk_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>

static CurveDataDialog::Spec tkSpec()
{
    CurveDataDialog::Spec s;
    s.windowTitle    = QObject::tr("T-k Curve Data");
    s.namedTitle     = QObject::tr("T-k Curve: %1");
    s.prompt         = QObject::tr("Enter temperature and thermal conductivity values (one per line):");
    s.xHeader        = QObject::tr("T (K)");
    s.yHeader        = QObject::tr("k (W/(m*K))");
    s.xPlaceholder   = QObject::tr("Temperature values (K)");
    s.yPlaceholder   = QObject::tr("Conductivity values (W/(m*K))");
    s.plotButton     = QObject::tr("Plot T-k Curve");
    s.logPlotButton  = QObject::tr("Log Plot T-k Curve");
    s.plotTitle      = QObject::tr("T-k Curve");
    s.xAxisLabel     = QObject::tr("T, K");
    s.yAxisLabel     = QObject::tr("k, W/(m*K)");
    s.importTitle    = QObject::tr("Import T-k Data");
    return s;
}

TKDataDialog::TKDataDialog(QWidget *parent) : CurveDataDialog(tkSpec(), parent)
{
}

void TKDataDialog::conditionData(std::vector<double> &T, std::vector<double> &K) const
{
    const size_t n = T.size();
    if (n < 2) return;

    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(),
                     [&T](size_t a, size_t b) { return T[a] < T[b]; });

    std::vector<double> Ts(n), Ks(n);
    for (size_t i = 0; i < n; i++) { Ts[i] = T[idx[i]]; Ks[i] = K[idx[i]]; }
    T.swap(Ts);
    K.swap(Ks);
}

// Linear interpolation with the ends held flat, matching HMaterialProp::GetK
// and Original FEMM's CTKData::GetK. This is deliberately the same rule the
// solver uses, so what the plot shows is what will be solved.
static double getK(double t, const std::vector<double> &T,
                   const std::vector<double> &K)
{
    const int n = (int)T.size();
    if (n == 0) return 0;
    if (n == 1) return K[0];
    if (t <= T[0]) return K[0];
    if (t >= T[n - 1]) return K[n - 1];

    for (int i = 0, j = 1; j < n; i++, j++)
        if (t >= T[i] && t <= T[j])
            return K[i] + (K[j] - K[i]) * (t - T[i]) / (T[j] - T[i]);

    return 0;
}

CurveDataDialog::PlotData TKDataDialog::buildPlotData(const std::vector<double> &T,
                                                      const std::vector<double> &K) const
{
    const int npts = (int)T.size();
    PlotData pd;

    // 101 points across the curve, as Original FEMM draws it. Unlike the B-H
    // curve, this one is plotted the usual way round: T across, k up.
    double dt = (T[npts - 1] - T[0]) / 100.0;
    for (int i = 0; i <= 100; i++) {
        double t = T[0] + i * dt;
        pd.cx.push_back(t);
        pd.cy.push_back(getK(t, T, K));
    }

    pd.mx = T;
    pd.my = K;
    return pd;
}

// ================================================================
// Import options — what is in the file being read
// Ported from femm42src/femm/TKDatafile.cpp (CTKDatafile)
// ================================================================

namespace {

class TKImportOptionsDialog : public QDialog {
public:
    explicit TKImportOptionsDialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(tr("T-k Data File Format"));

        auto *form = new QFormLayout;

        m_order = new QComboBox;
        m_order->addItem(tr("Temperature first"));
        m_order->addItem(tr("Conductivity first"));
        form->addRow(tr("Column order:"), m_order);

        m_tUnits = new QComboBox;
        m_tUnits->addItem(tr("Kelvin"));
        m_tUnits->addItem(QString::fromUtf8("Celsius (°C)"));
        m_tUnits->addItem(QString::fromUtf8("Fahrenheit (°F)"));
        m_tUnits->addItem(QString::fromUtf8("Rankine (°R)"));
        form->addRow(tr("Temperature units:"), m_tUnits);

        m_kUnits = new QComboBox;
        m_kUnits->addItem(tr("W/(m*K)"));
        m_kUnits->addItem(tr("Btu/(ft*hr*F)"));
        m_kUnits->addItem(tr("kcal/(m*hr*K)"));
        form->addRow(tr("Conductivity units:"), m_kUnits);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(tr("Describe the two-column data file being imported.")));
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    int order() const { return m_order->currentIndex(); }
    int tUnits() const { return m_tUnits->currentIndex(); }
    int kUnits() const { return m_kUnits->currentIndex(); }

private:
    QComboBox *m_order, *m_tUnits, *m_kUnits;
};

} // namespace

bool TKDataDialog::importOptions()
{
    TKImportOptionsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return false;
    m_order = dlg.order();
    m_tUnits = dlg.tUnits();
    m_kUnits = dlg.kUnits();
    return true;
}

void TKDataDialog::convertImported(double &T, double &K) const
{
    if (m_order == 1) std::swap(T, K);

    switch (m_tUnits) {
    case 1: T += 273.15;            break;   // Celsius
    case 2: T = 5.0 * (T + 459.67) / 9.0; break;   // Fahrenheit
    case 3: T = 5.0 * T / 9.0;      break;   // Rankine
    default: break;                          // already Kelvin
    }

    switch (m_kUnits) {
    case 1: K *= 1.73073466637139;  break;   // Btu/(ft*hr*F)
    case 2: K *= 1.163;             break;   // kcal/(m*hr*K)
    default: break;                          // already W/(m*K)
    }
}
