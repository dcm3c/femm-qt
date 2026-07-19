// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_prop_editors.cpp — Property editor dialogs for current flow
// Ported from femm42src/femm cd_MatDlg, cd_BdryDlg, cd_CircProp, cd_PtProp

#include "curr_prop_editors.h"
#include "complex_fmt.h"
#include "py_line_edit.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>



// ================================================================
// Material editor: name, ox, oy, ex, ey, ltx, lty
// ================================================================

CurrMaterialEditorDialog::CurrMaterialEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Material Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Material");
    form->addRow(tr("Name:"), m_name);

    m_ox = new PyLineEdit; m_ox->setText("0");
    form->addRow(tr("Conductivity, \u03c3x (S/m):"), m_ox);

    m_oy = new PyLineEdit; m_oy->setText("0");
    form->addRow(tr("Conductivity, \u03c3y (S/m):"), m_oy);

    m_ex = new PyLineEdit; m_ex->setText("1");
    form->addRow(tr("Rel. Permittivity, \u03b5x:"), m_ex);

    m_ey = new PyLineEdit; m_ey->setText("1");
    form->addRow(tr("Rel. Permittivity, \u03b5y:"), m_ey);

    m_ltx = new PyLineEdit; m_ltx->setText("0");
    form->addRow(tr("Loss Tangent, tan \u03b4x:"), m_ltx);

    m_lty = new PyLineEdit; m_lty->setText("0");
    form->addRow(tr("Loss Tangent, tan \u03b4y:"), m_lty);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CurrMaterialEditorDialog::fromProp(const CurrMaterialProp &m)
{
    m_name->setText(QString::fromStdString(m.BlockName));
    m_ox->setText(QString::number(m.ox, 'g', 15));
    m_oy->setText(QString::number(m.oy, 'g', 15));
    m_ex->setText(QString::number(m.ex, 'g', 15));
    m_ey->setText(QString::number(m.ey, 'g', 15));
    m_ltx->setText(QString::number(m.ltx, 'g', 15));
    m_lty->setText(QString::number(m.lty, 'g', 15));
}

void CurrMaterialEditorDialog::toProp(CurrMaterialProp &m) const
{
    m.BlockName = m_name->text().toStdString();
    m.ox = m_ox->evaluateAsDouble(0);
    m.oy = m_oy->evaluateAsDouble(0);
    m.ex = m_ex->evaluateAsDouble(1);
    m.ey = m_ey->evaluateAsDouble(1);
    m.ltx = m_ltx->evaluateAsDouble(0);
    m.lty = m_lty->evaluateAsDouble(0);
}

// ================================================================
// Boundary editor: name, type (0-4), complex vs/qs/c0/c1
// ================================================================

CurrBoundaryEditorDialog::CurrBoundaryEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Boundary Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Boundary");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    m_type->addItem(tr("Fixed Voltage"));              // 0
    m_type->addItem(tr("Mixed BC"));                   // 1
    m_type->addItem(tr("Surface Current Density"));    // 2
    m_type->addItem(tr("Periodic"));                   // 3
    m_type->addItem(tr("Anti-Periodic"));              // 4
    form->addRow(tr("Type:"), m_type);

    m_lvsRe = new QLabel(tr("Voltage (Re):"));
    m_vsRe = new PyLineEdit; m_vsRe->setText("0");
    form->addRow(m_lvsRe, m_vsRe);

    m_lvsIm = new QLabel(tr("Voltage (Im):"));
    m_vsIm = new PyLineEdit; m_vsIm->setText("0");
    form->addRow(m_lvsIm, m_vsIm);

    m_lqsRe = new QLabel(tr("Current density (Re):"));
    m_qsRe = new PyLineEdit; m_qsRe->setText("0");
    form->addRow(m_lqsRe, m_qsRe);

    m_lqsIm = new QLabel(tr("Current density (Im):"));
    m_qsIm = new PyLineEdit; m_qsIm->setText("0");
    form->addRow(m_lqsIm, m_qsIm);

    m_lc0Re = new QLabel(tr("c0 (Re):"));
    m_c0Re = new PyLineEdit; m_c0Re->setText("0");
    form->addRow(m_lc0Re, m_c0Re);

    m_lc0Im = new QLabel(tr("c0 (Im):"));
    m_c0Im = new PyLineEdit; m_c0Im->setText("0");
    form->addRow(m_lc0Im, m_c0Im);

    m_lc1Re = new QLabel(tr("c1 (Re):"));
    m_c1Re = new PyLineEdit; m_c1Re->setText("0");
    form->addRow(m_lc1Re, m_c1Re);

    m_lc1Im = new QLabel(tr("c1 (Im):"));
    m_c1Im = new PyLineEdit; m_c1Im->setText("0");
    form->addRow(m_lc1Im, m_c1Im);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CurrBoundaryEditorDialog::onTypeChanged);
    onTypeChanged(0);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CurrBoundaryEditorDialog::onTypeChanged(int idx)
{
    // Fixed Voltage (0): vs only
    // Mixed BC (1): c0, c1 only
    // Surface Current Density (2): qs only
    // Periodic (3), Anti-Periodic (4): no extra fields

    bool showVs = (idx == 0);
    bool showQs = (idx == 2);
    bool showC  = (idx == 1);

    m_lvsRe->setVisible(showVs); m_vsRe->setVisible(showVs);
    m_lvsIm->setVisible(showVs); m_vsIm->setVisible(showVs);
    m_lqsRe->setVisible(showQs); m_qsRe->setVisible(showQs);
    m_lqsIm->setVisible(showQs); m_qsIm->setVisible(showQs);
    m_lc0Re->setVisible(showC);  m_c0Re->setVisible(showC);
    m_lc0Im->setVisible(showC);  m_c0Im->setVisible(showC);
    m_lc1Re->setVisible(showC);  m_c1Re->setVisible(showC);
    m_lc1Im->setVisible(showC);  m_c1Im->setVisible(showC);
}

void CurrBoundaryEditorDialog::fromProp(const CurrBoundaryProp &b)
{
    m_name->setText(QString::fromStdString(b.BdryName));
    m_type->setCurrentIndex(b.BdryFormat);
    m_vsRe->setText(QString::number(b.vs.re, 'g', 15));
    m_vsIm->setText(QString::number(b.vs.im, 'g', 15));
    m_qsRe->setText(QString::number(b.qs.re, 'g', 15));
    m_qsIm->setText(QString::number(b.qs.im, 'g', 15));
    m_c0Re->setText(QString::number(b.c0.re, 'g', 15));
    m_c0Im->setText(QString::number(b.c0.im, 'g', 15));
    m_c1Re->setText(QString::number(b.c1.re, 'g', 15));
    m_c1Im->setText(QString::number(b.c1.im, 'g', 15));
}

void CurrBoundaryEditorDialog::toProp(CurrBoundaryProp &b) const
{
    b.BdryName = m_name->text().toStdString();
    b.BdryFormat = m_type->currentIndex();
    b.vs.re = m_vsRe->evaluateAsDouble(0);
    b.vs.im = m_vsIm->evaluateAsDouble(0);
    b.qs.re = m_qsRe->evaluateAsDouble(0);
    b.qs.im = m_qsIm->evaluateAsDouble(0);
    b.c0.re = m_c0Re->evaluateAsDouble(0);
    b.c0.im = m_c0Im->evaluateAsDouble(0);
    b.c1.re = m_c1Re->evaluateAsDouble(0);
    b.c1.im = m_c1Im->evaluateAsDouble(0);
}

// ================================================================
// Conductor editor: name, complex vc/qc, CircType
// ================================================================

CurrConductorEditorDialog::CurrConductorEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Conductor Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Conductor");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    // Order must match the CircType stored in the .fec and used by csolv:
    //   CircType 0 -> prescribed current (qc),  1 -> prescribed voltage (vc).
    m_type->addItem(tr("Prescribed Current"));    // 0
    m_type->addItem(tr("Prescribed Voltage"));    // 1
    form->addRow(tr("Type:"), m_type);

    // Single complex-valued boxes (accept "re+im*I" / "re+imj"), matching OF's
    // one-box-per-quantity layout instead of separate Re/Im fields.
    m_vc = new PyLineEdit; m_vc->setText("0");
    form->addRow(tr("Voltage (V):"), m_vc);

    m_qc = new PyLineEdit; m_qc->setText("0");
    form->addRow(tr("Current (A):"), m_qc);

    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CurrConductorEditorDialog::onTypeChanged);
    onTypeChanged(m_type->currentIndex());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CurrConductorEditorDialog::onTypeChanged(int idx)
{
    // CircType 0 = prescribed current (qc), 1 = prescribed voltage (vc): only
    // the prescribed quantity's box is editable.
    bool volt = (idx == 1);
    m_vc->setEnabled(volt);
    m_qc->setEnabled(!volt);
}

void CurrConductorEditorDialog::fromProp(const CurrCircuit &c)
{
    m_name->setText(QString::fromStdString(c.CircName));
    m_type->setCurrentIndex(c.CircType);
    onTypeChanged(m_type->currentIndex());
    m_vc->setText(toQString(c.vc));
    m_qc->setText(toQString(c.qc));
}

void CurrConductorEditorDialog::toProp(CurrCircuit &c) const
{
    c.CircName = m_name->text().toStdString();
    c.CircType = m_type->currentIndex();
    m_vc->evaluateAsComplex(c.vc.re, c.vc.im);
    m_qc->evaluateAsComplex(c.qc.re, c.qc.im);
}

// ================================================================
// Point property editor: name, complex vp/qp
// ================================================================

CurrPointPropEditorDialog::CurrPointPropEditorDialog(QWidget *parent)
    : PointPropEditorBase(tr("Specified Voltage Property"),
                          tr("Point Current Density Property"),
                          tr("Specified Voltage"),
                          tr("Current Density, A/m"),
                          parent)
{
    m_vp = new PyLineEdit; m_vp->setText("0");
    groupALayout()->addRow(m_vp);
    addGroupAField(m_vp);

    m_qp = new PyLineEdit; m_qp->setText("0");
    groupBLayout()->addRow(m_qp);
    addGroupBField(m_qp);

    selectMode(true);
}

void CurrPointPropEditorDialog::fromProp(const CurrPointProp &p)
{
    setName(QString::fromStdString(p.PointName));
    m_vp->setText(toQString(p.vp));
    m_qp->setText(toQString(p.qp));
    selectMode(p.qp.re == 0 && p.qp.im == 0);
}

void CurrPointPropEditorDialog::toProp(CurrPointProp &p) const
{
    p.PointName = name().toStdString();
    m_vp->evaluateAsComplex(p.vp.re, p.vp.im);
    m_qp->evaluateAsComplex(p.qp.re, p.qp.im);
}
