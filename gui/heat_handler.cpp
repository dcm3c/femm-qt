// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_handler.cpp — Heat flow postprocessor dialogs
#include "heat_handler.h"
#include "scalar_results_widget.h"
#include "heat_results_doc.h"
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
#include <QSettings>

#define tr QObject::tr

HeatFlowHandler::HeatFlowHandler(ScalarResultsWidget *widget, HeatResultsDoc *doc)
    : m_widget(widget), m_doc(doc)
{
}

void HeatFlowHandler::showPrefsDialog(QWidget *parent)
{
    ScalarPrefsDialog dlg(parent, 2);
    dlg.exec();
}

void HeatFlowHandler::showIntegrateDialog(QWidget *parent, OutputDialog *out)
{
    if (!m_doc) return;
    auto fmtD = [](double v) -> QString { return QString::number(v, 'g', 8); };
    bool isAxi = (m_doc->ProblemType == 1);

    if (m_widget->mode() == 1) {
        // Line integrals
        if (m_doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first"); return; }
        QStringList types;
        types << "G.t (Temperature gradient)" << "F.n (Heat flux)"
              << "Contour length" << "Average temperature";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Line Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        double z[4] = {};
        m_doc->LineIntegral(intType, z);
        QString text;
        switch (intType) {
        case 0: text = QString("Temperature drop = %1 K\n").arg(fmtD(z[0])); break;
        case 1: text = QString("Total heat flux = %1 W/m\nAverage F.n = %2 W/m^2\n").arg(fmtD(z[0])).arg(fmtD(z[1])); break;
        case 2: text = QString("Contour length = %1 meters\n").arg(fmtD(z[0])); text += QString("Surface area = %1 meter^2\n").arg(fmtD(z[1])); break;
        case 3: text = QString("Average temperature = %1 K\n").arg(fmtD(z[0])); break;
        }
        out->setOutput(text);
        out->show();
    } else if (m_widget->mode() == 2) {
        // Block integrals
        bool blocksSelected = false;
        for (auto &lbl : m_doc->labellist)
            if (lbl.IsSelected) { blocksSelected = true; break; }
        if (!blocksSelected) { m_widget->emitStatus("Select blocks for integration (use Area mode)"); return; }
        QStringList types;
        types << "Average temperature" << "Cross-section area" << "Volume"
              << "Average F (heat flux)" << "Average G (gradient)";
        bool ok;
        QString choice = QInputDialog::getItem(parent, tr("Block Integral"),
            tr("Integral type:"), types, 0, false, &ok);
        if (!ok) return;
        int intType = types.indexOf(choice);
        CComplex z = m_doc->BlockIntegral(intType);
        QString text;
        switch (intType) {
        case 0: text = QString("Average temperature = %1 K\n").arg(fmtD(z.re)); break;
        case 1: text = QString("%1 meter^2\n").arg(fmtD(z.re)); break;
        case 2: text = QString("%1 meter^3\n").arg(fmtD(z.re)); break;
        case 3: text = QString("Average Fx = %1 W/m^2\nAverage Fy = %2 W/m^2\n").arg(fmtD(z.re)).arg(fmtD(z.im)); break;
        case 4: text = QString("Average Gx = %1 K/m\nAverage Gy = %2 K/m\n").arg(fmtD(z.re)).arg(fmtD(z.im)); break;
        }
        out->setOutput(text);
        out->show();
    } else {
        m_widget->emitStatus("Select Contour or Area mode first");
    }
}

void HeatFlowHandler::showXYPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    if (m_doc->contour.size() < 2) { m_widget->emitStatus("Define a contour path first (use Contour mode)"); return; }
    QStringList types;
    types << "Temperature" << "|F| (flux magnitude)" << "F.n" << "F.t"
          << "|G| (gradient)" << "G.n" << "G.t";
    bool ok;
    QString choice = QInputDialog::getItem(parent, tr("X-Y Plot"), tr("Plot type:"), types, 0, false, &ok);
    if (!ok) return;
    int plotType = types.indexOf(choice);
    std::vector<HeatResultsDoc::XYPlotPoint> pts;
    int npts = QSettings().value("ho/plotPoints", 1500).toInt();
    m_doc->GetLineValues(pts, plotType, npts);
    if (pts.empty()) { m_widget->emitStatus("No data for plot"); return; }
    auto *pdlg = new QDialog(parent);
    pdlg->setAttribute(Qt::WA_DeleteOnClose);
    pdlg->setWindowTitle(choice);
    pdlg->resize(600, 400);
    auto *plot = new PlotWidget;
    auto *lay = new QVBoxLayout(pdlg);
    lay->addWidget(plot);
    PlotSeries series;
    series.label = choice;
    for (auto &pt : pts) { series.x.push_back(pt.x); series.y.push_back(pt.vals.empty() ? 0 : pt.vals[0]); }
    plot->addSeries(series);
    plot->setAxisLabels(tr("Position"), choice);
    pdlg->show();
}

void HeatFlowHandler::showCircuitDialog(QWidget *parent)
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
    dlg.resize(350, 200);
    if (m_doc->circproplist.empty()) {
        textEdit->setPlainText("No conductors defined in this problem.");
    }
    auto updateText = [&](int idx) {
        if (idx < 0 || idx >= (int)m_doc->circproplist.size()) return;
        QString text;
        text += QString("Temperature = %1 K\n").arg(m_doc->circproplist[idx].V, 0, 'g', 8);
        text += QString("Heat Flux = %1 W\n").arg(m_doc->circproplist[idx].q, 0, 'g', 8);
        textEdit->setPlainText(text);
    };
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateText);
    updateText(0);
    dlg.exec();
}

void HeatFlowHandler::showContourPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Contour Plot"));
    auto *form = new QFormLayout;
    auto *showT = new QCheckBox(tr("Show temperature contours"));
    showT->setChecked(m_widget->showContours());
    form->addRow(showT);
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
        m_widget->setShowContours(showT->isChecked());
        m_widget->setNumContours((int)numEdit->evaluateAsDouble(19));
        m_doc->A_Low = loEdit->evaluateAsDouble(m_doc->A_lb);
        m_doc->A_High = hiEdit->evaluateAsDouble(m_doc->A_ub);
        m_widget->m_showMask = showMaskCb->isChecked();
        m_widget->update();
    }
}

void HeatFlowHandler::showDensityPlotDialog(QWidget *parent)
{
    if (!m_doc) return;
    // PlotBounds layout: [0]=T, [1]=|F|, [2]=|G|
    // Combo order: 0=T, 1=|F|, 2=|G|
    // boundsMap: densityPlotType (1-based) -> PlotBounds index
    //   type 1=T->PB[0], type 2=|F|->PB[1], type 3=|G|->PB[2]
    int boundsMap[] = {-1, 0, 1, 2};
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Density Plot"));
    auto *form = new QFormLayout;
    auto *showIt = new QCheckBox(tr("Show density plot"));
    showIt->setChecked(m_widget->showDensity() && m_widget->m_densityPlotType > 0);
    form->addRow(showIt);
    auto *typeCombo = new QComboBox;
    typeCombo->addItem(tr("T (temperature)"));
    typeCombo->addItem(tr("|F| (heat flux)"));
    typeCombo->addItem(tr("|G| (gradient)"));
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
        if (bi >= 0 && bi < 4) {
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
        if (bi >= 0 && bi < 4) {
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
        if (bi >= 0 && bi < 4) {
            m_doc->PlotBounds[bi][0] = loEdit->evaluateAsDouble(m_doc->d_PlotBounds[bi][0]);
            m_doc->PlotBounds[bi][1] = hiEdit->evaluateAsDouble(m_doc->d_PlotBounds[bi][1]);
        }
        m_widget->update();
    }
}

void HeatFlowHandler::showVectorPlotDialog(QWidget *parent)
{
    QStringList types;
    types << "Off" << "F (heat flux)" << "G (gradient)";
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

void HeatFlowHandler::showProblemInfo(QWidget *parent)
{
    if (!m_doc) return;
    showProblemInfoDialog(parent, m_doc->LengthUnits, m_doc->ProblemType,
                          m_doc->Depth, -1,
                          (int)m_doc->meshnode.size(), (int)m_doc->meshelem.size());
}

#undef tr
