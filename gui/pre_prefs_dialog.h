// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// pre_prefs_dialog.h — Preprocessor preferences dialog
#ifndef PRE_PREFS_DIALOG_H
#define PRE_PREFS_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

class PyLineEdit;

class PrePrefsDialog : public QDialog {
    Q_OBJECT
public:
    PrePrefsDialog(int physicsType = 0, QWidget *parent = nullptr);

    bool showGrid() const;
    bool snapGrid() const;
    bool showNames() const;
    bool smartMesh() const;
    double precision() const;
    double minAngle() const;
    double depth() const;
    int lengthUnits() const;
    int problemType() const;

    void setShowGrid(bool on);
    void setSnapGrid(bool on);
    void setShowNames(bool on);
    void setSmartMesh(bool on);
    void setPrecision(double p);
    void setMinAngle(double a);
    void setDepth(double d);
    void setLengthUnits(int u);
    void setProblemType(int t);
    double frequency() const;
    void setFrequency(double f);

private slots:
    void onModifyColor();
    void onResetColors();
    void accept() override;

private:
    QComboBox *m_colorList;
    QCheckBox *m_showGrid, *m_snapGrid, *m_showNames, *m_smartMesh;
    PyLineEdit *m_precision, *m_minAngle, *m_depth, *m_frequency;
    QComboBox *m_lengthUnits, *m_problemType;
};

#endif // PRE_PREFS_DIALOG_H
