// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// electrostatics_handler.cpp — Electrostatics-specific postprocessor dialogs
#include "electrostatics_handler.h"
#include "xy_plot.h"
#include "scalar_results_widget.h"
#include "elec_results_doc.h"
#include "scalar_prefs_dialog.h"
#include "output_dialog.h"
#include "plot_widget.h"
#include "py_line_edit.h"

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
#include <memory>
#include <QApplication>
#include <QSettings>

#include "mono_font.h"
#define tr QObject::tr

void ElectrostaticsHandler::showPrefsDialog(QWidget *parent)
{
    ScalarPrefsDialog dlg(parent);
    dlg.exec();
}

void ElectrostaticsHandler::showIntegrateDialog(QWidget *parent, OutputDialog *out)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    auto fmtD = [](double v) -> QString { return QString::number(v, 'g', 8); };
    bool isAxi = (doc->ProblemType == 1);

    if (m_widget->mode() == 1) {
        if (doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first"); return; }
        QStringList types;
        types << "E.t" << "D.n" << "Contour length"
              << "Force from stress tensor" << "Torque from stress tensor";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Line Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        double z[4] = {};
        // Sampling density comes from the user's preference, read here rather
        // than at load time so a change takes effect on the next integral.
        doc->d_LineIntegralPoints = QSettings().value("eo/liPoints", 400).toInt();
        doc->LineIntegral(intType, z);
        QString text;
        switch (intType) {
        case 0: text = QString("Voltage drop = %1 V\n").arg(fmtD(z[0])); break;
        case 1: text = QString("Total charge on surface = %1 C\nAverage D.n = %2 C/m^2\n").arg(fmtD(z[0])).arg(fmtD(z[1])); break;
        case 2: text = QString("Contour length = %1 meters\n").arg(fmtD(z[0])); text += QString("Surface area = %1 meter^2\n").arg(fmtD(z[1])); break;
        case 3:
            if (!isAxi) text = QString("Force in x-direction = %1 N\nForce in y-direction = %2 N\n").arg(fmtD(z[0])).arg(fmtD(z[1]));
            else text = QString("Force in z-direction = %1 N\n").arg(fmtD(z[1]));
            break;
        case 4:
            if (!isAxi) text = QString("Torque about (0,0) = %1 N*m\n").arg(fmtD(z[0]));
            else text = "No net torque in axisymmetric case.\n";
            break;
        }
        out->setOutput(text);
        out->show();
    } else if (m_widget->mode() == 2) {
        bool blocksSelected = false;
        for (auto &lbl : doc->labellist)
            if (lbl.IsSelected) { blocksSelected = true; break; }
        // A weighted-stress-tensor force/torque acts on a conductor, which is a
        // mesh-node selection (right-click) rather than a block selection.
        bool nodesSelected = false;
        for (auto &mn : doc->meshnode)
            if (mn.IsSelected) { nodesSelected = true; break; }
        if (!blocksSelected && !nodesSelected) {
            m_widget->emitStatus("Select a block or a conductor for integration (Area mode)");
            return;
        }
        QStringList types;
        types << "Stored energy" << "Block cross-section area" << "Block volume"
              << "Average D over block" << "Average E over block"
              << "Force via Weighted Stress Tensor" << "Torque via Weighted Stress Tensor";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Block Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        // Force/torque (5, 6) may use a conductor (mesh-node) selection; the
        // block-based integrals need selected blocks.
        if (intType < 5 && !blocksSelected) {
            m_widget->emitStatus("Select one or more blocks for this integral");
            return;
        }

        // The mask (WST) integrals run a linear solve; MakeMask fires the first
        // progress callback immediately before that solve, and only after it has
        // accepted the selection. Create the progress dialog lazily on that first
        // callback, so a selection rejected before the solve never pops a dialog.
        std::unique_ptr<QProgressDialog> progDlg;
        // No `this` capture: tr() is a static member, so the body needs nothing
        // from the enclosing object (clang -Wunused-lambda-capture).
        doc->progressCallback = [&progDlg, parent](int pct) {
            if (!progDlg) {
                progDlg = std::make_unique<QProgressDialog>(tr("Masking..."),
                                                            QString(), 0, 100, parent);
                progDlg->setWindowModality(Qt::WindowModal);
                progDlg->setAutoReset(false);
                progDlg->setAutoClose(false);
                progDlg->setMinimumDuration(0);
                progDlg->show();
            }
            progDlg->setValue(pct);
            QApplication::processEvents();
        };

        CComplex z = doc->BlockIntegral(intType);
        QString text;
        switch (intType) {
        case 0: text = QString("%1 Joules\n").arg(fmtD(z.re)); break;
        case 1: text = QString("%1 meter^2\n").arg(fmtD(z.re)); break;
        case 2: text = QString("%1 meter^3\n").arg(fmtD(z.re)); break;
        case 3: text = QString("Average Dx = %1 C/m^2\nAverage Dy = %2 C/m^2\n").arg(fmtD(z.re)).arg(fmtD(z.im)); break;
        case 4: text = QString("Average Ex = %1 V/m\nAverage Ey = %2 V/m\n").arg(fmtD(z.re)).arg(fmtD(z.im)); break;
        case 5:
            if (!isAxi) text = QString("Force in x-direction = %1 N\nForce in y-direction = %2 N\n").arg(fmtD(z.re)).arg(fmtD(z.im));
            else text = QString("Force in z-direction = %1 N\n").arg(fmtD(z.im));
            break;
        case 6:
            if (!isAxi) text = QString("Torque about (0,0) = %1 N*m\n").arg(fmtD(z.re));
            else text = "No net torque in axisymmetric case.\n";
            break;
        }
        doc->progressCallback = nullptr;
        progDlg.reset();
        if (doc->maskSelectionInvalid) {
            QMessageBox::warning(parent, tr("Invalid Selection"),
                tr("The selected region is invalid. A valid selection cannot abut "
                   "a region which is not free space."));
            return;
        }
        out->setOutput(text);
        out->show();
    } else {
        m_widget->emitStatus("Select Contour or Area mode first");
    }
}

void ElectrostaticsHandler::showXYPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    if (doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first (use Contour mode)"); return; }
    // The list lives in libfemm so *_makeplot can label its exported
    // columns with the same names.
    const QStringList types = xyplot::quantityList(xyexport::elecPlotNames());
    int plotType = 0, npts = 0;
    if (!xyplot::prompt(parent, types, 0, "eo/plotPoints", plotType, npts)) return;

    std::vector<ElecResultsDoc::XYPlotPoint> pts;
    doc->GetLineValues(pts, plotType, npts);
    if (pts.empty()) { m_widget->emitStatus("No data for plot"); return; }
    xyplot::showAlongContour(parent, types[plotType], pts);
}

void ElectrostaticsHandler::showCircuitDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Conductor Properties"));
    auto *combo = new QComboBox;
    for (auto &cp : doc->circproplist) combo->addItem(QString::fromStdString(cp.CircName));
    auto *textEdit = new QTextEdit;
    textEdit->setReadOnly(true);
    textEdit->setFont(monoFont(9));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::close);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(combo);
    layout->addWidget(textEdit);
    layout->addWidget(buttons);
    dlg.resize(350, 200);
    if (doc->circproplist.empty()) {
        textEdit->setPlainText("No conductors defined in this problem.");
    }
    auto updateText = [&](int idx) {
        if (idx < 0 || idx >= (int)doc->circproplist.size()) return;
        QString text;
        text += QString("Voltage = %1 V\n").arg(doc->circproplist[idx].V, 0, 'g', 8);
        text += QString("Charge = %1 C\n").arg(doc->circproplist[idx].q, 0, 'g', 8);
        textEdit->setPlainText(text);
    };
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateText);
    updateText(0);
    dlg.exec();
}

void ElectrostaticsHandler::showContourPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Contour Plot"));
    auto *form = new QFormLayout;
    auto *showV = new QCheckBox(tr("Show voltage contours"));
    showV->setChecked(m_widget->showContours());
    form->addRow(showV);
    auto *numEdit = new PyLineEdit;
    numEdit->setText(QString::number(m_widget->numContours()));
    form->addRow(tr("Number of contours:"), numEdit);
    auto *loEdit = new PyLineEdit;
    loEdit->setText(QString::number(doc->A_Low, 'g', 10));
    form->addRow(tr("Lower bound:"), loEdit);
    auto *hiEdit = new PyLineEdit;
    hiEdit->setText(QString::number(doc->A_High, 'g', 10));
    form->addRow(tr("Upper bound:"), hiEdit);
    auto *showMaskCb = new QCheckBox(tr("Show stress tensor mask"));
    showMaskCb->setChecked(m_widget->m_showMask);
    form->addRow(showMaskCb);
    auto *dfltBtn = new QPushButton(tr("Defaults"));
    QObject::connect(dfltBtn, &QPushButton::clicked, [&]() {
        loEdit->setText(QString::number(doc->A_lb, 'g', 10));
        hiEdit->setText(QString::number(doc->A_ub, 'g', 10));
        numEdit->setText("19");
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(dfltBtn);
    layout->addWidget(buttons);
    if (dlg.exec() == QDialog::Accepted) {
        m_widget->setShowContours(showV->isChecked());
        m_widget->setNumContours((int)numEdit->evaluateAsDouble(19));
        doc->A_Low = loEdit->evaluateAsDouble(doc->A_lb);
        doc->A_High = hiEdit->evaluateAsDouble(doc->A_ub);
        m_widget->m_showMask = showMaskCb->isChecked();
        m_widget->update();
    }
}

void ElectrostaticsHandler::showDensityPlotDialog(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    int boundsMap[] = {-1, 1, 2, 0, 3};
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Density Plot"));
    auto *form = new QFormLayout;
    auto *showIt = new QCheckBox(tr("Show density plot"));
    showIt->setChecked(m_widget->showDensity() && m_widget->m_densityPlotType > 0);
    form->addRow(showIt);
    auto *typeCombo = new QComboBox;
    typeCombo->addItem(tr("|D|")); typeCombo->addItem(tr("|E|"));
    typeCombo->addItem(tr("V (voltage)")); typeCombo->addItem(tr("Energy density"));
    int curComboIdx = (m_widget->m_densityPlotType > 0) ? m_widget->m_densityPlotType - 1 : 2;
    typeCombo->setCurrentIndex(curComboIdx);
    form->addRow(tr("Plot type:"), typeCombo);
    auto *loEdit = new PyLineEdit;
    auto *hiEdit = new PyLineEdit;
    form->addRow(tr("Lower bound:"), loEdit);
    form->addRow(tr("Upper bound:"), hiEdit);
    auto updateBounds = [&](int comboIdx) {
        int bi = boundsMap[comboIdx + 1];
        if (bi >= 0 && bi < 4) {
            loEdit->setText(QString::number(doc->PlotBounds[bi][0], 'g', 10));
            hiEdit->setText(QString::number(doc->PlotBounds[bi][1], 'g', 10));
        }
    };
    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateBounds);
    updateBounds(curComboIdx);
    auto *showLegendCb = new QCheckBox(tr("Show legend")); showLegendCb->setChecked(m_widget->m_showLegend); form->addRow(showLegendCb);
    auto *greyScaleCb = new QCheckBox(tr("Greyscale")); greyScaleCb->setChecked(m_widget->m_greyScale); form->addRow(greyScaleCb);
    auto *dfltBtn = new QPushButton(tr("Defaults"));
    QObject::connect(dfltBtn, &QPushButton::clicked, [&]() {
        int bi = boundsMap[typeCombo->currentIndex() + 1];
        if (bi >= 0 && bi < 4) {
            loEdit->setText(QString::number(doc->d_PlotBounds[bi][0], 'g', 10));
            hiEdit->setText(QString::number(doc->d_PlotBounds[bi][1], 'g', 10));
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(dfltBtn);
    layout->addWidget(buttons);
    if (dlg.exec() == QDialog::Accepted) {
        int densType = typeCombo->currentIndex() + 1;
        m_widget->m_densityPlotType = showIt->isChecked() ? densType : 0;
        m_widget->setShowDensity(showIt->isChecked());
        if (showIt->isChecked()) m_widget->m_showVectors = false;
        m_widget->m_showLegend = showLegendCb->isChecked();
        m_widget->m_greyScale = greyScaleCb->isChecked();
        int bi = boundsMap[densType];
        if (bi >= 0 && bi < 4) {
            doc->PlotBounds[bi][0] = loEdit->evaluateAsDouble(doc->d_PlotBounds[bi][0]);
            doc->PlotBounds[bi][1] = hiEdit->evaluateAsDouble(doc->d_PlotBounds[bi][1]);
        }
        m_widget->update();
    }
}

void ElectrostaticsHandler::showVectorPlotDialog(QWidget *parent)
{
    QStringList types;
    types << "Off" << "D (displacement)" << "E (electric field)";
    int cur = m_widget->m_showVectors ? (m_widget->m_vectorType + 1) : 0;
    bool ok;
    QString choice = QInputDialog::getItem(parent, tr("Vector Plot"), tr("Vector type:"), types, cur, false, &ok);
    if (ok) {
        int idx = types.indexOf(choice);
        m_widget->m_showVectors = (idx > 0);
        if (idx > 0) { m_widget->m_vectorType = idx - 1; m_widget->setShowDensity(false); }
        m_widget->update();
    }
}

void ElectrostaticsHandler::showProblemInfo(QWidget *parent)
{
    auto *doc = m_widget->document();
    if (!doc) return;
    showProblemInfoDialog(parent, doc->LengthUnits, doc->ProblemType,
                          doc->Depth, -1,
                          (int)doc->meshnode.size(), (int)doc->meshelem.size());
}

#undef tr
