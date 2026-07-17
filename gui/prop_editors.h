// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_editors.h — Individual property editor dialogs for magnetics
// MaterialEditor, BoundaryEditor, CircuitEditor, PointPropEditor
#ifndef PROP_EDITORS_H
#define PROP_EDITORS_H

#include "point_prop_editor_base.h"

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QTextEdit>
#include <QRadioButton>
#include <QPushButton>
#include <vector>
#include <string>

class PyLineEdit;
struct FMaterialProp;
struct FBoundaryProp;
struct FCircuit;
struct FPointProp;

// ================================================================
// Material property editor
// ================================================================
class MaterialEditorDialog : public QDialog {
    Q_OBJECT
public:
    MaterialEditorDialog(QWidget *parent = nullptr);
    void fromProp(const FMaterialProp &m);
    void toProp(FMaterialProp &m) const;
    void setName(const QString &name);
    QString name() const;
private slots:
    void onLamTypeChanged(int idx);
    void onNonlinearToggled(bool on);
    void onEditBHCurve();
private:
    void applyWireDefaults();   // OF CMatDlg::IsWire()
    // B-H curve data (preserved across dialog open/close)
    std::vector<double> m_bhB, m_bhH;
    int m_bhPoints = 0;
    PyLineEdit *m_name;
    PyLineEdit *m_mu_x, *m_mu_y;
    PyLineEdit *m_Hc, *m_Jsrc;
    PyLineEdit *m_Cduct, *m_Lam_d, *m_LamFill;
    PyLineEdit *m_Theta_hn, *m_Theta_hx, *m_Theta_hy;
    PyLineEdit *m_NStrands, *m_WireD;
    QComboBox *m_lamType;
    QCheckBox *m_nonlinear;
    QLabel *m_muLabel, *m_hcLabel, *m_wireLabel;
    QPushButton *m_bhButton;
};

// ================================================================
// Boundary condition editor
// ================================================================
class BoundaryEditorDialog : public QDialog {
    Q_OBJECT
public:
    BoundaryEditorDialog(QWidget *parent = nullptr);
    void fromProp(const FBoundaryProp &b);
    void toProp(FBoundaryProp &b) const;
    void setName(const QString &name);
    QString name() const;
private slots:
    void onTypeChanged(int idx);
private:
    PyLineEdit *m_name;
    QComboBox *m_type;
    PyLineEdit *m_A0, *m_A1, *m_A2, *m_phi;
    PyLineEdit *m_Mu, *m_Sig;
    PyLineEdit *m_c0, *m_c1;
    PyLineEdit *m_innerAngle, *m_outerAngle;
    QWidget *m_panelA, *m_panelSSD, *m_panelMixed, *m_panelAGE;
};

// ================================================================
// Circuit property editor
// ================================================================
class CircuitEditorDialog : public QDialog {
    Q_OBJECT
public:
    CircuitEditorDialog(QWidget *parent = nullptr);
    void fromProp(const FCircuit &c);
    void toProp(FCircuit &c) const;
    void setName(const QString &name);
    QString name() const;
private:
    PyLineEdit *m_name;
    PyLineEdit *m_amps;
    QRadioButton *m_series, *m_parallel;
};

// ================================================================
// Point (nodal) property editor
// ================================================================
class PointPropEditorBase;

class PointPropEditorDialog : public PointPropEditorBase {
    Q_OBJECT
public:
    PointPropEditorDialog(QWidget *parent = nullptr);
    void fromProp(const FPointProp &p);
    void toProp(FPointProp &p) const;
private:
    PyLineEdit *m_Ap;
    PyLineEdit *m_Jp;
};

#endif // PROP_EDITORS_H
