// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// problem_dialog.h — Problem Definition dialog for magnetics
#ifndef PROBLEM_DIALOG_H
#define PROBLEM_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>

class PyLineEdit;
class QTextEdit;
class DocumentBase;

class ProblemDialog : public QDialog {
    Q_OBJECT
public:
    ProblemDialog(DocumentBase *doc, QWidget *parent = nullptr);
private slots:
    void onProbTypeChanged(int idx);
    void onBrowsePrevSoln();
    void accept() override;
private:
    DocumentBase *m_doc;
    PyLineEdit *m_frequency;
    QComboBox *m_lengthUnits;
    QComboBox *m_probType;
    PyLineEdit *m_precision;
    PyLineEdit *m_minAngle;
    PyLineEdit *m_depth;
    QComboBox *m_acSolver;
    QComboBox *m_smartMesh;
    QLineEdit *m_prevSoln;
    QComboBox *m_prevType;
    QTextEdit *m_comment;
};

#endif // PROBLEM_DIALOG_H
