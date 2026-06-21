// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#ifndef COPY_MOVE_DIALOG_H
#define COPY_MOVE_DIALOG_H

#include <QDialog>
#include <QRadioButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

class PyLineEdit;

// Combined Move/Copy dialog matching OF's CCopyDlg.
// isMove=true: Move operation (no "number of copies" field)
// isMove=false: Copy operation (includes "number of copies" field)
class CopyMoveDialog : public QDialog
{
    Q_OBJECT
public:
    CopyMoveDialog(bool isMove, QWidget *parent = nullptr);

    bool isTranslate() const { return m_translate->isChecked(); }

    // Translation values
    double dx() const;
    double dy() const;

    // Rotation values
    double angle() const;
    double aboutX() const;
    double aboutY() const;

    // Copy count (only meaningful for copy mode)
    int copies() const;

private slots:
    void onModeChanged();

private:
    QRadioButton *m_translate;
    QRadioButton *m_rotate;

    // Translation fields
    QLabel *m_dxLabel, *m_dyLabel;
    PyLineEdit *m_dx, *m_dy;

    // Rotation fields
    QLabel *m_angleLabel, *m_aboutXLabel, *m_aboutYLabel;
    PyLineEdit *m_angle, *m_aboutX, *m_aboutY;

    // Copy count (null for move)
    QLabel *m_ncopiesLabel = nullptr;
    PyLineEdit *m_ncopies = nullptr;
};

#endif // COPY_MOVE_DIALOG_H
