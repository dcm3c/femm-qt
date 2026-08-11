// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_editors.cpp — Individual property editor dialogs for magnetics
// Ported from femm42src/femm MatDlg.cpp, BdryDlg.cpp, CircProp.cpp, NodeProp.cpp

#include "prop_editors.h"
#include "bh_dialog.h"
#include "complex_fmt.h"
#include "py_line_edit.h"
#include "mag_types.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>

// Evaluate a PyLineEdit as CComplex
static CComplex evalComplex(const PyLineEdit *edit)
{
    double re, im;
    edit->evaluateAsComplex(re, im);
    return CComplex(re, im);
}



// ================================================================
// MaterialEditorDialog
// ================================================================

MaterialEditorDialog::MaterialEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Material Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Material");
    form->addRow(tr("Name:"), m_name);

    m_mu_x = new PyLineEdit; m_mu_x->setText("1");
    m_mu_y = new PyLineEdit; m_mu_y->setText("1");
    m_muLabel = new QLabel(QStringLiteral("Relative \u00b5:"));
    auto *muRow = new QHBoxLayout;
    muRow->addWidget(new QLabel("x:")); muRow->addWidget(m_mu_x);
    muRow->addWidget(new QLabel("y:")); muRow->addWidget(m_mu_y);
    form->addRow(m_muLabel, muRow);

    m_Hc = new PyLineEdit; m_Hc->setText("0");
    m_hcLabel = new QLabel(QStringLiteral("H\u1d04 (A/m):"));
    form->addRow(m_hcLabel, m_Hc);

    m_Jsrc = new PyLineEdit; m_Jsrc->setText("0");
    form->addRow(QStringLiteral("J (MA/m\u00b2):"), m_Jsrc);

    m_Cduct = new PyLineEdit; m_Cduct->setText("0");
    form->addRow(QStringLiteral("\u03c3 (MS/m):"), m_Cduct);

    m_lamType = new QComboBox;
    // The combo carries one more entry than the stored LamType: index 1
    // "Laminated in-plane" is LamType 0 with a lamination thickness / fill
    // factor (in-plane lamination is modeled via the fill factor on an
    // otherwise-solid material, not a distinct LamType). Mirrors Original FEMM's
    // MatDlg: stored LamType = comboIndex - 1, clamped at 0. See fromProp/toProp.
    m_lamType->addItem(tr("Not laminated"));          // 0 -> LamType 0
    m_lamType->addItem(tr("Laminated in-plane"));     // 1 -> LamType 0 (+ fill/thickness)
    m_lamType->addItem(tr("Laminated x-direction"));  // 2 -> LamType 1
    m_lamType->addItem(tr("Laminated y-direction"));  // 3 -> LamType 2
    m_lamType->addItem(tr("Magnet wire"));            // 4 -> LamType 3
    m_lamType->addItem(tr("Plain stranded wire"));    // 5 -> LamType 4
    m_lamType->addItem(tr("Litz wire"));              // 6 -> LamType 5
    m_lamType->addItem(tr("Square wire"));            // 7 -> LamType 6
    form->addRow(tr("Material Type:"), m_lamType);
    connect(m_lamType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MaterialEditorDialog::onLamTypeChanged);

    m_Lam_d = new PyLineEdit; m_Lam_d->setText("0");
    form->addRow(QStringLiteral("Lam. thickness (mm):"), m_Lam_d);

    m_LamFill = new PyLineEdit; m_LamFill->setText("1");
    form->addRow(tr("Fill factor:"), m_LamFill);

    // The hysteresis lag angle is phi everywhere else -- <Phi_h> in the file
    // format, phi_hmax / phi_hx / phi_hy in the manual and in mi_addmaterial --
    // and OF's dialog labelled all three with the Symbol font's phi. The C++
    // members keep OF's Theta_h* names. U+03C6 is the character OF used.
    m_Theta_hn = new PyLineEdit; m_Theta_hn->setText("0");
    form->addRow(QStringLiteral("\u03c6\u2095 (deg):"), m_Theta_hn);

    m_Theta_hx = new PyLineEdit; m_Theta_hx->setText("0");
    m_Theta_hy = new PyLineEdit; m_Theta_hy->setText("0");
    auto *thRow = new QHBoxLayout;
    thRow->addWidget(new QLabel("x:")); thRow->addWidget(m_Theta_hx);
    thRow->addWidget(new QLabel("y:")); thRow->addWidget(m_Theta_hy);
    form->addRow(QStringLiteral("\u03c6\u2095 x/y:"), thRow);

    m_NStrands = new PyLineEdit; m_NStrands->setText("0");
    m_WireD = new PyLineEdit; m_WireD->setText("0");
    m_wireLabel = new QLabel(tr("Wire:"));
    auto *wireRow = new QHBoxLayout;
    wireRow->addWidget(new QLabel("Strands:")); wireRow->addWidget(m_NStrands);
    wireRow->addWidget(new QLabel("Dia (mm):")); wireRow->addWidget(m_WireD);
    form->addRow(m_wireLabel, wireRow);

    m_nonlinear = new QCheckBox(tr("Nonlinear B-H curve"));
    form->addRow(m_nonlinear);
    connect(m_nonlinear, &QCheckBox::toggled, this, &MaterialEditorDialog::onNonlinearToggled);

    m_bhButton = new QPushButton(tr("Edit B-H Curve..."));
    m_bhButton->setEnabled(false);
    form->addRow(m_bhButton);
    connect(m_bhButton, &QPushButton::clicked, this, &MaterialEditorDialog::onEditBHCurve);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    onLamTypeChanged(0);
    onNonlinearToggled(false);
}

// Apply the field defaults and enable/disable rules that Original FEMM's
// CMatDlg imposes when the material type changes (MatDlg.cpp OnSelchangeLamDir
// and IsWire). idx is the combo index: 0 not-laminated, 1 in-plane, 2 x, 3 y,
// 4 magnet / 5 stranded / 6 litz / 7 square wire.
void MaterialEditorDialog::onLamTypeChanged(int idx)
{
    const bool isLaminated = (idx >= 1 && idx <= 3);   // in-plane, x, y
    const bool isWire = (idx >= 4);

    if (isWire) {
        // Magnet (4) and square (7) wire are a single strand; stranded (5) and
        // litz (6) let the user set the strand count.
        if (idx == 4 || idx == 7) {
            m_NStrands->setText("1");
            m_NStrands->setEnabled(false);
        } else {
            m_NStrands->setEnabled(true);
        }
        m_WireD->setEnabled(true);
        m_wireLabel->setEnabled(true);
        applyWireDefaults();
        return;
    }

    m_NStrands->setEnabled(false);
    m_WireD->setEnabled(false);
    m_wireLabel->setEnabled(false);
    m_Lam_d->setEnabled(isLaminated);
    m_LamFill->setEnabled(isLaminated);

    if (isLaminated) {
        // A lamination is soft-magnetic: it carries no coercivity.
        m_Hc->setText("0");
        m_Hc->setEnabled(false);
        m_hcLabel->setEnabled(false);
    } else {
        m_Hc->setEnabled(true);
        m_hcLabel->setEnabled(true);
    }
}

// OF CMatDlg::IsWire(): a wire's magnetic and source properties are fixed by its
// geometry, so reset them and lock the fields that don't apply. The wire's own
// parameters (strand count, diameter) are left untouched.
void MaterialEditorDialog::applyWireDefaults()
{
    m_mu_x->setText("1");
    m_mu_y->setText("1");
    m_Hc->setText("0");
    m_Jsrc->setText("0");
    m_Lam_d->setText("0");
    m_LamFill->setText("1");
    m_Theta_hn->setText("0");
    m_Theta_hx->setText("0");
    m_Theta_hy->setText("0");
    m_bhB.clear();
    m_bhH.clear();
    m_bhPoints = 0;
    m_nonlinear->setChecked(false);   // a wire cannot be nonlinear

    m_mu_x->setEnabled(true);
    m_mu_y->setEnabled(true);
    m_muLabel->setEnabled(true);
    m_Theta_hx->setEnabled(true);
    m_Theta_hy->setEnabled(true);
    m_Lam_d->setEnabled(false);
    m_LamFill->setEnabled(false);
    m_Hc->setEnabled(false);
    m_hcLabel->setEnabled(false);
    m_bhButton->setEnabled(false);
    m_Theta_hn->setEnabled(false);
}

// OF CMatDlg::OnSelchangeNlcombo(): the nonlinear (B-H curve) choice decides
// whether the linear permeability / directional hysteresis-angle fields are
// active, or the B-H curve editor and single hysteresis-lag angle instead.
void MaterialEditorDialog::onNonlinearToggled(bool on)
{
    m_mu_x->setEnabled(!on);
    m_mu_y->setEnabled(!on);
    m_muLabel->setEnabled(!on);
    m_Theta_hx->setEnabled(!on);
    m_Theta_hy->setEnabled(!on);
    m_bhButton->setEnabled(on);
    m_Theta_hn->setEnabled(on);

    // A wire can't carry a B-H curve; if one is selected when nonlinear is
    // turned on, revert the material type to "Not laminated".
    if (on && m_lamType->currentIndex() > 3)
        m_lamType->setCurrentIndex(0);   // fires onLamTypeChanged(0)
}

void MaterialEditorDialog::onEditBHCurve()
{
    BHDataDialog dlg(this);
    dlg.setMaterialName(m_name->text());
    if (m_bhPoints > 0) {
        std::vector<double> Bvals(m_bhPoints), Hvals(m_bhPoints);
        for (int i = 0; i < m_bhPoints; i++) {
            Bvals[i] = m_bhB[i];
            Hvals[i] = m_bhH[i];
        }
        dlg.setData(Bvals, Hvals, m_bhPoints);
    }
    if (dlg.exec() == QDialog::Accepted) {
        dlg.getData(m_bhB, m_bhH, m_bhPoints);
    }
}

void MaterialEditorDialog::fromProp(const FMaterialProp &m)
{
    m_name->setText(QString::fromStdString(m.BlockName));
    m_mu_x->setText(QString::number(m.mu_x, 'g', 15));
    m_mu_y->setText(QString::number(m.mu_y, 'g', 15));
    m_Hc->setText(QString::number(m.H_c, 'g', 15));
    m_Jsrc->setText(toQString(m.Jsrc));
    m_Cduct->setText(QString::number(m.Cduct, 'g', 15));
    // Stored LamType -> combo index (OF MatDlg::OnInitDialog): nonzero LamType
    // shifts up by one; LamType 0 carrying a lamination thickness or fill != 1
    // is the "Laminated in-plane" choice (combo index 1).
    int lamIdx = m.LamType;
    if (lamIdx != 0) lamIdx++;
    else if (m.Lam_d != 0 || m.LamFill != 1) lamIdx = 1;
    // Populate the combo and nonlinear check without firing their handlers
    // mid-load; the handlers run once, explicitly, after every field is in
    // place (mirrors OF OnInitDialog, where MFC's SetCurSel doesn't notify).
    m_lamType->blockSignals(true);
    m_nonlinear->blockSignals(true);
    m_lamType->setCurrentIndex(lamIdx);
    m_nonlinear->setChecked(m.BHpoints > 0);
    m_lamType->blockSignals(false);
    m_nonlinear->blockSignals(false);
    m_Lam_d->setText(QString::number(m.Lam_d, 'g', 15));
    m_LamFill->setText(QString::number(m.LamFill, 'g', 15));
    m_Theta_hn->setText(QString::number(m.Theta_hn, 'g', 15));
    m_Theta_hx->setText(QString::number(m.Theta_hx, 'g', 15));
    m_Theta_hy->setText(QString::number(m.Theta_hy, 'g', 15));
    m_NStrands->setText(QString::number(m.NStrands));
    m_WireD->setText(QString::number(m.WireD, 'g', 15));
    onLamTypeChanged(lamIdx);
    onNonlinearToggled(m.BHpoints > 0);

    // Load B-H data
    m_bhPoints = m.BHpoints;
    m_bhB.resize(m_bhPoints);
    m_bhH.resize(m_bhPoints);
    for (int i = 0; i < m_bhPoints; i++) {
        m_bhB[i] = m.BHdata[i].re;  // B is stored in .re
        m_bhH[i] = m.BHdata[i].im;  // H is stored in .im
    }
}

void MaterialEditorDialog::toProp(FMaterialProp &m) const
{
    m.BlockName = m_name->text().toStdString();
    m.mu_x = m_mu_x->evaluateAsDouble(1);
    m.mu_y = m_mu_y->evaluateAsDouble(1);
    m.H_c = m_Hc->evaluateAsDouble(0);
    m.Jsrc = evalComplex(m_Jsrc);
    m.Cduct = m_Cduct->evaluateAsDouble(0);
    // Combo index -> stored LamType (OF MatDlg::OnOK): LamType = index - 1,
    // clamped at 0, so both "Not laminated" (0) and "Laminated in-plane" (1)
    // store LamType 0 -- they differ only by the lamination thickness / fill.
    int lamIdx = m_lamType->currentIndex();
    m.LamType = lamIdx - 1;
    if (m.LamType < 0) m.LamType = 0;
    m.Lam_d = m_Lam_d->evaluateAsDouble(0);
    m.LamFill = m_LamFill->evaluateAsDouble(1);
    if (m.LamFill <= 0) m.LamFill = 1;
    if (m.LamFill > 1) m.LamFill = 1;
    if (lamIdx == 0) {   // "Not laminated": force truly solid so it doesn't
        m.Lam_d = 0;     // reload as "Laminated in-plane"
        m.LamFill = 1;
    }
    m.Theta_hn = m_Theta_hn->evaluateAsDouble(0);
    m.Theta_hx = m_Theta_hx->evaluateAsDouble(0);
    m.Theta_hy = m_Theta_hy->evaluateAsDouble(0);
    m.NStrands = (int)m_NStrands->evaluateAsDouble(0);
    m.WireD = m_WireD->evaluateAsDouble(0);

    // Zero-diameter wire guard (OF MatDlg::OnOK): a wire needs a positive strand
    // diameter; without one it isn't a valid wire, so fall back to a plain solid
    // material ("Not laminated"). lamIdx >= 4 is any of the wire choices.
    if (lamIdx >= 4 && m.WireD <= 0) {
        m.WireD = 0;
        m.LamType = 0;
    }

    // B-H curve data
    if (m_nonlinear->isChecked() && m_bhPoints > 0) {
        m.BHpoints = m_bhPoints;
        m.BHdata.resize(m_bhPoints);
        for (int i = 0; i < m_bhPoints; i++)
            m.BHdata[i] = CComplex(m_bhB[i], m_bhH[i]);
    }
    else {
        m.BHpoints = 0;
        m.BHdata.clear();
    }
}

void MaterialEditorDialog::setName(const QString &name) { m_name->setText(name); }
QString MaterialEditorDialog::name() const { return m_name->text(); }

// ================================================================
// BoundaryEditorDialog
// ================================================================

BoundaryEditorDialog::BoundaryEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Boundary Condition"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Boundary");
    form->addRow(tr("Name:"), m_name);

    m_type = new QComboBox;
    m_type->addItem(tr("Prescribed A"));           // 0
    m_type->addItem(tr("Small Skin Depth"));        // 1
    m_type->addItem(tr("Mixed"));                   // 2
    m_type->addItem(tr("Strategic Dual Image"));    // 3
    m_type->addItem(tr("Periodic"));                // 4
    m_type->addItem(tr("Anti-Periodic"));           // 5
    m_type->addItem(tr("Periodic Air Gap"));        // 6
    m_type->addItem(tr("Anti-Periodic Air Gap"));   // 7
    form->addRow(tr("Type:"), m_type);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BoundaryEditorDialog::onTypeChanged);

    // Prescribed A panel
    m_panelA = new QWidget;
    auto *aLayout = new QHBoxLayout(m_panelA);
    aLayout->setContentsMargins(0, 0, 0, 0);
    m_A0 = new PyLineEdit; m_A0->setText("0");
    m_A1 = new PyLineEdit; m_A1->setText("0");
    m_A2 = new PyLineEdit; m_A2->setText("0");
    m_phi = new PyLineEdit; m_phi->setText("0");
    aLayout->addWidget(new QLabel("A0:")); aLayout->addWidget(m_A0);
    aLayout->addWidget(new QLabel("A1:")); aLayout->addWidget(m_A1);
    aLayout->addWidget(new QLabel("A2:")); aLayout->addWidget(m_A2);
    aLayout->addWidget(new QLabel(QStringLiteral("\u03c6:"))); aLayout->addWidget(m_phi);
    form->addRow(tr("Prescribed A:"), m_panelA);

    // Small Skin Depth panel
    m_panelSSD = new QWidget;
    auto *ssdLayout = new QHBoxLayout(m_panelSSD);
    ssdLayout->setContentsMargins(0, 0, 0, 0);
    m_Mu = new PyLineEdit; m_Mu->setText("0");
    m_Sig = new PyLineEdit; m_Sig->setText("0");
    ssdLayout->addWidget(new QLabel(QStringLiteral("\u00b5:"))); ssdLayout->addWidget(m_Mu);
    ssdLayout->addWidget(new QLabel(QStringLiteral("\u03c3:"))); ssdLayout->addWidget(m_Sig);
    form->addRow(tr("Skin Depth:"), m_panelSSD);

    // Mixed BC panel
    m_panelMixed = new QWidget;
    auto *mixedLayout = new QHBoxLayout(m_panelMixed);
    mixedLayout->setContentsMargins(0, 0, 0, 0);
    m_c0 = new PyLineEdit; m_c0->setText("0");
    m_c1 = new PyLineEdit; m_c1->setText("0");
    mixedLayout->addWidget(new QLabel("c0:")); mixedLayout->addWidget(m_c0);
    mixedLayout->addWidget(new QLabel("c1:")); mixedLayout->addWidget(m_c1);
    form->addRow(tr("Mixed BC:"), m_panelMixed);

    // Air Gap panel
    m_panelAGE = new QWidget;
    auto *ageLayout = new QHBoxLayout(m_panelAGE);
    ageLayout->setContentsMargins(0, 0, 0, 0);
    m_innerAngle = new PyLineEdit; m_innerAngle->setText("0");
    m_outerAngle = new PyLineEdit; m_outerAngle->setText("0");
    ageLayout->addWidget(new QLabel("Inner:")); ageLayout->addWidget(m_innerAngle);
    ageLayout->addWidget(new QLabel("Outer:")); ageLayout->addWidget(m_outerAngle);
    form->addRow(tr("Air Gap:"), m_panelAGE);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    onTypeChanged(0);
}

void BoundaryEditorDialog::onTypeChanged(int idx)
{
    m_panelA->setVisible(idx == 0);
    m_panelSSD->setVisible(idx == 1);
    m_panelMixed->setVisible(idx == 2);
    m_panelAGE->setVisible(idx >= 6);

    // Resize dialog to fit visible content
    adjustSize();
}

void BoundaryEditorDialog::fromProp(const FBoundaryProp &b)
{
    m_name->setText(QString::fromStdString(b.BdryName));
    m_type->setCurrentIndex(b.BdryFormat);
    m_A0->setText(QString::number(b.A0, 'g', 15));
    m_A1->setText(QString::number(b.A1, 'g', 15));
    m_A2->setText(QString::number(b.A2, 'g', 15));
    m_phi->setText(QString::number(b.phi, 'g', 15));
    m_Mu->setText(QString::number(b.Mu, 'g', 15));
    m_Sig->setText(QString::number(b.Sig, 'g', 15));
    m_c0->setText(toQString(b.c0));
    m_c1->setText(toQString(b.c1));
    m_innerAngle->setText(QString::number(b.InnerAngle, 'g', 15));
    m_outerAngle->setText(QString::number(b.OuterAngle, 'g', 15));
}

void BoundaryEditorDialog::toProp(FBoundaryProp &b) const
{
    b.BdryName = m_name->text().toStdString();
    b.BdryFormat = m_type->currentIndex();
    b.A0 = m_A0->evaluateAsDouble(0);
    b.A1 = m_A1->evaluateAsDouble(0);
    b.A2 = m_A2->evaluateAsDouble(0);
    b.phi = m_phi->evaluateAsDouble(0);
    b.Mu = m_Mu->evaluateAsDouble(0);
    b.Sig = m_Sig->evaluateAsDouble(0);
    m_c0->evaluateAsComplex(b.c0.re, b.c0.im);
    m_c1->evaluateAsComplex(b.c1.re, b.c1.im);
    b.InnerAngle = m_innerAngle->evaluateAsDouble(0);
    b.OuterAngle = m_outerAngle->evaluateAsDouble(0);
}

void BoundaryEditorDialog::setName(const QString &name) { m_name->setText(name); }
QString BoundaryEditorDialog::name() const { return m_name->text(); }

// ================================================================
// CircuitEditorDialog
// ================================================================

CircuitEditorDialog::CircuitEditorDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Circuit Properties"));
    auto *form = new QFormLayout;

    m_name = new PyLineEdit; m_name->setText("New Circuit");
    form->addRow(tr("Name:"), m_name);

    m_amps = new PyLineEdit; m_amps->setText("0");
    form->addRow(tr("Current (A):"), m_amps);

    m_series = new QRadioButton(tr("Series"));
    m_parallel = new QRadioButton(tr("Parallel"));
    m_series->setChecked(true);
    auto *typeRow = new QHBoxLayout;
    typeRow->addWidget(m_series); typeRow->addWidget(m_parallel);
    form->addRow(tr("Type:"), typeRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void CircuitEditorDialog::fromProp(const FCircuit &c)
{
    m_name->setText(QString::fromStdString(c.CircName));
    m_amps->setText(toQString(c.Amps));
    if (c.CircType == 0) m_parallel->setChecked(true);
    else m_series->setChecked(true);
}

void CircuitEditorDialog::toProp(FCircuit &c) const
{
    c.CircName = m_name->text().toStdString();
    m_amps->evaluateAsComplex(c.Amps.re, c.Amps.im);
    c.CircType = m_series->isChecked() ? 1 : 0;
}

void CircuitEditorDialog::setName(const QString &name) { m_name->setText(name); }
QString CircuitEditorDialog::name() const { return m_name->text(); }

// ================================================================
// PointPropEditorDialog
// ================================================================

PointPropEditorDialog::PointPropEditorDialog(QWidget *parent)
    : PointPropEditorBase(tr("Specified Potential Property"),
                          tr("Point Current Property"),
                          tr("Specified Vector Potential, Wb/m"),
                          tr("Point Current, Amps"),
                          parent)
{
    m_Ap = new PyLineEdit; m_Ap->setText("0");
    groupALayout()->addRow(m_Ap);
    addGroupAField(m_Ap);

    m_Jp = new PyLineEdit; m_Jp->setText("0");
    groupBLayout()->addRow(m_Jp);
    addGroupBField(m_Jp);

    selectMode(true);
}

void PointPropEditorDialog::fromProp(const FPointProp &p)
{
    setName(QString::fromStdString(p.PointName));
    m_Ap->setText(toQString(p.Ap));
    m_Jp->setText(toQString(p.Jp));
    selectMode(p.Jp.re == 0 && p.Jp.im == 0);
}

void PointPropEditorDialog::toProp(FPointProp &p) const
{
    p.PointName = name().toStdString();
    m_Ap->evaluateAsComplex(p.Ap.re, p.Ap.im);
    m_Jp->evaluateAsComplex(p.Jp.re, p.Jp.im);
}
