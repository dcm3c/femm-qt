// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#include "arc_dialog.h"
#include "dialog_util.h"
#include "py_line_edit.h"

ArcDialog::ArcDialog(const std::vector<std::string> &boundaryNames,
                     double arcAngle, double maxSeg, int curBdry,
                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Arc segment properties"));

    auto *layout = new QGridLayout(this);

    layout->addWidget(new QLabel(tr("Arc Angle:")), 0, 0);
    m_arcAngle = new PyLineEdit;
    m_arcAngle->setText(QString::number(arcAngle));
    layout->addWidget(m_arcAngle, 0, 1);

    layout->addWidget(new QLabel(tr("Max segment, deg:")), 1, 0);
    m_maxSeg = new PyLineEdit;
    m_maxSeg->setText(QString::number(maxSeg));
    layout->addWidget(m_maxSeg, 1, 1);

    layout->addWidget(new QLabel(tr("Boundary:")), 2, 0);
    m_boundary = new QComboBox;
    m_boundary->addItem(tr("<None>"));
    for (auto &name : boundaryNames)
        m_boundary->addItem(QString::fromStdString(name));
    if (curBdry >= 0 && curBdry < m_boundary->count())
        m_boundary->setCurrentIndex(curBdry);
    layout->addWidget(m_boundary, 2, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons, 3, 0, 1, 2);
    ensureTitleVisible(this);
}

double ArcDialog::arcAngle() const
{
    double v = m_arcAngle->evaluateAsDouble(90.0);
    if (v < 1.0) v = 1.0;
    if (v > 180.0) v = 180.0;
    return v;
}

double ArcDialog::maxSeg() const
{
    double v = m_maxSeg->evaluateAsDouble(10.0);
    if (v < 0.01) v = 0.01;
    if (v > 10.0) v = 10.0;
    return v;
}

int ArcDialog::boundaryIndex() const
{
    return m_boundary->currentIndex();
}
