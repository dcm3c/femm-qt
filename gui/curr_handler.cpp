// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_handler.cpp — Current flow postprocessor dialogs
#include "curr_handler.h"
#include "xy_plot.h"
#include "complex_format.h"
#include "scalar_results_widget.h"
#include "curr_results_doc.h"
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
#include <QProgressDialog>
#include <memory>
#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <cmath>

#define tr QObject::tr

CurrentFlowHandler::CurrentFlowHandler(ScalarResultsWidget *widget, CurrResultsDoc *doc)
    : m_widget(widget), m_doc(doc)
{
}

void CurrentFlowHandler::showPrefsDialog(QWidget *parent)
{
    ScalarPrefsDialog dlg(parent, 3);
    dlg.exec();
}

void CurrentFlowHandler::showIntegrateDialog(QWidget *parent, OutputDialog *out)
{
    if (!m_doc) return;
    auto fmtD = [](double v) -> QString { return QString::number(v, 'g', 8); };
    auto fmtC = [](CComplex z) -> QString { return femmFmtComplex(z); };

    if (m_widget->mode() == 1) {
        // Line integrals
        if (m_doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first"); return; }
        QStringList types;
        types << "E.t (voltage drop)" << "J.n (current)"
              << "Contour length" << "Average voltage";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Line Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        CComplex z[4] = {};
        m_doc->LineIntegral(intType, z);
        QString text;
        switch (intType) {
        case 0:
            text = QString("Voltage drop = %1 V\n").arg(fmtC(z[0]));
            break;
        case 1:
            text = QString("Total current = %1 A\n").arg(fmtC(z[0]));
            text += QString("Average J.n = %1 A/m^2\n").arg(fmtC(z[1]));
            break;
        case 2:
            text = QString("Contour length = %1 meters\n").arg(fmtD(z[0].re));
            text += QString("Surface area = %1 meter^2\n").arg(fmtD(z[1].re));
            break;
        case 3:
            text = QString("Average voltage = %1 V\n").arg(fmtC(z[0]));
            break;
        }
        out->setOutput(text);
        out->show();
    } else if (m_widget->mode() == 2) {
        // Block integrals
        bool blocksSelected = false;
        for (auto &lbl : m_doc->labellist)
            if (lbl.IsSelected) { blocksSelected = true; break; }
        // Henrotte force/torque acts on a conductor (mesh-node selection).
        bool nodesSelected = false;
        for (auto &mn : m_doc->meshnode)
            if (mn.IsSelected) { nodesSelected = true; break; }
        if (!blocksSelected && !nodesSelected) {
            m_widget->emitStatus("Select a block or a conductor for integration (Area mode)");
            return;
        }
        QStringList types;
        types << "Real Power" << "Reactive Power" << "Apparent Power"
              << "Time-Avg Stored Energy" << "Cross-section area" << "Volume"
              << "x/r Henrotte force (SS)" << "y/z Henrotte force (SS)"
              << "x/r Henrotte force (2x)" << "y/z Henrotte force (2x)"
              << "Henrotte torque (SS)" << "Henrotte torque (2x)";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Block Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        // Force/torque (>= 6) may use a conductor (mesh-node) selection; the
        // power/energy/geometry integrals need selected blocks.
        if (intType < 6 && !blocksSelected) {
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
        m_doc->progressCallback = [&progDlg, parent](int pct) {
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

        CComplex z = m_doc->BlockIntegral(intType);
        QString text;
        switch (intType) {
        case 0: text = QString("Real Power = %1 W\n").arg(fmtC(z)); break;
        case 1: text = QString("Reactive Power = %1 VAr\n").arg(fmtC(z)); break;
        case 2: text = QString("Apparent Power = %1 VA\n").arg(fmtC(z)); break;
        case 3: text = QString("Time-Avg Stored Energy = %1 J\n").arg(fmtD(z.re)); break;
        case 4: text = QString("Cross-section area = %1 meter^2\n").arg(fmtD(z.re)); break;
        case 5: text = QString("Volume = %1 meter^3\n").arg(fmtD(z.re)); break;
        case 6: case 8:
            text = QString("Force in x/r direction = %1 N\n").arg(fmtC(z)); break;
        case 7: case 9:
            text = QString("Force in y/z direction = %1 N\n").arg(fmtC(z)); break;
        case 10: case 11:
            text = QString("Torque = %1 N*m\n").arg(fmtC(z)); break;
        }
        m_doc->progressCallback = nullptr;
        progDlg.reset();
        if (m_doc->maskSelectionInvalid) {
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

void CurrentFlowHandler::showXYPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    if (m_doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first (use Contour mode)"); return; }
    // The list lives in libfemm so *_makeplot can label its exported
    // columns with the same names.
    const QStringList types = xyplot::quantityList(xyexport::currPlotNames());

    int plotType = 0, npts = 0;
    if (!xyplot::prompt(parent, types, 0, "co/plotPoints", plotType, npts)) return;

    std::vector<CurrResultsDoc::XYPlotPoint> pts;
    m_doc->GetLineValues(pts, plotType, npts);
    if (pts.empty()) { m_widget->emitStatus("No data for plot"); return; }
    xyplot::showAlongContour(parent, types[plotType], pts);
}

void CurrentFlowHandler::showCircuitDialog(QWidget *parent)
{
    if (!m_doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Conductor Properties"));
    auto *combo = new QComboBox;
    for (auto &cp : m_doc->circproplist) combo->addItem(QString::fromStdString(cp.CircName));
    auto *textEdit = new QTextEdit;
    textEdit->setReadOnly(true);
    textEdit->setFont(QFont("Consolas", 9));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::close);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(combo);
    layout->addWidget(textEdit);
    layout->addWidget(buttons);
    dlg.resize(400, 250);

    if (m_doc->circproplist.empty()) {
        textEdit->setPlainText("No conductors defined in this problem.");
    }
    auto fmtC = [](CComplex z) -> QString { return femmFmtComplex(z); };
    auto updateText = [&](int idx) {
        if (idx < 0 || idx >= (int)m_doc->circproplist.size()) return;
        CComplex V = m_doc->circproplist[idx].V;
        CComplex I = m_doc->circproplist[idx].q;
        QString text;
        text += QString("Voltage = %1 V\n").arg(fmtC(V));
        text += QString("Current = %1 A\n").arg(fmtC(I));
        if (abs(I) != 0)
            text += QString("Volt/Curr = %1 Ohms\n").arg(fmtC(V / I));
        if (abs(V) != 0)
            text += QString("Curr/Volt = %1 S\n").arg(fmtC(I / V));
        textEdit->setPlainText(text);
    };
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateText);
    updateText(0);
    dlg.exec();
}

void CurrentFlowHandler::showContourPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Contour Plot"));
    auto *form = new QFormLayout;
    auto *showV = new QCheckBox(tr("Show potential contours"));
    showV->setChecked(m_widget->showContours());
    form->addRow(showV);
    auto *numEdit = new PyLineEdit;
    numEdit->setText(QString::number(m_widget->numContours()));
    form->addRow(tr("Number of contours:"), numEdit);
    auto *loEdit = new PyLineEdit;
    loEdit->setText(QString::number(m_doc->A_Low, 'g', 10));
    form->addRow(tr("Lower bound:"), loEdit);
    auto *hiEdit = new PyLineEdit;
    hiEdit->setText(QString::number(m_doc->A_High, 'g', 10));
    form->addRow(tr("Upper bound:"), hiEdit);
    auto *showMaskCb = new QCheckBox(tr("Show stress tensor mask"));
    showMaskCb->setChecked(m_widget->m_showMask);
    form->addRow(showMaskCb);
    auto *dfltBtn = new QPushButton(tr("Defaults"));
    QObject::connect(dfltBtn, &QPushButton::clicked, [&]() {
        loEdit->setText(QString::number(m_doc->A_lb, 'g', 10));
        hiEdit->setText(QString::number(m_doc->A_ub, 'g', 10));
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
        m_doc->A_Low = loEdit->evaluateAsDouble(m_doc->A_lb);
        m_doc->A_High = hiEdit->evaluateAsDouble(m_doc->A_ub);
        m_widget->m_showMask = showMaskCb->isChecked();
        m_widget->update();
    }
}

void CurrentFlowHandler::showDensityPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    // PlotBounds layout: [0]=|V|, [1]=Re(V), [2]=Im(V), [3]=|J|, [4]=Re(J), [5]=Im(J),
    //                     [6]=|E|, [7]=Re(E), [8]=Im(E)
    // Combo order (0-based): 0=|V|, 1=Re(V), 2=Im(V), 3=|J|, 4=Re(J), 5=Im(J),
    //                        6=|E|, 7=Re(E), 8=Im(E), 9=|Jc|, 10=Re(Jc), 11=Im(Jc), 12=|Jd|
    // boundsMap: densityPlotType (1-based) -> PlotBounds index
    //   types 1-9 map directly to PB[0..8]; types 10-13 computed from existing bounds
    int boundsMap[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 3, 4, 5, 3};
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Density Plot"));
    auto *form = new QFormLayout;
    auto *showIt = new QCheckBox(tr("Show density plot"));
    showIt->setChecked(m_widget->showDensity() && m_widget->m_densityPlotType > 0);
    form->addRow(showIt);
    auto *typeCombo = new QComboBox;
    typeCombo->addItem(tr("|V| (voltage magnitude)"));
    typeCombo->addItem(tr("Re(V)"));
    typeCombo->addItem(tr("Im(V)"));
    typeCombo->addItem(tr("|J| (current density)"));
    typeCombo->addItem(tr("Re(J)"));
    typeCombo->addItem(tr("Im(J)"));
    typeCombo->addItem(tr("|E| (electric field)"));
    typeCombo->addItem(tr("Re(E)"));
    typeCombo->addItem(tr("Im(E)"));
    typeCombo->addItem(tr("|Jc| (conduction)"));
    typeCombo->addItem(tr("Re(Jc)"));
    typeCombo->addItem(tr("Im(Jc)"));
    typeCombo->addItem(tr("|Jd| (displacement)"));
    int curComboIdx = (m_widget->m_densityPlotType > 0) ? m_widget->m_densityPlotType - 1 : 0;
    if (curComboIdx >= typeCombo->count()) curComboIdx = 0;
    typeCombo->setCurrentIndex(curComboIdx);
    form->addRow(tr("Plot type:"), typeCombo);
    auto *loEdit = new PyLineEdit;
    auto *hiEdit = new PyLineEdit;
    form->addRow(tr("Lower bound:"), loEdit);
    form->addRow(tr("Upper bound:"), hiEdit);
    auto updateBounds = [&](int comboIdx) {
        int bi = boundsMap[comboIdx + 1];
        if (bi >= 0 && bi < 9) {
            loEdit->setText(QString::number(m_doc->PlotBounds[bi][0], 'g', 10));
            hiEdit->setText(QString::number(m_doc->PlotBounds[bi][1], 'g', 10));
        }
    };
    QObject::connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateBounds);
    updateBounds(curComboIdx);
    auto *showLegendCb = new QCheckBox(tr("Show legend")); showLegendCb->setChecked(m_widget->m_showLegend); form->addRow(showLegendCb);
    auto *greyScaleCb = new QCheckBox(tr("Greyscale")); greyScaleCb->setChecked(m_widget->m_greyScale); form->addRow(greyScaleCb);
    auto *dfltBtn = new QPushButton(tr("Defaults"));
    QObject::connect(dfltBtn, &QPushButton::clicked, [&]() {
        int bi = boundsMap[typeCombo->currentIndex() + 1];
        if (bi >= 0 && bi < 9) {
            loEdit->setText(QString::number(m_doc->d_PlotBounds[bi][0], 'g', 10));
            hiEdit->setText(QString::number(m_doc->d_PlotBounds[bi][1], 'g', 10));
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
        if (bi >= 0 && bi < 9) {
            m_doc->PlotBounds[bi][0] = loEdit->evaluateAsDouble(m_doc->d_PlotBounds[bi][0]);
            m_doc->PlotBounds[bi][1] = hiEdit->evaluateAsDouble(m_doc->d_PlotBounds[bi][1]);
        }
        m_widget->update();
    }
}

void CurrentFlowHandler::showVectorPlotDialog(QWidget *parent)
{
    QStringList types;
    types << "Off" << "J (current density)" << "E (electric field)";
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

void CurrentFlowHandler::showProblemInfo(QWidget *parent)
{
    if (!m_doc) return;
    showProblemInfoDialog(parent, m_doc->LengthUnits, m_doc->ProblemType,
                          m_doc->Depth, m_doc->Frequency,
                          (int)m_doc->cmeshnode.size(), (int)m_doc->cmeshelem.size());
}

#undef tr
