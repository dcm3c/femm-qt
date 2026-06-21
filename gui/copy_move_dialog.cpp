// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#include "copy_move_dialog.h"
#include "py_line_edit.h"

CopyMoveDialog::CopyMoveDialog(bool isMove, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(isMove ? tr("Move") : tr("Copy"));

    auto *layout = new QVBoxLayout(this);

    // Mode selection
    auto *modeBox = new QGroupBox(tr("Operation"));
    auto *modeLayout = new QHBoxLayout(modeBox);
    m_translate = new QRadioButton(tr("Translation"));
    m_rotate = new QRadioButton(tr("Rotation"));
    m_translate->setChecked(true);
    modeLayout->addWidget(m_translate);
    modeLayout->addWidget(m_rotate);
    layout->addWidget(modeBox);

    // Translation fields
    auto *transBox = new QGroupBox(tr("Translation"));
    auto *transLayout = new QGridLayout(transBox);
    m_dxLabel = new QLabel(tr("Horizontal shift:"));
    m_dx = new PyLineEdit; m_dx->setText("0");
    m_dyLabel = new QLabel(tr("Vertical shift:"));
    m_dy = new PyLineEdit; m_dy->setText("0");
    transLayout->addWidget(m_dxLabel, 0, 0);
    transLayout->addWidget(m_dx, 0, 1);
    transLayout->addWidget(m_dyLabel, 1, 0);
    transLayout->addWidget(m_dy, 1, 1);
    layout->addWidget(transBox);

    // Rotation fields
    auto *rotBox = new QGroupBox(tr("Rotation"));
    auto *rotLayout = new QGridLayout(rotBox);
    m_angleLabel = new QLabel(tr("Angular shift, degrees:"));
    m_angle = new PyLineEdit; m_angle->setText("0");
    m_aboutXLabel = new QLabel(tr("About point, x:"));
    m_aboutX = new PyLineEdit; m_aboutX->setText("0");
    m_aboutYLabel = new QLabel(tr("About point, y:"));
    m_aboutY = new PyLineEdit; m_aboutY->setText("0");
    rotLayout->addWidget(m_angleLabel, 0, 0);
    rotLayout->addWidget(m_angle, 0, 1);
    rotLayout->addWidget(m_aboutXLabel, 1, 0);
    rotLayout->addWidget(m_aboutX, 1, 1);
    rotLayout->addWidget(m_aboutYLabel, 2, 0);
    rotLayout->addWidget(m_aboutY, 2, 1);
    layout->addWidget(rotBox);

    // Number of copies (copy mode only)
    if (!isMove) {
        auto *copyLayout = new QHBoxLayout;
        m_ncopiesLabel = new QLabel(tr("Number of copies:"));
        m_ncopies = new PyLineEdit; m_ncopies->setText("1");
        copyLayout->addWidget(m_ncopiesLabel);
        copyLayout->addWidget(m_ncopies);
        layout->addLayout(copyLayout);
    }

    // OK/Cancel
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Wire up mode switching
    connect(m_translate, &QRadioButton::toggled, this, &CopyMoveDialog::onModeChanged);
    onModeChanged();
}

void CopyMoveDialog::onModeChanged()
{
    bool trans = m_translate->isChecked();
    m_dx->setEnabled(trans);
    m_dy->setEnabled(trans);
    m_dxLabel->setEnabled(trans);
    m_dyLabel->setEnabled(trans);
    m_angle->setEnabled(!trans);
    m_aboutX->setEnabled(!trans);
    m_aboutY->setEnabled(!trans);
    m_angleLabel->setEnabled(!trans);
    m_aboutXLabel->setEnabled(!trans);
    m_aboutYLabel->setEnabled(!trans);
}

double CopyMoveDialog::dx() const { return m_dx->evaluateAsDouble(); }
double CopyMoveDialog::dy() const { return m_dy->evaluateAsDouble(); }
double CopyMoveDialog::angle() const { return m_angle->evaluateAsDouble(); }
double CopyMoveDialog::aboutX() const { return m_aboutX->evaluateAsDouble(); }
double CopyMoveDialog::aboutY() const { return m_aboutY->evaluateAsDouble(); }
int CopyMoveDialog::copies() const {
    return m_ncopies ? (int)m_ncopies->evaluateAsDouble(1) : 1;
}
