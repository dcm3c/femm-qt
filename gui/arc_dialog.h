// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#ifndef ARC_DIALOG_H
#define ARC_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <vector>
#include <string>

class PyLineEdit;

// Arc segment properties dialog matching OF's CArcDlg.
// Shown when the user draws an arc segment between two nodes.
class ArcDialog : public QDialog
{
    Q_OBJECT
public:
    ArcDialog(const std::vector<std::string> &boundaryNames,
              double arcAngle, double maxSeg, int curBdry,
              QWidget *parent = nullptr);

    double arcAngle() const;
    double maxSeg() const;
    int boundaryIndex() const;  // 0 = "<None>", 1+ = index into lineproplist

private:
    PyLineEdit *m_arcAngle;
    PyLineEdit *m_maxSeg;
    QComboBox *m_boundary;
};

#endif // ARC_DIALOG_H
