// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_prop_editors.cpp — Property editor dialogs for heat flow
// Ported from femm42src/femm hd_MatDlg, hd_BdryDlg, hd_CircProp, hd_PtProp

#include "heat_prop_editors.h"
#include "py_line_edit.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>

// ================================================================
// Material editor: name, Kx, Ky, qv, Kt
// ================================================================

HeatMaterialEditorDialog::HeatMaterialEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Material Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Material");
    form->addRow(tr("Name:"), m_name);

    m_Kx = new PyLineEdit; m_Kx->setText("1");
    form->addRow(tr("Thermal conductivity, x (W/m-K):"), m_Kx);

    m_Ky = new PyLineEdit; m_Ky->setText("1");
    form->addRow(tr("Thermal conductivity, y (W/m-K):"), m_Ky);

    m_qv = new PyLineEdit; m_qv->setText("0");
    form->addRow(tr("Volume heat generation (W/m^3):"), m_qv);

    m_Kt = new PyLineEdit; m_Kt->setText("0");
    form->addRow(tr("Volumetric heat capacity (MJ/(m^3*K)):"), m_Kt);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void HeatMaterialEditorDialog::fromProp(const HMaterialProp &m)
{
    m_name->setText(QString::fromStdString(m.BlockName));
    m_Kx->setText(QString::number(m.Kx, 'g', 15));
    m_Ky->setText(QString::number(m.Ky, 'g', 15));
    m_qv->setText(QString::number(m.qv, 'g', 15));
    m_Kt->setText(QString::number(m.Kt, 'g', 15));
}

void HeatMaterialEditorDialog::toProp(HMaterialProp &m) const
{
    m.BlockName = m_name->text().toStdString();
    m.Kx = m_Kx->evaluateAsDouble(1);
    m.Ky = m_Ky->evaluateAsDouble(1);
    m.qv = m_qv->evaluateAsDouble(0);
    m.Kt = m_Kt->evaluateAsDouble(0);
}

// ================================================================
// Boundary editor: name, type (6 types), Tset, qs, Tinf, h, beta, TinfRad
// ================================================================

HeatBoundaryEditorDialog::HeatBoundaryEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Boundary Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Boundary");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    m_type->addItem(tr("Fixed Temperature"));      // 0
    m_type->addItem(tr("Heat Flux"));               // 1
    m_type->addItem(tr("Convection"));              // 2
    m_type->addItem(tr("Radiation"));               // 3
    m_type->addItem(tr("Periodic"));                // 4
    m_type->addItem(tr("Anti-Periodic"));           // 5
    form->addRow(tr("Type:"), m_type);

    m_lTset = new QLabel(tr("Fixed temperature (K):"));
    m_Tset = new PyLineEdit; m_Tset->setText("0");
    form->addRow(m_lTset, m_Tset);

    m_lqs = new QLabel(tr("Heat flux density (W/m^2):"));
    m_qs = new PyLineEdit; m_qs->setText("0");
    form->addRow(m_lqs, m_qs);

    m_lTinf = new QLabel(tr("External temperature (K):"));
    m_Tinf = new PyLineEdit; m_Tinf->setText("0");
    form->addRow(m_lTinf, m_Tinf);

    m_lh = new QLabel(tr("Heat transfer coeff (W/(m^2*K)):"));
    m_h = new PyLineEdit; m_h->setText("0");
    form->addRow(m_lh, m_h);

    m_lbeta = new QLabel(tr("Emissivity:"));
    m_beta = new PyLineEdit; m_beta->setText("0");
    form->addRow(m_lbeta, m_beta);

    m_lTinfRad = new QLabel(tr("Radiation temperature (K):"));
    m_TinfRad = new PyLineEdit; m_TinfRad->setText("0");
    form->addRow(m_lTinfRad, m_TinfRad);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HeatBoundaryEditorDialog::onTypeChanged);
    onTypeChanged(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void HeatBoundaryEditorDialog::onTypeChanged(int idx)
{
    // Fixed Temperature (0): Tset only
    // Heat Flux (1): qs only
    // Convection (2): qs, Tinf, h, beta, TinfRad (mixed BC — all visible)
    // Radiation (3): beta, TinfRad
    // Periodic (4), Anti-Periodic (5): no extra fields

    bool showTset    = (idx == 0);
    bool showQs      = (idx == 1 || idx == 2);
    bool showTinf    = (idx == 2);
    bool showH       = (idx == 2);
    bool showBeta    = (idx == 2 || idx == 3);
    bool showTinfRad = (idx == 2 || idx == 3);

    m_lTset->setVisible(showTset);       m_Tset->setVisible(showTset);
    m_lqs->setVisible(showQs);           m_qs->setVisible(showQs);
    m_lTinf->setVisible(showTinf);       m_Tinf->setVisible(showTinf);
    m_lh->setVisible(showH);             m_h->setVisible(showH);
    m_lbeta->setVisible(showBeta);       m_beta->setVisible(showBeta);
    m_lTinfRad->setVisible(showTinfRad); m_TinfRad->setVisible(showTinfRad);
}

void HeatBoundaryEditorDialog::fromProp(const HBoundaryProp &b)
{
    m_name->setText(QString::fromStdString(b.BdryName));
    m_type->setCurrentIndex(b.BdryFormat);
    m_Tset->setText(QString::number(b.Tset, 'g', 15));
    m_qs->setText(QString::number(b.qs, 'g', 15));
    m_Tinf->setText(QString::number(b.Tinf, 'g', 15));
    m_h->setText(QString::number(b.h, 'g', 15));
    m_beta->setText(QString::number(b.beta, 'g', 15));
    m_TinfRad->setText(QString::number(b.TinfRad, 'g', 15));
}

void HeatBoundaryEditorDialog::toProp(HBoundaryProp &b) const
{
    b.BdryName = m_name->text().toStdString();
    b.BdryFormat = m_type->currentIndex();
    b.Tset = m_Tset->evaluateAsDouble(0);
    b.qs = m_qs->evaluateAsDouble(0);
    b.Tinf = m_Tinf->evaluateAsDouble(0);
    b.h = m_h->evaluateAsDouble(0);
    b.beta = m_beta->evaluateAsDouble(0);
    b.TinfRad = m_TinfRad->evaluateAsDouble(0);
}

// ================================================================
// Conductor editor: name, T, q, type
// ================================================================

HeatConductorEditorDialog::HeatConductorEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Conductor Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Conductor");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    // Order must match the CircType stored in the .feh and used by hsolv:
    //   CircType 0 -> prescribed heat flux (q),  1 -> prescribed temperature (T).
    m_type->addItem(tr("Prescribed heat flux"));     // 0
    m_type->addItem(tr("Prescribed temperature"));   // 1
    form->addRow(tr("Type:"), m_type);

    m_T = new PyLineEdit; m_T->setText("0");
    form->addRow(tr("Temperature (K):"), m_T);

    m_q = new PyLineEdit; m_q->setText("0");
    form->addRow(tr("Heat flux (W/m):"), m_q);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &HeatConductorEditorDialog::onTypeChanged);
    onTypeChanged(m_type->currentIndex());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void HeatConductorEditorDialog::onTypeChanged(int idx)
{
    // Match OF hd_CircProp: a heat-flux conductor (type 0) enables the flux box
    // and disables temperature; a fixed-temperature conductor (type 1) does the
    // reverse -- only the field the conductor actually prescribes is editable.
    m_q->setEnabled(idx == 0);
    m_T->setEnabled(idx == 1);
}

void HeatConductorEditorDialog::fromProp(const HCircuit &c)
{
    m_name->setText(QString::fromStdString(c.CircName));
    m_type->setCurrentIndex(c.CircType);
    onTypeChanged(m_type->currentIndex());
    m_T->setText(QString::number(c.T, 'g', 15));
    m_q->setText(QString::number(c.q, 'g', 15));
}

void HeatConductorEditorDialog::toProp(HCircuit &c) const
{
    c.CircName = m_name->text().toStdString();
    c.CircType = m_type->currentIndex();
    c.T = m_T->evaluateAsDouble(0);
    c.q = m_q->evaluateAsDouble(0);
}

// ================================================================
// Point property editor: name, T, qp
// ================================================================

HeatPointPropEditorDialog::HeatPointPropEditorDialog(QWidget *parent)
    : PointPropEditorBase(tr("Specified Temperature Property"),
                          tr("Point Heat Generation Property"),
                          tr("Specified Temperature"),
                          tr("Heat Generation, W/m"),
                          parent)
{
    m_T = new PyLineEdit; m_T->setText("0");
    groupALayout()->addRow(m_T);
    addGroupAField(m_T);

    m_qp = new PyLineEdit; m_qp->setText("0");
    groupBLayout()->addRow(m_qp);
    addGroupBField(m_qp);

    selectMode(true);
}

void HeatPointPropEditorDialog::fromProp(const HPointProp &p)
{
    setName(QString::fromStdString(p.PointName));
    m_T->setText(QString::number(p.T, 'g', 15));
    m_qp->setText(QString::number(p.qp, 'g', 15));
    selectMode(p.qp == 0);
}

void HeatPointPropEditorDialog::toProp(HPointProp &p) const
{
    p.PointName = name().toStdString();
    p.T = m_T->evaluateAsDouble(0);
    p.qp = m_qp->evaluateAsDouble(0);
}
