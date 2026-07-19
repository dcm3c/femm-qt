// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_prop_editors.cpp — Property editor dialogs for electrostatics
// Ported from femm42src/femm bd_MatDlg, bd_BdryDlg, bd_CircProp, bd_PtProp

#include "elec_prop_editors.h"
#include "py_line_edit.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>

// ================================================================
// Material editor: name, ex, ey, qv
// ================================================================

ElecMaterialEditorDialog::ElecMaterialEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Material Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Material");
    form->addRow(tr("Name:"), m_name);

    m_ex = new PyLineEdit; m_ex->setText("1");
    form->addRow(tr("Relative permittivity, x:"), m_ex);

    m_ey = new PyLineEdit; m_ey->setText("1");
    form->addRow(tr("Relative permittivity, y:"), m_ey);

    m_qv = new PyLineEdit; m_qv->setText("0");
    form->addRow(tr("Charge density (C/m^3):"), m_qv);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ElecMaterialEditorDialog::fromProp(const EMaterialProp &m)
{
    m_name->setText(QString::fromStdString(m.BlockName));
    m_ex->setText(QString::number(m.ex, 'g', 15));
    m_ey->setText(QString::number(m.ey, 'g', 15));
    m_qv->setText(QString::number(m.qv, 'g', 15));
}

void ElecMaterialEditorDialog::toProp(EMaterialProp &m) const
{
    m.BlockName = m_name->text().toStdString();
    m.ex = m_ex->evaluateAsDouble(1);
    m.ey = m_ey->evaluateAsDouble(1);
    m.qv = m_qv->evaluateAsDouble(0);
}

// ================================================================
// Boundary editor: name, type (5 types), V, qs, c0, c1
// ================================================================

ElecBoundaryEditorDialog::ElecBoundaryEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Boundary Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Boundary");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    m_type->addItem(tr("Fixed Voltage"));          // 0
    m_type->addItem(tr("Mixed"));                   // 1
    m_type->addItem(tr("Surface Charge Density"));  // 2
    m_type->addItem(tr("Periodic"));                // 3
    m_type->addItem(tr("Anti-Periodic"));           // 4
    form->addRow(tr("Type:"), m_type);

    m_lV = new QLabel(tr("Fixed Voltage (V):"));
    m_V = new PyLineEdit; m_V->setText("0");
    form->addRow(m_lV, m_V);

    m_lqs = new QLabel(tr("Surface charge (C/m^2):"));
    m_qs = new PyLineEdit; m_qs->setText("0");
    form->addRow(m_lqs, m_qs);

    m_lc0 = new QLabel(tr("c0 coefficient:"));
    m_c0 = new PyLineEdit; m_c0->setText("0");
    form->addRow(m_lc0, m_c0);

    m_lc1 = new QLabel(tr("c1 coefficient:"));
    m_c1 = new PyLineEdit; m_c1->setText("0");
    form->addRow(m_lc1, m_c1);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ElecBoundaryEditorDialog::onTypeChanged);
    onTypeChanged(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ElecBoundaryEditorDialog::onTypeChanged(int idx)
{
    // Show/hide fields based on boundary type
    bool showV = (idx == 0);
    bool showMixed = (idx == 1);
    bool showQs = (idx == 2);

    m_lV->setVisible(showV); m_V->setVisible(showV);
    m_lqs->setVisible(showQs); m_qs->setVisible(showQs);
    m_lc0->setVisible(showMixed); m_c0->setVisible(showMixed);
    m_lc1->setVisible(showMixed); m_c1->setVisible(showMixed);
}

void ElecBoundaryEditorDialog::fromProp(const EBoundaryProp &b)
{
    m_name->setText(QString::fromStdString(b.BdryName));
    m_type->setCurrentIndex(b.BdryFormat);
    m_V->setText(QString::number(b.V, 'g', 15));
    m_qs->setText(QString::number(b.qs, 'g', 15));
    m_c0->setText(QString::number(b.c0, 'g', 15));
    m_c1->setText(QString::number(b.c1, 'g', 15));
}

void ElecBoundaryEditorDialog::toProp(EBoundaryProp &b) const
{
    b.BdryName = m_name->text().toStdString();
    b.BdryFormat = m_type->currentIndex();
    b.V = m_V->evaluateAsDouble(0);
    b.qs = m_qs->evaluateAsDouble(0);
    b.c0 = m_c0->evaluateAsDouble(0);
    b.c1 = m_c1->evaluateAsDouble(0);
}

// ================================================================
// Conductor editor: name, V, q, type
// ================================================================

ElecConductorEditorDialog::ElecConductorEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Conductor Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Conductor");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    m_type->addItem(tr("Prescribed charge"));    // 0
    m_type->addItem(tr("Prescribed voltage"));   // 1
    form->addRow(tr("Type:"), m_type);

    m_V = new PyLineEdit; m_V->setText("0");
    form->addRow(tr("Voltage (V):"), m_V);

    m_q = new PyLineEdit; m_q->setText("0");
    form->addRow(tr("Charge (C):"), m_q);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ElecConductorEditorDialog::onTypeChanged);
    onTypeChanged(m_type->currentIndex());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ElecConductorEditorDialog::onTypeChanged(int idx)
{
    // CircType 0 = prescribed charge (q), 1 = prescribed voltage (V): only the
    // prescribed quantity's box is editable (matches OF's bd_CircProp).
    m_q->setEnabled(idx == 0);
    m_V->setEnabled(idx == 1);
}

void ElecConductorEditorDialog::fromProp(const ECircuit &c)
{
    m_name->setText(QString::fromStdString(c.CircName));
    m_type->setCurrentIndex(c.CircType);
    onTypeChanged(m_type->currentIndex());
    m_V->setText(QString::number(c.V, 'g', 15));
    m_q->setText(QString::number(c.q, 'g', 15));
}

void ElecConductorEditorDialog::toProp(ECircuit &c) const
{
    c.CircName = m_name->text().toStdString();
    c.CircType = m_type->currentIndex();
    c.V = m_V->evaluateAsDouble(0);
    c.q = m_q->evaluateAsDouble(0);
}

// ================================================================
// Point property editor: name, V, qp
// ================================================================

ElecPointPropEditorDialog::ElecPointPropEditorDialog(QWidget *parent)
    : PointPropEditorBase(tr("Specified Potential Property"),
                          tr("Point Charge Density Property"),
                          tr("Specified Potential"),
                          tr("Point Charge Density, C/m"),
                          parent)
{
    m_V = new PyLineEdit; m_V->setText("0");
    groupALayout()->addRow(m_V);
    addGroupAField(m_V);

    m_qp = new PyLineEdit; m_qp->setText("0");
    groupBLayout()->addRow(m_qp);
    addGroupBField(m_qp);

    selectMode(true);
}

void ElecPointPropEditorDialog::fromProp(const EPointProp &p)
{
    setName(QString::fromStdString(p.PointName));
    m_V->setText(QString::number(p.V, 'g', 15));
    m_qp->setText(QString::number(p.qp, 'g', 15));
    selectMode(p.qp == 0);
}

void ElecPointPropEditorDialog::toProp(EPointProp &p) const
{
    p.PointName = name().toStdString();
    p.V = m_V->evaluateAsDouble(0);
    p.qp = m_qp->evaluateAsDouble(0);
}
