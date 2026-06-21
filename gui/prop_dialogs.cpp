// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_dialogs.cpp — Property dialogs for FEMM geometry entities
// Ported from femm42src/femm OpBlkDlg.cpp, OpSegDlg.cpp, etc.
// All numeric fields use PyLineEdit for inline expression evaluation.

#include "prop_dialogs.h"
#include "dialog_util.h"
#include "py_line_edit.h"
#include "document.h"
#include "mag_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QString>

// ================================================================
// GroupDialog
// ================================================================

GroupDialog::GroupDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Group"));
    auto *form = new QFormLayout;
    m_group = new PyLineEdit;
    m_group->setText("0");
    form->addRow(tr("In Group:"), m_group);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

int GroupDialog::group() const { return (int)m_group->evaluateAsDouble(0); }
void GroupDialog::setGroup(int g) { m_group->setText(QString::number(g)); }

// ================================================================
// NodePropDialog
// ================================================================

NodePropDialog::NodePropDialog(DocumentBase *doc, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Node Properties"));
    auto *form = new QFormLayout;

    m_combo = new QComboBox;
    m_combo->addItem("<None>");
    for (int i = 0; i < doc->numPointProps(); i++)
        m_combo->addItem(QString::fromStdString(doc->pointPropName(i)));
    form->addRow(tr("Boundary:"), m_combo);

    // Conductor combo (electrostatics/heat/current only)
    if (doc->physicsType() != 0) {
        m_condCombo = new QComboBox;
        m_condCombo->addItem("<None>");
        for (int i = 0; i < doc->numCircuits(); i++)
            m_condCombo->addItem(QString::fromStdString(doc->circuitName(i)));
        form->addRow(tr("In Conductor:"), m_condCombo);
    }

    m_group = new PyLineEdit;
    m_group->setText("0");
    form->addRow(tr("In Group:"), m_group);

    m_autoMesh = new QCheckBox(tr("Auto mesh"));
    m_autoMesh->setChecked(true);
    form->addRow(m_autoMesh);
    connect(m_autoMesh, &QCheckBox::toggled, this, &NodePropDialog::onAutoMeshToggled);

    m_lfs = new PyLineEdit;
    m_lfs->setText("0");
    m_lfs->setEnabled(false);
    form->addRow(tr("Local mesh size:"), m_lfs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    ensureTitleVisible(this);
}

void NodePropDialog::onAutoMeshToggled(bool on)
{
    m_lfs->setEnabled(!on);
    if (on) m_lfs->setText("0");
}

int NodePropDialog::group() const { return (int)m_group->evaluateAsDouble(0); }

double NodePropDialog::lfs() const
{
    if (m_autoMesh->isChecked()) return -1;
    double v = m_lfs->evaluateAsDouble(0);
    return (v > 0) ? v : -1;
}

void NodePropDialog::setLfs(double v)
{
    if (v > 0) {
        m_autoMesh->setChecked(false);
        m_lfs->setText(QString::number(v, 'g', 10));
    } else {
        m_autoMesh->setChecked(true);
    }
}
void NodePropDialog::setGroup(int g) { m_group->setText(QString::number(g)); }

// ================================================================
// SegmentPropDialog
// ================================================================

SegmentPropDialog::SegmentPropDialog(DocumentBase *doc, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Segment Properties"));
    auto *form = new QFormLayout;

    m_combo = new QComboBox;
    m_combo->addItem("<None>");
    for (int i = 0; i < doc->numBoundaries(); i++)
        m_combo->addItem(QString::fromStdString(doc->boundaryName(i)));
    form->addRow(tr("Boundary:"), m_combo);

    m_meshSize = new PyLineEdit;
    m_meshSize->setText("0");
    form->addRow(tr("Element size:"), m_meshSize);

    m_autoMesh = new QCheckBox(tr("Auto mesh"));
    m_autoMesh->setChecked(true);
    form->addRow(m_autoMesh);
    connect(m_autoMesh, &QCheckBox::toggled, this, &SegmentPropDialog::onAutoMeshToggled);

    m_hide = new QCheckBox(tr("Hidden"));
    form->addRow(m_hide);

    m_group = new PyLineEdit;
    m_group->setText("0");
    form->addRow(tr("In Group:"), m_group);

    // Conductor combo (electrostatics/heat/current only)
    if (doc->physicsType() != 0) {
        m_condCombo = new QComboBox;
        m_condCombo->addItem("<None>");
        for (int i = 0; i < doc->numCircuits(); i++)
            m_condCombo->addItem(QString::fromStdString(doc->circuitName(i)));
        form->addRow(tr("In Conductor:"), m_condCombo);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    onAutoMeshToggled(true);
    ensureTitleVisible(this);
}

void SegmentPropDialog::onAutoMeshToggled(bool on)
{
    m_meshSize->setEnabled(!on);
    if (on) m_meshSize->setText("0");
}

double SegmentPropDialog::meshSize() const { return m_meshSize->evaluateAsDouble(0); }
bool SegmentPropDialog::autoMesh() const { return m_autoMesh->isChecked(); }
bool SegmentPropDialog::hidden() const { return m_hide->isChecked(); }
int SegmentPropDialog::group() const { return (int)m_group->evaluateAsDouble(0); }
void SegmentPropDialog::setMeshSize(double s) { m_meshSize->setText(QString::number(s, 'g', 10)); }
void SegmentPropDialog::setAutoMesh(bool on) { m_autoMesh->setChecked(on); onAutoMeshToggled(on); }
void SegmentPropDialog::setHidden(bool h) { m_hide->setChecked(h); }
void SegmentPropDialog::setGroup(int g) { m_group->setText(QString::number(g)); }

// ================================================================
// ArcPropDialog
// ================================================================

ArcPropDialog::ArcPropDialog(DocumentBase *doc, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Arc Segment Properties"));
    auto *form = new QFormLayout;

    m_combo = new QComboBox;
    m_combo->addItem("<None>");
    for (int i = 0; i < doc->numBoundaries(); i++)
        m_combo->addItem(QString::fromStdString(doc->boundaryName(i)));
    form->addRow(tr("Boundary:"), m_combo);

    m_maxSeg = new PyLineEdit;
    m_maxSeg->setText("10");
    form->addRow(tr("Max segment (deg):"), m_maxSeg);

    m_hide = new QCheckBox(tr("Hidden"));
    form->addRow(m_hide);

    m_group = new PyLineEdit;
    m_group->setText("0");
    form->addRow(tr("In Group:"), m_group);

    // Conductor combo (electrostatics/heat/current only)
    if (doc->physicsType() != 0) {
        m_condCombo = new QComboBox;
        m_condCombo->addItem("<None>");
        for (int i = 0; i < doc->numCircuits(); i++)
            m_condCombo->addItem(QString::fromStdString(doc->circuitName(i)));
        form->addRow(tr("In Conductor:"), m_condCombo);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    ensureTitleVisible(this);
}

double ArcPropDialog::maxSegAngle() const {
    double v = m_maxSeg->evaluateAsDouble(10);
    if (v < 0.01) v = 0.01;
    if (v > 10.0) v = 10.0;
    return v;
}
bool ArcPropDialog::hidden() const { return m_hide->isChecked(); }
int ArcPropDialog::group() const { return (int)m_group->evaluateAsDouble(0); }
void ArcPropDialog::setMaxSegAngle(double a) { m_maxSeg->setText(QString::number(a, 'g', 10)); }
void ArcPropDialog::setHidden(bool h) { m_hide->setChecked(h); }
void ArcPropDialog::setGroup(int g) { m_group->setText(QString::number(g)); }

// ================================================================
// BlockPropDialog — the most complex dialog
// ================================================================

BlockPropDialog::BlockPropDialog(DocumentBase *doc, QWidget *parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Block Label Properties"));
    bool isMag = (doc->physicsType() == 0);

    auto *form = new QFormLayout;

    // Material combo: <None>, <No Mesh>, then all materials
    m_matCombo = new QComboBox;
    m_matCombo->addItem("<None>");
    m_matCombo->addItem("<No Mesh>");
    for (int i = 0; i < doc->numMaterials(); i++)
        m_matCombo->addItem(QString::fromStdString(doc->materialName(i)));
    form->addRow(tr("Material:"), m_matCombo);
    connect(m_matCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BlockPropDialog::onMaterialChanged);

    // Auto mesh + mesh size
    m_autoMesh = new QCheckBox(tr("Auto mesh"));
    m_autoMesh->setChecked(true);
    form->addRow(m_autoMesh);
    connect(m_autoMesh, &QCheckBox::toggled, this, &BlockPropDialog::onAutoMeshToggled);

    m_meshSize = new PyLineEdit;
    m_meshSize->setText("0");
    m_meshSize->setEnabled(false);
    form->addRow(tr("Mesh size:"), m_meshSize);

    // Circuit combo (magnetics only)
    m_circCombo = new QComboBox;
    m_circCombo->addItem("<None>");
    for (int i = 0; i < doc->numCircuits(); i++)
        m_circCombo->addItem(QString::fromStdString(doc->circuitName(i)));
    if (isMag) {
        form->addRow(tr("In Circuit:"), m_circCombo);
        connect(m_circCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &BlockPropDialog::onCircuitChanged);
    } else {
        m_circCombo->hide();
    }

    // Magnetization direction (magnetics only)
    m_magDirLabel = new QLabel(tr("Magnetization dir:"));
    m_magDir = new PyLineEdit;
    m_magDir->setText("0");
    m_magDir->setEnabled(false);
    if (isMag) form->addRow(m_magDirLabel, m_magDir);
    else { m_magDirLabel->hide(); m_magDir->hide(); }

    // Turns (magnetics only)
    m_turnsLabel = new QLabel(tr("Turns:"));
    m_turns = new PyLineEdit;
    m_turns->setText("1");
    m_turns->setEnabled(false);
    if (isMag) form->addRow(m_turnsLabel, m_turns);
    else { m_turnsLabel->hide(); m_turns->hide(); }

    // Group
    m_group = new PyLineEdit;
    m_group->setText("0");
    form->addRow(tr("In Group:"), m_group);

    // External + Default
    m_external = new QCheckBox(tr("External region"));
    m_default = new QCheckBox(tr("Default"));
    form->addRow(m_external);
    form->addRow(m_default);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    ensureTitleVisible(this);
}

void BlockPropDialog::onAutoMeshToggled(bool on)
{
    m_meshSize->setEnabled(!on);
    if (on) m_meshSize->setText("0");
}

void BlockPropDialog::onMaterialChanged(int idx)
{
    // Enable magnetization direction only for PM materials (H_c != 0) — magnetics only
    bool enableMagDir = false;
    if (m_doc->physicsType() == 0 && idx >= 2) {
        auto *magDoc = dynamic_cast<MagDocument*>(m_doc);
        if (magDoc) {
            int matIdx = idx - 2;
            if (matIdx >= 0 && matIdx < (int)magDoc->blockproplist.size())
                enableMagDir = (magDoc->blockproplist[matIdx].H_c != 0);
        }
    }
    m_magDir->setEnabled(enableMagDir);
    m_magDirLabel->setEnabled(enableMagDir);
}

void BlockPropDialog::onCircuitChanged(int idx)
{
    // Enable turns only for series circuits (CircType != 0) — magnetics only
    bool enableTurns = false;
    if (m_doc->physicsType() == 0 && idx >= 1) {
        auto *magDoc = dynamic_cast<MagDocument*>(m_doc);
        if (magDoc) {
            int circIdx = idx - 1;
            if (circIdx >= 0 && circIdx < (int)magDoc->circproplist.size())
                enableTurns = (magDoc->circproplist[circIdx].CircType != 0);
        }
    }
    m_turns->setEnabled(enableTurns);
    m_turnsLabel->setEnabled(enableTurns);
    if (!enableTurns) m_turns->setText("1");
}

void BlockPropDialog::setProblemType(int pt) {
    m_problemType = pt;
    m_external->setEnabled(pt == 1);  // only for axisymmetric
}

double BlockPropDialog::meshSize() const { return m_meshSize->evaluateAsDouble(0); }
bool BlockPropDialog::autoMesh() const { return m_autoMesh->isChecked(); }
double BlockPropDialog::magDir() const { return m_magDir->evaluateAsDouble(0); }

QString BlockPropDialog::magDirFctn() const {
    // If the text looks like it could be a function (not a plain number), return as-is
    QString t = m_magDir->text().trimmed();
    bool ok;
    t.toDouble(&ok);
    if (ok) return QString();  // plain number, no function
    return t;  // expression/function string
}

int BlockPropDialog::turns() const {
    int t = (int)m_turns->evaluateAsDouble(1);
    return (t == 0) ? 1 : t;
}
int BlockPropDialog::group() const { return (int)m_group->evaluateAsDouble(0); }
bool BlockPropDialog::isExternal() const { return m_external->isChecked(); }
bool BlockPropDialog::isDefault() const { return m_default->isChecked(); }

void BlockPropDialog::setSelectedCircuit(int idx) {
    m_circCombo->setCurrentIndex(idx);
    onCircuitChanged(idx);
}
void BlockPropDialog::setMeshSize(double s) {
    m_meshSize->setText(QString::number(s, 'g', 10));
}
void BlockPropDialog::setAutoMesh(bool on) {
    m_autoMesh->setChecked(on);
    onAutoMeshToggled(on);
}
void BlockPropDialog::setMagDir(double d) { m_magDir->setText(QString::number(d, 'g', 15)); }
void BlockPropDialog::setMagDirFctn(const QString &f) {
    if (!f.isEmpty()) m_magDir->setText(f);
}
void BlockPropDialog::setTurns(int t) { m_turns->setText(QString::number(t)); }
void BlockPropDialog::setGroup(int g) { m_group->setText(QString::number(g)); }
void BlockPropDialog::setExternal(bool e) { m_external->setChecked(e); }
void BlockPropDialog::setDefault(bool d) { m_default->setChecked(d); }
