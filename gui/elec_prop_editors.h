// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_prop_editors.h — Property editor dialogs for electrostatics
#ifndef ELEC_PROP_EDITORS_H
#define ELEC_PROP_EDITORS_H

#include "point_prop_editor_base.h"

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include "elec_types.h"

class PyLineEdit;

// Material: name, ex, ey, qv
class ElecMaterialEditorDialog : public QDialog {
    Q_OBJECT
public:
    ElecMaterialEditorDialog(QWidget *parent = nullptr);
    void fromProp(const EMaterialProp &m);
    void toProp(EMaterialProp &m) const;
private:
    PyLineEdit *m_name, *m_ex, *m_ey, *m_qv;
};

// Boundary: name, type, V, qs, c0, c1
class ElecBoundaryEditorDialog : public QDialog {
    Q_OBJECT
public:
    ElecBoundaryEditorDialog(QWidget *parent = nullptr);
    void fromProp(const EBoundaryProp &b);
    void toProp(EBoundaryProp &b) const;
private slots:
    void onTypeChanged(int idx);
private:
    PyLineEdit *m_name, *m_V, *m_qs, *m_c0, *m_c1;
    QComboBox *m_type;
    QLabel *m_lV, *m_lqs, *m_lc0, *m_lc1;
};

// Conductor: name, V, q, type
class ElecConductorEditorDialog : public QDialog {
    Q_OBJECT
public:
    ElecConductorEditorDialog(QWidget *parent = nullptr);
    void fromProp(const ECircuit &c);
    void toProp(ECircuit &c) const;
private:
    PyLineEdit *m_name, *m_V, *m_q;
    QComboBox *m_type;
};

// Point property: name, V, qp
class ElecPointPropEditorDialog : public PointPropEditorBase {
    Q_OBJECT
public:
    ElecPointPropEditorDialog(QWidget *parent = nullptr);
    void fromProp(const EPointProp &p);
    void toProp(EPointProp &p) const;
private:
    PyLineEdit *m_V, *m_qp;
};

#endif // ELEC_PROP_EDITORS_H
