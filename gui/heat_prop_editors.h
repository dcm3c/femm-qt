// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_prop_editors.h — Property editor dialogs for heat flow
#ifndef HEAT_PROP_EDITORS_H
#define HEAT_PROP_EDITORS_H

#include "point_prop_editor_base.h"

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <vector>
#include "heat_types.h"

class PyLineEdit;

class QPushButton;

// Material: name, T-k curve selection, Kx, Ky, qv, Kt
class HeatMaterialEditorDialog : public QDialog {
    Q_OBJECT
public:
    HeatMaterialEditorDialog(QWidget *parent = nullptr);
    void fromProp(const HMaterialProp &m);
    void toProp(HMaterialProp &m) const;
private slots:
    void onNonlinearChanged(int idx);
    void onEditTKCurve();
private:
    PyLineEdit *m_name, *m_Kx, *m_Ky, *m_qv, *m_Kt;
    QLabel *m_KxLabel, *m_KyLabel;
    QComboBox *m_nlcurve;
    QPushButton *m_tkButton;

    // The T-k curve as entered. Held here rather than in the prop so that a
    // cancelled edit leaves the material alone.
    std::vector<double> m_T, m_K;
    int m_npts = 0;
};

// Boundary: name, type, Tset, qs, Tinf, h, beta, TinfRad
class HeatBoundaryEditorDialog : public QDialog {
    Q_OBJECT
public:
    HeatBoundaryEditorDialog(QWidget *parent = nullptr);
    void fromProp(const HBoundaryProp &b);
    void toProp(HBoundaryProp &b) const;
private slots:
    void onTypeChanged(int idx);
private:
    PyLineEdit *m_name, *m_Tset, *m_qs, *m_Tinf, *m_h, *m_beta, *m_TinfRad;
    QComboBox *m_type;
    QLabel *m_lTset, *m_lqs, *m_lTinf, *m_lh, *m_lbeta, *m_lTinfRad;
};

// Conductor: name, T, q, type
class HeatConductorEditorDialog : public QDialog {
    Q_OBJECT
public:
    HeatConductorEditorDialog(QWidget *parent = nullptr);
    void fromProp(const HCircuit &c);
    void toProp(HCircuit &c) const;
private slots:
    void onTypeChanged(int idx);
private:
    PyLineEdit *m_name, *m_T, *m_q;
    QComboBox *m_type;
};

// Point property: name, T, qp
class HeatPointPropEditorDialog : public PointPropEditorBase {
    Q_OBJECT
public:
    HeatPointPropEditorDialog(QWidget *parent = nullptr);
    void fromProp(const HPointProp &p);
    void toProp(HPointProp &p) const;
private:
    PyLineEdit *m_T, *m_qp;
};

#endif // HEAT_PROP_EDITORS_H
