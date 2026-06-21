// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// magnetics_handler.cpp — Magnetics-specific postprocessor dialogs
#include "magnetics_handler.h"
#include "complex_format.h"
#include "results_widget.h"
#include "results_doc.h"
#include "prefs_dialog.h"
#include "output_dialog.h"
#include "plot_widget.h"
#include "py_line_edit.h"
#include "py_state.h"

#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QTextEdit>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QApplication>
#include <QSettings>
#include <cmath>

#ifndef PI
#define PI 3.141592653589793238462643383279
#endif
#ifndef muo
#define muo 1.2566370614359173e-6
#endif

#define tr QObject::tr

void MagneticsHandler::showPrefsDialog(QWidget *parent)
{
    PrefsDialog dlg(parent);
    dlg.exec();
}

void MagneticsHandler::showIntegrateDialog(QWidget *parent, OutputDialog *out)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    auto fmtC = [](CComplex z) -> QString { return femmFmtComplex(z); };

    if (m_widget->mode() == 1) {
        // Line integral
        if (doc->contour.size() < 2) {
            m_widget->emitStatus("Define a contour path first");
            return;
        }
        QStringList types;
        types << "B.n" << "H.t" << "Contour length"
              << "Force from stress tensor" << "Torque from stress tensor"
              << "(B.n)^2";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Line Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        CComplex z[4] = {};
        doc->LineIntegral(intType, z);
        bool isAC = (doc->Frequency != 0);
        bool isAxi = (doc->ProblemType == 1);

        QString text;
        switch (intType) {
        case 0:
            text = QString("Normal flux = %1 Webers\n").arg(fmtC(z[0]));
            text += QString("Average B.n = %1 Tesla\n").arg(fmtC(z[1]));
            break;
        case 1:
            text = QString("MMF drop along contour = %1 Amp-turns\n").arg(fmtC(z[0]));
            text += QString("Average H.t = %1 Amp/Meter\n").arg(fmtC(z[1]));
            break;
        case 2:
            text = QString("Contour length = %1 meters\n").arg(z[0].re, 0, 'g', 8);
            text += QString("Surface Area = %1 meter^2\n").arg(z[0].im, 0, 'g', 8);
            break;
        case 3:
            if (!isAC) {
                if (!isAxi)
                    text = QString("Force in x-direction = %1 N\nForce in y-direction = %2 N\n")
                        .arg(z[0].re, 0, 'g', 8).arg(z[1].re, 0, 'g', 8);
                else
                    text = QString("Force in z-direction = %1 N\n").arg(z[1].re, 0, 'g', 8);
            } else {
                if (!isAxi) {
                    text = QString("DC Force:\n x-direction = %1 N\n y-direction = %2 N\n")
                        .arg(z[2].re, 0, 'g', 8).arg(z[3].re, 0, 'g', 8);
                    text += QString("\n%1 Hz Force:\n x-direction = %2 N\n y-direction = %3 N\n")
                        .arg(doc->Frequency * 2.0).arg(fmtC(z[0])).arg(fmtC(z[1]));
                } else {
                    text = QString("DC Force:\n z-direction = %1 N\n").arg(z[3].re, 0, 'g', 8);
                    text += QString("\n%1 Hz Force:\n z-direction = %2 N\n")
                        .arg(doc->Frequency * 2.0).arg(fmtC(z[1]));
                }
            }
            break;
        case 4:
            if (!isAC) {
                if (!isAxi)
                    text = QString("Torque about (0,0) = %1 N*m\n").arg(z[0].re, 0, 'g', 8);
                else
                    text = "No net torque in axisymmetric case.\n";
            } else {
                if (!isAxi) {
                    text = QString("Torque about (0,0):\nDC Torque = %1 N*m\n").arg(z[1].re, 0, 'g', 8);
                    text += QString("%1 Hz Torque = %2 N*m\n")
                        .arg(doc->Frequency * 2.0).arg(fmtC(z[0]));
                } else {
                    text = "No net torque in axisymmetric case.\n";
                }
            }
            break;
        case 5:
            text = QString("Result = %1 Webers^2\n").arg(fmtC(z[0]));
            text += QString("Average (B.n)^2 = %1 Tesla^2\n").arg(fmtC(z[1]));
            break;
        }
        out->setOutput(text);
        out->show();
    }
    else if (m_widget->mode() == 2) {
        bool blocksSelected = false;
        for (auto &lbl : doc->labellist)
            if (lbl.IsSelected) { blocksSelected = true; break; }

        // AGE integral dialog when no blocks selected
        if (!blocksSelected && !doc->agelist.empty()) {
            QDialog dlg(parent);
            dlg.setWindowTitle(tr("Air Gap Integrals"));
            auto *form = new QFormLayout;
            auto *ageCombo = new QComboBox;
            for (auto &age : doc->agelist)
                ageCombo->addItem(QString::fromStdString(age.BdryName));
            form->addRow(tr("Air Gap Element:"), ageCombo);
            auto *typeCombo = new QComboBox;
            if (!doc->bIncremental) {
                typeCombo->addItem(tr("Torque"));
                typeCombo->addItem(tr("Force"));
                typeCombo->addItem(tr("Energy"));
            } else {
                typeCombo->addItem(tr("Incremental Torque"));
                typeCombo->addItem(tr("Incremental Force"));
            }
            form->addRow(tr("Integral type:"), typeCombo);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
            QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
            auto *layout = new QVBoxLayout(&dlg);
            layout->addLayout(form);
            layout->addWidget(buttons);
            if (dlg.exec() != QDialog::Accepted) return;

            int dlgType = typeCombo->currentIndex();
            bool isAC = (doc->Frequency != 0);
            QString text;

            if (!doc->bIncremental) {
                int ageIdx = ageCombo->currentIndex();
                auto &age = doc->agelist[ageIdx];
                double R = (age.ri + age.ro) / 2.0;
                switch (dlgType) {
                case 0: { // Torque
                    double tq = 0;
                    for (int k = 0; k < age.nn; k++)
                        tq += (age.brc[k].re * age.btc[k].re + age.brc[k].im * age.btc[k].im +
                               age.brs[k].re * age.bts[k].re + age.brs[k].im * age.bts[k].im);
                    tq *= PI * R * R * doc->Depth / muo;
                    if (isAC) tq /= 2.0;
                    if (!isAC) {
                        text = QString("Torque = %1 N*m\n").arg(tq, 0, 'g', 8);
                    } else {
                        CComplex tq2x(0, 0);
                        for (int k = 0; k < age.nn; k++)
                            tq2x = tq2x + age.brc[k] * age.btc[k] + age.brs[k] * age.bts[k];
                        tq2x = tq2x * PI * R * R * doc->Depth / (2.0 * muo);
                        text = QString("DC Torque = %1 N*m\n2X Torque = %2 N*m\n").arg(tq, 0, 'g', 8).arg(fmtC(tq2x));
                    }
                    break;
                }
                case 1: { // Force
                    CComplex fx(0, 0), fy(0, 0);
                    if (round(age.totalArcLength) == 360) {
                        for (int k = 1; k < age.nn; k++) {
                            CComplex dfx = (age.brs[k] + age.btc[k]) * conj(age.brs[k-1] - age.btc[k-1]) +
                                           (age.brs[k-1] - age.btc[k-1]) * conj(age.brs[k] + age.btc[k]) +
                                           (age.brc[k] - age.bts[k]) * conj(age.brc[k-1] + age.bts[k-1]) +
                                           (age.brc[k-1] + age.bts[k-1]) * conj(age.brc[k] - age.bts[k]);
                            CComplex dfy = ((-age.brc[k] + age.bts[k]) * conj(age.brs[k-1] - age.btc[k-1]) +
                                           (age.brc[k-1] + age.bts[k-1]) * conj(age.brs[k] + age.btc[k]) +
                                           (age.brs[k] + age.btc[k]) * conj(age.brc[k-1] + age.bts[k-1]) +
                                           (-age.brs[k-1] + age.btc[k-1]) * conj(age.brc[k] - age.bts[k]));
                            fx.re += dfx.re; fy.re += dfy.re;
                        }
                        fx = fx * doc->Depth * PI * R / (4.0 * muo);
                        fy = fy * doc->Depth * PI * R / (4.0 * muo);
                        if (isAC) { fx = fx * 0.5; fy = fy * 0.5; }
                    }
                    if (!isAC) {
                        text = QString("Fx = %1 N\nFy = %2 N\n").arg(fx.re, 0, 'g', 8).arg(fy.re, 0, 'g', 8);
                    } else {
                        CComplex fx2(0, 0), fy2(0, 0);
                        if (round(age.totalArcLength) == 360) {
                            for (int k = 1; k < age.nn; k++) {
                                CComplex dfx2 = (age.brs[k-1] - age.btc[k-1]) * (age.brs[k] + age.btc[k]) +
                                               (age.brc[k-1] + age.bts[k-1]) * (age.brc[k] - age.bts[k]);
                                CComplex dfy2 = (age.brs[k] + age.btc[k]) * (age.brc[k-1] + age.bts[k-1]) -
                                               (age.brs[k-1] - age.btc[k-1]) * (age.brc[k] - age.bts[k]);
                                fx2 = fx2 + dfx2; fy2 = fy2 + dfy2;
                            }
                            fx2 = fx2 * doc->Depth * PI * R / (4.0 * muo);
                            fy2 = fy2 * doc->Depth * PI * R / (4.0 * muo);
                        }
                        text = QString("DC Fx = %1 N\nDC Fy = %2 N\n2X Fx = %3 N\n2X Fy = %4 N\n")
                            .arg(fx.re, 0, 'g', 8).arg(fy.re, 0, 'g', 8).arg(fmtC(fx2)).arg(fmtC(fy2));
                    }
                    break;
                }
                case 2: { // Energy
                    double Ri = age.ri / R, Ro = age.ro / R;
                    double dr = R * (Ro - Ri);
                    double W = 0;
                    for (int k = 0; k < age.nn; k++) {
                        int n = age.nh[k];
                        if (n != 0)
                            W += (abs(age.brs[k])*abs(age.brs[k]) + abs(age.brc[k])*abs(age.brc[k]) +
                                  abs(age.bts[k])*abs(age.bts[k]) + abs(age.btc[k])*abs(age.btc[k])) * dr;
                        else
                            W += 2.0 * dr * abs(age.btc[k]) * abs(age.btc[k]);
                    }
                    W *= PI * R * doc->Depth / (2.0 * muo);
                    if (isAC) W /= 2.0;
                    text = QString("%1 Energy = %2 J\n").arg(isAC ? "Average Stored" : "Stored").arg(W, 0, 'g', 8);
                    break;
                }
                }
            } else {
                int ageIdx = ageCombo->currentIndex();
                auto &age = doc->agelist[ageIdx];
                double R = (age.ri + age.ro) / 2.0;
                bool isAC2 = (doc->Frequency != 0);
                if (dlgType == 0) {
                    CComplex tq(0, 0);
                    for (int k = 0; k < age.nn; k++)
                        tq.re += age.btcPrev[k] * age.brc[k].re + age.brcPrev[k] * age.btc[k].re +
                                 age.btsPrev[k] * age.brs[k].re + age.brsPrev[k] * age.bts[k].re;
                    tq = tq * PI * R * R * doc->Depth / muo;
                    text = QString("Torque = %1 N*m\n").arg(fmtC(tq));
                } else {
                    CComplex fx(0, 0), fy(0, 0);
                    if (round(age.totalArcLength) == 360 && isAC2) {
                        for (int k = 1; k < age.nn; k++) {
                            double dfx = (age.brs[k].re + age.btc[k].re) * (age.brsPrev[k-1] - age.btcPrev[k-1]) +
                                         (age.brs[k-1].re - age.btc[k-1].re) * (age.brsPrev[k] + age.btcPrev[k]) +
                                         (age.brc[k].re - age.bts[k].re) * (age.brcPrev[k-1] + age.btsPrev[k-1]) +
                                         (age.brc[k-1].re + age.bts[k-1].re) * (age.brcPrev[k] - age.btsPrev[k]);
                            double dfy = (age.brsPrev[k] + age.btcPrev[k]) * (age.brc[k-1].re + age.bts[k-1].re) -
                                         (age.brsPrev[k-1] - age.btcPrev[k-1]) * (age.brc[k].re - age.bts[k].re) +
                                         (age.brs[k].re + age.btc[k].re) * (age.brcPrev[k-1] + age.btsPrev[k-1]) -
                                         (age.brs[k-1].re - age.btc[k-1].re) * (age.brcPrev[k] - age.btsPrev[k]);
                            fx.re += dfx; fy.re += dfy;
                        }
                        fx = fx * doc->Depth * PI * R / (2.0 * muo);
                        fy = fy * doc->Depth * PI * R / (2.0 * muo);
                    }
                    text = QString("1X Fx = %1 N\n1X Fy = %2 N\n").arg(fmtC(fx)).arg(fmtC(fy));
                }
            }
            out->setOutput(text);
            out->show();
            return;
        }

        if (!blocksSelected) {
            m_widget->emitStatus("Select blocks for integration (use Area mode)");
            return;
        }

        // Block integral
        QStringList types;
        types << "A . J" << "A" << "Magnetic field energy"
              << "Hysteresis, Laminated eddy, or Proximity effect"
              << "Resistive losses" << "Block cross-section area"
              << "Total losses" << "Total current" << "Integral of B over block"
              << "Block volume" << "Lorentz force (J x B)" << "Lorentz torque (r x J x B)"
              << "Magnetic field coenergy" << "Force via Weighted Stress Tensor"
              << "Torque via Weighted Stress Tensor"
              << "R^2 (i.e. Moment of Inertia / Density)" << "Total Loss Density";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Block Integral"),
            tr("Integral type:"), types, 2, false, &ok);
        if (!ok) return;
        int dlgIdx = types.indexOf(choice);
        bool isAC = (doc->Frequency != 0);
        bool isAxi = (doc->ProblemType == 1);
        CComplex z, z1, z2, z3, z4;
        QString text;

        // Set up progress dialog for block integrals that may trigger MakeMask
        QProgressDialog progDlg(tr("Masking..."),
                                QString(), 0, 100, parent);
        progDlg.setWindowModality(Qt::WindowModal);
        progDlg.setMinimumDuration(500);
        progDlg.setAutoReset(false);
        progDlg.setAutoClose(false);
        progDlg.setValue(0);
        doc->progressCallback = [&progDlg](int pct) {
            progDlg.setValue(pct);
            QApplication::processEvents();
        };

        switch (dlgIdx) {
        case 0: z = doc->BlockIntegral(0); text = QString("%1 Henry*Amp^2\n").arg(fmtC(z)); break;
        case 1:
            z = doc->BlockIntegral(1); z1 = doc->BlockIntegral(5);
            text = QString("Integral of A over selection:\n%1 Henry*Amp*meter^2\n").arg(fmtC(z));
            if (z1.re != 0) text += QString("Integral of A / selected area:\n%1 Henry*Amp\n").arg(fmtC(z / z1.re));
            break;
        case 2: z = doc->BlockIntegral(2); text = QString("%1 Joules\n").arg(z.re, 0, 'g', 8); break;
        case 3: z = doc->BlockIntegral(3); text = QString("%1 Watts\n").arg(z.re, 0, 'g', 8); break;
        case 4: z = doc->BlockIntegral(4); text = QString("%1 Watts\n").arg(z.re, 0, 'g', 8); break;
        case 5: z = doc->BlockIntegral(5); text = QString("%1 meter^2\n").arg(z.re, 0, 'g', 8); break;
        case 6: z = doc->BlockIntegral(6); text = QString("%1 Watts\n").arg(z.re, 0, 'g', 8); break;
        case 7: z = doc->BlockIntegral(7); text = QString("%1 Amps\n").arg(fmtC(z)); break;
        case 8: {
            z1 = doc->BlockIntegral(8); z2 = doc->BlockIntegral(9);
            QString cx = isAxi ? "r" : "x", cy = isAxi ? "z" : "y";
            text = QString("%1-component: %2 Tesla*meter^3\n%3-component: %4 Tesla*meter^3\n").arg(cx).arg(fmtC(z1)).arg(cy).arg(fmtC(z2));
            break;
        }
        case 9: z = doc->BlockIntegral(10); text = QString("%1 meter^3\n").arg(z.re, 0, 'g', 8); break;
        case 10: {
            z1 = doc->BlockIntegral(11); z2 = doc->BlockIntegral(12);
            if (!isAC) {
                if (!isAxi) text = QString("x-component: %1 N\ny-component: %2 N\n").arg(z1.re, 0, 'g', 8).arg(z2.re, 0, 'g', 8);
                else text = QString("r-component: 0 N\nz-component: %1 N\n").arg(z2.re, 0, 'g', 8);
            } else {
                z3 = doc->BlockIntegral(13); z4 = doc->BlockIntegral(14);
                if (!isAxi) {
                    text = QString("Steady-state force:\nx-component: %1 N\ny-component: %2 N\n").arg(z1.re, 0, 'g', 8).arg(z2.re, 0, 'g', 8);
                    text += QString("2x Frequency force:\nx-component: %1 N\ny-component: %2 N\n").arg(fmtC(z3)).arg(fmtC(z4));
                } else {
                    text = QString("Steady-state force:\nz-component: %1 N\n").arg(z2.re, 0, 'g', 8);
                    text += QString("2x Frequency force:\nz-component: %1 N\n").arg(fmtC(z4));
                }
            }
            break;
        }
        case 11:
            if (isAxi) { text = "No Lorentz torque possible in axisymmetric case.\n"; }
            else {
                z1 = doc->BlockIntegral(15);
                if (!isAC) text = QString("Torque about (0,0): %1 N*m\n").arg(z1.re, 0, 'g', 8);
                else { z2 = doc->BlockIntegral(16); text = QString("Torque about (0,0):\nSteady State: %1 N*m\n2x Frequency: %2 N*m\n").arg(z1.re, 0, 'g', 8).arg(fmtC(z2)); }
            }
            break;
        case 12: z = doc->BlockIntegral(17); text = QString("%1 Joules\n").arg(z.re, 0, 'g', 8); break;
        case 13: {
            z1 = doc->BlockIntegral(18); z2 = doc->BlockIntegral(19);
            if (!isAC) {
                if (!isAxi) text = QString("x-component: %1 N\ny-component: %2 N\n").arg(z1.re, 0, 'g', 8).arg(z2.re, 0, 'g', 8);
                else text = QString("r-component: 0 N\nz-component: %1 N\n").arg(z2.re, 0, 'g', 8);
            } else {
                z3 = doc->BlockIntegral(20); z4 = doc->BlockIntegral(21);
                if (!isAxi) {
                    text = QString("Steady-state force:\nx-component: %1 N\ny-component: %2 N\n").arg(z1.re, 0, 'g', 8).arg(z2.re, 0, 'g', 8);
                    text += QString("2x Frequency force:\nx-component: %1 N\ny-component: %2 N\n").arg(fmtC(z3)).arg(fmtC(z4));
                } else {
                    text = QString("Steady-state force:\nz-component: %1 N\n").arg(z2.re, 0, 'g', 8);
                    text += QString("2x Frequency force:\nz-component: %1 N\n").arg(fmtC(z4));
                }
            }
            break;
        }
        case 14:
            if (isAxi) { text = "No torque possible in axisymmetric case.\n"; }
            else {
                z1 = doc->BlockIntegral(22);
                if (!isAC) text = QString("Torque about (0,0): %1 N*m\n").arg(z1.re, 0, 'g', 8);
                else { z2 = doc->BlockIntegral(23); text = QString("Torque about (0,0):\nSteady State: %1 N*m\n2x Frequency: %2 N*m\n").arg(z1.re, 0, 'g', 8).arg(fmtC(z2)); }
            }
            break;
        case 15:
            z1 = doc->BlockIntegral(24);
            text = isAxi ? QString("Integral of R^2: %1 m^5\n").arg(z1.re, 0, 'g', 8)
                         : QString("Integral of (x^2 + y^2): %1 m^5\n").arg(z1.re, 0, 'g', 8);
            break;
        case 16: {
            z = doc->BlockIntegral(6); CComplex vol = doc->BlockIntegral(10);
            text = (vol.re != 0) ? QString("%1 Watts/meter^3\n").arg(z.re / vol.re, 0, 'g', 8) : "Cannot compute (zero volume).\n";
            break;
        }
        }
        doc->progressCallback = nullptr;
        out->setOutput(text);
        out->show();
    }
    else {
        m_widget->emitStatus("Select Contour or Area mode first");
    }
}

void MagneticsHandler::showXYPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;

    if (doc->contour.size() < 2) {
        if (doc->agelist.empty()) {
            m_widget->emitStatus("Define a contour path first (use Contour mode)");
            return;
        }
        // AGE plot dialog
        QDialog dlg(parent);
        dlg.setWindowTitle(tr("Plot Air Gap Element Field Values"));
        auto *form = new QFormLayout;
        auto *ageCombo = new QComboBox;
        for (auto &age : doc->agelist)
            ageCombo->addItem(QString::fromStdString(age.BdryName));
        form->addRow(tr("Air Gap Element:"), ageCombo);
        QStringList ageTypes;
        ageTypes << "Potential" << "|B|      (Magnitude of flux density)"
                 << "B . n   (Normal flux density)" << "B . t   (Tangential flux density)"
                 << "|H|      (Magnitude of field intensity)"
                 << "H . n   (Normal field intensity)" << "H . t   (Tangential field intensity)";
        auto *typeCombo = new QComboBox;
        for (auto &t : ageTypes) typeCombo->addItem(t);
        typeCombo->setCurrentIndex(1);
        form->addRow(tr("Quantity:"), typeCombo);
        auto *nptsEdit = new PyLineEdit;
        nptsEdit->setText(QSettings().value("pp/plotPoints", "1500").toString());
        form->addRow(tr("Number of points:"), nptsEdit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        auto *layout = new QVBoxLayout(&dlg);
        layout->addLayout(form);
        layout->addWidget(buttons);
        if (dlg.exec() != QDialog::Accepted) return;

        int ageIdx = ageCombo->currentIndex();
        int plotType = typeCombo->currentIndex();
        int npts = (int)nptsEdit->evaluateAsDouble(1000);
        if (npts < 10) npts = 10;
        if (npts > 100000) npts = 100000;
        std::vector<ResultsDoc::XYPlotPoint> pts;
        doc->GetGapValues(pts, plotType, npts, ageIdx);
        if (pts.empty()) { m_widget->emitStatus("No plot data"); return; }
        QString shortName = ageTypes[plotType];
        int paren = shortName.indexOf('(');
        if (paren > 0) shortName = shortName.left(paren).trimmed();
        int ncols = (int)pts[0].vals.size();
        QStringList colNames;
        if (ncols == 1) colNames << shortName;
        else if (ncols == 3) colNames << "|" + shortName + "|" << "Re(" + shortName + ")" << "Im(" + shortName + ")";
        auto *pdlg = new PlotDialog(shortName + " - " + ageCombo->currentText(), parent);
        pdlg->setAttribute(Qt::WA_DeleteOnClose);
        for (int c = 0; c < ncols; c++) {
            PlotSeries series;
            series.label = (c < colNames.size()) ? colNames[c] : shortName;
            for (auto &pt : pts) { series.x.push_back(pt.x); series.y.push_back(c < (int)pt.vals.size() ? pt.vals[c] : 0); }
            pdlg->plot()->addSeries(series);
        }
        pdlg->plot()->setAxisLabels(tr("Angle, Degrees"), shortName);
        pdlg->show();
        return;
    }

    // Contour X-Y plot
    QStringList types;
    types << "Potential" << "|B|      (Magnitude of flux density)"
          << "B . n   (Normal flux density)" << "B . t   (Tangential flux density)"
          << "|H|      (Magnitude of field intensity)"
          << "H . n   (Normal field intensity)" << "H . t   (Tangential field intensity)";
    if (doc->Frequency != 0)
        types << "J_eddy     (Eddy current density)" << "Js+J_eddy  (Total current density)";

    QDialog dlg(parent);
    dlg.setWindowTitle(tr("X-Y Plot"));
    auto *form = new QFormLayout;
    auto *typeCombo = new QComboBox;
    for (auto &t : types) typeCombo->addItem(t);
    typeCombo->setCurrentIndex(1);
    form->addRow(tr("Quantity:"), typeCombo);
    auto *nptsEdit = new PyLineEdit;
    nptsEdit->setText(QSettings().value("pp/plotPoints", "1500").toString());
    form->addRow(tr("Number of points:"), nptsEdit);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    int plotType = typeCombo->currentIndex();
    int npts = (int)nptsEdit->evaluateAsDouble(150);
    if (npts < 10) npts = 10;
    if (npts > 100000) npts = 100000;
    std::vector<ResultsDoc::XYPlotPoint> pts;
    doc->GetLineValues(pts, plotType, npts);
    if (pts.empty()) { m_widget->emitStatus("No plot data"); return; }
    QString shortName = types[plotType];
    int paren = shortName.indexOf('(');
    if (paren > 0) shortName = shortName.left(paren).trimmed();
    int ncols = (int)pts[0].vals.size();
    QStringList colNames;
    if (ncols == 1) colNames << shortName;
    else if (ncols == 3) colNames << "|" + shortName + "|" << "Re(" + shortName + ")" << "Im(" + shortName + ")";
    auto *pdlg = new PlotDialog(shortName + " along contour", parent);
    pdlg->setAttribute(Qt::WA_DeleteOnClose);
    for (int c = 0; c < ncols; c++) {
        PlotSeries series;
        series.label = (c < colNames.size()) ? colNames[c] : shortName;
        for (auto &pt : pts) { series.x.push_back(pt.x); series.y.push_back(c < (int)pt.vals.size() ? pt.vals[c] : 0); }
        pdlg->plot()->addSeries(series);
    }
    pdlg->plot()->setAxisLabels(tr("Position"), shortName);
    pdlg->show();
}

void MagneticsHandler::showCircuitDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;

    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Circuit Properties"));
    auto *combo = new QComboBox;
    for (auto &cp : doc->circproplist)
        combo->addItem(QString::fromStdString(cp.CircName));
    auto *textEdit = new QTextEdit;
    textEdit->setReadOnly(true);
    textEdit->setFont(QFont("Consolas", 9));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::close);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(combo);
    layout->addWidget(textEdit);
    layout->addWidget(buttons);
    dlg.resize(400, 300);

    if (doc->circproplist.empty()) {
        textEdit->setPlainText("No circuits defined in this problem.");
    }

    bool isAC = (doc->Frequency != 0);
    auto fmtC = [](CComplex z) -> QString { return femmFmtComplex(z); };
    auto updateText = [&](int idx) {
        if (idx < 0 || idx >= (int)doc->circproplist.size()) return;
        CComplex amps = doc->circproplist[idx].Amps;
        CComplex volts = doc->GetVoltageDrop(idx);
        CComplex flux = doc->GetFluxLinkage(idx);
        QString text;
        text += QString("Total Current = %1 Amps\n").arg(fmtC(amps));
        text += QString("Voltage Drop = %1 Volts\n").arg(fmtC(volts));
        text += QString("Flux Linkage = %1 Webers\n").arg(fmtC(flux));
        if (abs(amps) > 0) {
            text += QString("Flux/Current = %1 Henries\n").arg(fmtC(flux / amps));
            text += QString("Voltage/Current = %1 Ohms\n").arg(fmtC(volts / amps));
        }
        if (!isAC) {
            text += QString("Power = %1 Watts\n").arg(Re(amps * volts), 0, 'g', 8);
        } else {
            text += QString("Real Power = %1 Watts\n").arg(Re(volts * conj(amps)) / 2.0, 0, 'g', 8);
            text += QString("Reactive Power = %1 VAr\n").arg(Im(volts * conj(amps)) / 2.0, 0, 'g', 8);
            text += QString("Apparent Power = %1 VA\n").arg(abs(volts) * abs(amps) / 2.0, 0, 'g', 8);
        }
        textEdit->setPlainText(text);
    };
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateText);
    updateText(0);
    dlg.exec();
}

void MagneticsHandler::showContourPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    bool isAC = (doc->Frequency != 0);
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Contour Plot"));
    auto *form = new QFormLayout;
    auto *showAr = new QCheckBox(isAC ? tr("Show Re(A) contours") : tr("Show contour plot"));
    showAr->setChecked(m_widget->m_showAr);
    form->addRow(showAr);
    QCheckBox *showAi = nullptr;
    if (isAC) { showAi = new QCheckBox(tr("Show Im(A) contours")); showAi->setChecked(m_widget->m_showAi); form->addRow(showAi); }
    auto *numContours = new PyLineEdit;
    numContours->setText(QString::number(m_widget->numContours()));
    form->addRow(tr("Number of contours:"), numContours);
    auto *aLow = new PyLineEdit;
    aLow->setText(QString::number(doc->A_low, 'g', 15));
    form->addRow(tr("Lower bound:"), aLow);
    auto *aHigh = new PyLineEdit;
    aHigh->setText(QString::number(doc->A_high, 'g', 15));
    form->addRow(tr("Upper bound:"), aHigh);
    auto *showMaskCb = new QCheckBox(tr("Show stress tensor mask"));
    showMaskCb->setChecked(m_widget->m_showMask);
    form->addRow(showMaskCb);
    auto *defaultsBtn = new QPushButton(tr("Defaults"));
    QObject::connect(defaultsBtn, &QPushButton::clicked, [&]() {
        numContours->setText("19");
        aLow->setText(QString::number(doc->A_lb, 'g', 15));
        aHigh->setText(QString::number(doc->A_ub, 'g', 15));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->addButton(defaultsBtn, QDialogButtonBox::ActionRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    m_widget->m_showAr = showAr->isChecked();
    if (showAi) m_widget->m_showAi = showAi->isChecked();
    m_widget->m_showMask = showMaskCb->isChecked();
    m_widget->setShowContours(m_widget->m_showAr || (showAi && m_widget->m_showAi));
    m_widget->setNumContours((int)numContours->evaluateAsDouble(19));
    doc->A_low = aLow->evaluateAsDouble(doc->A_lb);
    doc->A_high = aHigh->evaluateAsDouble(doc->A_ub);
    m_widget->update();
}

void MagneticsHandler::showDensityPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    bool isAC = (doc->Frequency != 0);
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Density Plot"));
    auto *form = new QFormLayout;
    auto *showIt = new QCheckBox(tr("Show density plot"));
    showIt->setChecked(m_widget->showDensity());
    form->addRow(showIt);
    auto *typeCombo = new QComboBox;
    if (!isAC) {
        typeCombo->addItem(tr("Flux Density (T)"));
        typeCombo->addItem(tr("Field Intensity (A/m)"));
        typeCombo->addItem(tr("Current Density (MA/m^2)"));
        typeCombo->addItem(tr("log10(|Flux Density|) Log(T)"));
    } else {
        typeCombo->addItem(tr("|Flux Density| (T)"));
        typeCombo->addItem(tr("Re(Flux Density) (T)"));
        typeCombo->addItem(tr("Im(Flux Density) (T)"));
        typeCombo->addItem(tr("|Field Intensity| (A/m)"));
        typeCombo->addItem(tr("Re(Field Intensity) (A/m)"));
        typeCombo->addItem(tr("Im(Field Intensity) (A/m)"));
        typeCombo->addItem(tr("|Current Density| (MA/m^2)"));
        typeCombo->addItem(tr("Re(Current Density) (MA/m^2)"));
        typeCombo->addItem(tr("Im(Current Density) (MA/m^2)"));
        typeCombo->addItem(tr("log10(|Flux Density|) Log(T)"));
    }
    int curType = m_widget->m_densityPlotType - 1;
    if (curType < 0) curType = 0;
    if (curType >= typeCombo->count()) curType = 0;
    typeCombo->setCurrentIndex(curType);
    form->addRow(tr("Plot type:"), typeCombo);
    auto *lbEdit = new PyLineEdit;
    auto *ubEdit = new PyLineEdit;
    lbEdit->setText(QString::number(doc->PlotBounds[curType][0], 'g', 15));
    ubEdit->setText(QString::number(doc->PlotBounds[curType][1], 'g', 15));
    form->addRow(tr("Lower bound:"), lbEdit);
    form->addRow(tr("Upper bound:"), ubEdit);
    int *pCurType = &curType;
    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [&, pCurType](int idx) {
        doc->PlotBounds[*pCurType][0] = lbEdit->evaluateAsDouble(doc->d_PlotBounds[*pCurType][0]);
        doc->PlotBounds[*pCurType][1] = ubEdit->evaluateAsDouble(doc->d_PlotBounds[*pCurType][1]);
        *pCurType = idx;
        lbEdit->setText(QString::number(doc->PlotBounds[idx][0], 'g', 15));
        ubEdit->setText(QString::number(doc->PlotBounds[idx][1], 'g', 15));
    });
    auto *showLegend = new QCheckBox(tr("Show legend")); showLegend->setChecked(m_widget->m_showLegend); form->addRow(showLegend);
    auto *greyScale = new QCheckBox(tr("Greyscale")); greyScale->setChecked(m_widget->m_greyScale); form->addRow(greyScale);
    auto *defaultsBtn = new QPushButton(tr("Defaults"));
    QObject::connect(defaultsBtn, &QPushButton::clicked, [&, pCurType]() {
        lbEdit->setText(QString::number(doc->d_PlotBounds[*pCurType][0], 'g', 15));
        ubEdit->setText(QString::number(doc->d_PlotBounds[*pCurType][1], 'g', 15));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->addButton(defaultsBtn, QDialogButtonBox::ActionRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    int finalIdx = typeCombo->currentIndex();
    doc->PlotBounds[finalIdx][0] = lbEdit->evaluateAsDouble(doc->d_PlotBounds[finalIdx][0]);
    doc->PlotBounds[finalIdx][1] = ubEdit->evaluateAsDouble(doc->d_PlotBounds[finalIdx][1]);
    if (showIt->isChecked()) { m_widget->m_densityPlotType = finalIdx + 1; m_widget->setShowDensity(true); m_widget->m_showVectors = false; }
    else { m_widget->setShowDensity(false); }
    m_widget->m_showLegend = showLegend->isChecked();
    m_widget->m_greyScale = greyScale->isChecked();
    m_widget->update();
}

void MagneticsHandler::showVectorPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Vector Plot"));
    auto *form = new QFormLayout;
    auto *showIt = new QCheckBox(tr("Show vector plot")); showIt->setChecked(m_widget->m_showVectors); form->addRow(showIt);
    auto *typeCombo = new QComboBox;
    typeCombo->addItem(tr("Flux Density (B)")); typeCombo->addItem(tr("Field Intensity (H)"));
    typeCombo->setCurrentIndex(m_widget->m_vectorType);
    form->addRow(tr("Quantity:"), typeCombo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    m_widget->m_showVectors = showIt->isChecked();
    m_widget->m_vectorType = typeCombo->currentIndex();
    if (m_widget->m_showVectors) m_widget->setShowDensity(false);
    m_widget->update();
}

void MagneticsHandler::showProblemInfo(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    showProblemInfoDialog(parent, doc->LengthUnits, doc->ProblemType,
                          doc->Depth, doc->Frequency,
                          (int)doc->meshnode.size(), (int)doc->meshelem.size());
}

void MagneticsHandler::showBHCurves(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    std::vector<int> matIdx;
    for (int i = 0; i < (int)doc->blockproplist.size(); i++)
        if (doc->blockproplist[i].BHpoints > 0) matIdx.push_back(i);
    if (matIdx.empty()) { m_widget->emitStatus("No nonlinear materials in this solution"); return; }
    auto *pdlg = new PlotDialog(tr("B-H Curves"), parent);
    pdlg->setAttribute(Qt::WA_DeleteOnClose);
    for (size_t mi = 0; mi < matIdx.size(); mi++) {
        auto &mat = doc->blockproplist[matIdx[mi]];
        PlotSeries series;
        series.label = QString::fromStdString(mat.BlockName);
        double bMax = mat.Bdata.back();
        for (int j = 0; j <= 200; j++) {
            double b = bMax * j / 200;
            series.x.push_back(mat.GetH(b));
            series.y.push_back(b);
        }
        pdlg->plot()->addSeries(series);
    }
    pdlg->plot()->setAxisLabels(tr("H, A/m"), tr("B, Tesla"));
    pdlg->show();
}

#undef tr
