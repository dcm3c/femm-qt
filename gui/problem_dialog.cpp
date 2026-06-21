// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// problem_dialog.cpp — Problem Definition dialog
// Shows fields appropriate for the physics type (magnetics vs electrostatics).

#include "problem_dialog.h"
#include "py_line_edit.h"
#include "document.h"
#include "mag_document.h"
#include "elec_document.h"
#include "curr_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFileDialog>
#include <QTextEdit>
#include <QLabel>
#include <cmath>

ProblemDialog::ProblemDialog(DocumentBase *doc, QWidget *parent)
    : QDialog(parent), m_doc(doc)
{
    setWindowTitle(tr("Problem Definition"));
    bool isMag = (doc->physicsType() == 0);
    bool isCurr = (doc->physicsType() == 3);

    auto *form = new QFormLayout;

    // Frequency (magnetics and current flow)
    m_frequency = new PyLineEdit;
    if (isMag) {
        auto *magDoc = dynamic_cast<MagDocument*>(doc);
        m_frequency->setText(QString::number(magDoc ? magDoc->Frequency : 0, 'g', 15));
        form->addRow(tr("Frequency (Hz):"), m_frequency);
    } else if (isCurr) {
        auto *currDoc = dynamic_cast<CurrDocument*>(doc);
        m_frequency->setText(QString::number(currDoc ? currDoc->Frequency : 60, 'g', 15));
        form->addRow(tr("Frequency (Hz):"), m_frequency);
    } else {
        m_frequency->hide();
    }

    m_lengthUnits = new QComboBox;
    m_lengthUnits->addItem(tr("Inches"));       // 0
    m_lengthUnits->addItem(tr("Millimeters"));   // 1
    m_lengthUnits->addItem(tr("Centimeters"));   // 2
    m_lengthUnits->addItem(tr("Meters"));        // 3
    m_lengthUnits->addItem(tr("Mils"));          // 4
    m_lengthUnits->addItem(tr("Micrometers"));   // 5
    m_lengthUnits->setCurrentIndex(doc->LengthUnits);
    form->addRow(tr("Length Units:"), m_lengthUnits);

    m_probType = new QComboBox;
    m_probType->addItem(tr("Planar"));          // 0
    m_probType->addItem(tr("Axisymmetric"));    // 1
    m_probType->setCurrentIndex(doc->ProblemType);
    form->addRow(tr("Problem Type:"), m_probType);
    connect(m_probType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProblemDialog::onProbTypeChanged);

    m_precision = new PyLineEdit;
    m_precision->setText(QString::number(doc->Precision, 'g', 15));
    form->addRow(tr("Precision:"), m_precision);

    m_depth = new PyLineEdit;
    m_depth->setText(QString::number(doc->Depth, 'g', 15));
    m_depth->setEnabled(doc->ProblemType == 0);
    form->addRow(tr("Depth:"), m_depth);

    m_minAngle = new PyLineEdit;
    m_minAngle->setText(QString::number(doc->MinAngle, 'g', 15));
    form->addRow(tr("Min Angle (deg):"), m_minAngle);

    // AC Solver (magnetics only)
    m_acSolver = new QComboBox;
    m_acSolver->addItem(tr("Successive Approximation"));  // 0
    m_acSolver->addItem(tr("Newton"));                     // 1
    if (isMag) {
        auto *magDoc = dynamic_cast<MagDocument*>(doc);
        m_acSolver->setCurrentIndex(magDoc ? magDoc->ACSolver : 0);
        form->addRow(tr("AC Solver:"), m_acSolver);
    } else {
        m_acSolver->hide();
    }

    // Smart Mesh
    m_smartMesh = new QComboBox;
    m_smartMesh->addItem(tr("Off"));   // 0
    m_smartMesh->addItem(tr("On"));    // 1
    if (isMag) {
        auto *magDoc = dynamic_cast<MagDocument*>(doc);
        m_smartMesh->setCurrentIndex(magDoc ? magDoc->SmartMesh : 0);
    } else {
        auto *elecDoc = dynamic_cast<ElecDocument*>(doc);
        m_smartMesh->setCurrentIndex(elecDoc ? elecDoc->SmartMesh : 0);
    }
    form->addRow(tr("Smart Mesh:"), m_smartMesh);

    // Previous solution file + browse button (magnetics only)
    m_prevSoln = new QLineEdit;
    m_prevType = new QComboBox;
    if (isMag) {
        auto *magDoc = dynamic_cast<MagDocument*>(doc);
        auto *prevRow = new QHBoxLayout;
        m_prevSoln->setText(QString::fromStdString(magDoc ? magDoc->PrevSoln : ""));
        m_prevSoln->setPlaceholderText(tr("(none)"));
        auto *browseBtn = new QPushButton(tr("..."));
        browseBtn->setFixedWidth(30);
        connect(browseBtn, &QPushButton::clicked, this, &ProblemDialog::onBrowsePrevSoln);
        prevRow->addWidget(m_prevSoln);
        prevRow->addWidget(browseBtn);
        form->addRow(tr("Prev. Solution:"), prevRow);

        m_prevType->addItem(tr("None"));          // 0
        m_prevType->addItem(tr("Incremental"));   // 1
        m_prevType->addItem(tr("Frozen"));        // 2
        m_prevType->setCurrentIndex(magDoc ? magDoc->PrevType : 0);
        form->addRow(tr("Prev. Type:"), m_prevType);
    } else {
        m_prevSoln->hide();
        m_prevType->hide();
    }

    m_comment = new QTextEdit;
    m_comment->setPlainText(QString::fromStdString(doc->ProblemNote));
    m_comment->setMaximumHeight(80);
    form->addRow(tr("Comment:"), m_comment);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ProblemDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

void ProblemDialog::onProbTypeChanged(int idx)
{
    m_depth->setEnabled(idx == 0);  // depth only for planar
}

void ProblemDialog::onBrowsePrevSoln()
{
    QString path = QFileDialog::getOpenFileName(this,
        tr("Select Previous Solution"), QString(),
        tr("Solution Files (*.ans *.res);;All Files (*)"));
    if (!path.isEmpty())
        m_prevSoln->setText(path);
}

void ProblemDialog::accept()
{
    // Shared fields
    m_doc->LengthUnits = m_lengthUnits->currentIndex();
    m_doc->ProblemType = m_probType->currentIndex();

    double prec = m_precision->evaluateAsDouble(1e-8);
    if (prec == 0 || prec > 1e-8) prec = 1e-8;
    if (prec < 1e-16) prec = 1e-16;
    m_doc->Precision = prec;

    if (m_doc->ProblemType == 1)
        m_doc->Depth = 1;
    else {
        m_doc->Depth = fabs(m_depth->evaluateAsDouble(1));
        if (m_doc->Depth <= 0) m_doc->Depth = 1;
    }

    double ma = m_minAngle->evaluateAsDouble(30);
    if (ma >= 1 && ma <= 33.8) m_doc->MinAngle = ma;

    m_doc->ProblemNote = m_comment->toPlainText().toStdString();

    // Magnetics-specific fields
    if (auto *magDoc = dynamic_cast<MagDocument*>(m_doc)) {
        magDoc->Frequency = fabs(m_frequency->evaluateAsDouble(0));
        magDoc->ACSolver = m_acSolver->currentIndex();
        magDoc->SmartMesh = m_smartMesh->currentIndex();
        magDoc->PrevSoln = m_prevSoln->text().toStdString();
        magDoc->PrevType = m_prevType->currentIndex();
        if (magDoc->PrevSoln.empty()) magDoc->PrevType = 0;
    }
    // Electrostatics-specific fields
    else if (auto *elecDoc = dynamic_cast<ElecDocument*>(m_doc)) {
        elecDoc->SmartMesh = m_smartMesh->currentIndex();
    }
    // Current flow-specific fields
    else if (auto *currDoc = dynamic_cast<CurrDocument*>(m_doc)) {
        currDoc->Frequency = fabs(m_frequency->evaluateAsDouble(60));
        currDoc->SmartMesh = m_smartMesh->currentIndex();
    }

    QDialog::accept();
}
