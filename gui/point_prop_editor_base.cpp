// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// point_prop_editor_base.cpp — Base class for point/nodal property editor dialogs
// Ported from femm42src/femm NodeProp.cpp (common pattern across all physics types)

#include "point_prop_editor_base.h"
#include "dialog_util.h"
#include "py_line_edit.h"

#include <QVBoxLayout>
#include <QDialogButtonBox>

PointPropEditorBase::PointPropEditorBase(const QString &radioALabel,
                                         const QString &radioBLabel,
                                         const QString &groupATitle,
                                         const QString &groupBTitle,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Nodal Property"));

    auto *layout = new QVBoxLayout(this);

    // Name row
    auto *nameForm = new QFormLayout;
    m_name = new PyLineEdit;
    m_name->setText("New Point Property");
    nameForm->addRow(tr("Name"), m_name);
    layout->addLayout(nameForm);

    // Radio buttons
    m_radioA = new QRadioButton(radioALabel);
    m_radioB = new QRadioButton(radioBLabel);
    m_radioA->setChecked(true);
    layout->addWidget(m_radioA);
    layout->addWidget(m_radioB);

    connect(m_radioA, &QRadioButton::clicked, this, &PointPropEditorBase::onSetA);
    connect(m_radioB, &QRadioButton::clicked, this, &PointPropEditorBase::onSetI);

    // Group boxes (subclasses populate the layouts via groupALayout/groupBLayout)
    m_groupA = new QGroupBox(groupATitle);
    m_groupAForm = new QFormLayout(m_groupA);
    layout->addWidget(m_groupA);

    m_groupB = new QGroupBox(groupBTitle);
    m_groupBForm = new QFormLayout(m_groupB);
    layout->addWidget(m_groupB);

    // OK / Cancel
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    ensureTitleVisible(this);
}

void PointPropEditorBase::setName(const QString &name) { m_name->setText(name); }
QString PointPropEditorBase::name() const { return m_name->text(); }

void PointPropEditorBase::addGroupAField(PyLineEdit *field) { m_groupAFields.append(field); }
void PointPropEditorBase::addGroupBField(PyLineEdit *field) { m_groupBFields.append(field); }

void PointPropEditorBase::selectMode(bool isPotential)
{
    if (isPotential) {
        m_radioA->setChecked(true);
        onSetA();
    } else {
        m_radioB->setChecked(true);
        onSetI();
    }
}

void PointPropEditorBase::onSetA()
{
    // Enable group A, disable group B and zero its fields
    for (auto *f : m_groupAFields) f->setEnabled(true);
    for (auto *f : m_groupBFields) { f->setEnabled(false); f->setText("0"); }
}

void PointPropEditorBase::onSetI()
{
    // Enable group B, disable group A and zero its fields
    for (auto *f : m_groupBFields) f->setEnabled(true);
    for (auto *f : m_groupAFields) { f->setEnabled(false); f->setText("0"); }
}
