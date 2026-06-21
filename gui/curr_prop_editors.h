// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_prop_editors.h — Property editor dialogs for current flow
#ifndef CURR_PROP_EDITORS_H
#define CURR_PROP_EDITORS_H

#include "point_prop_editor_base.h"

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include "curr_types.h"

class PyLineEdit;

// Material: name, ox, oy, ex, ey, ltx, lty
class CurrMaterialEditorDialog : public QDialog {
    Q_OBJECT
public:
    CurrMaterialEditorDialog(QWidget *parent = nullptr);
    void fromProp(const CurrMaterialProp &m);
    void toProp(CurrMaterialProp &m) const;
private:
    PyLineEdit *m_name, *m_ox, *m_oy, *m_ex, *m_ey, *m_ltx, *m_lty;
};

// Boundary: name, BdryFormat (0-4), complex vs/qs/c0/c1
class CurrBoundaryEditorDialog : public QDialog {
    Q_OBJECT
public:
    CurrBoundaryEditorDialog(QWidget *parent = nullptr);
    void fromProp(const CurrBoundaryProp &b);
    void toProp(CurrBoundaryProp &b) const;
private slots:
    void onTypeChanged(int idx);
private:
    PyLineEdit *m_name;
    PyLineEdit *m_vsRe, *m_vsIm;
    PyLineEdit *m_qsRe, *m_qsIm;
    PyLineEdit *m_c0Re, *m_c0Im;
    PyLineEdit *m_c1Re, *m_c1Im;
    QComboBox *m_type;
    QLabel *m_lvsRe, *m_lvsIm;
    QLabel *m_lqsRe, *m_lqsIm;
    QLabel *m_lc0Re, *m_lc0Im;
    QLabel *m_lc1Re, *m_lc1Im;
};

// Conductor: name, complex vc/qc, CircType
class CurrConductorEditorDialog : public QDialog {
    Q_OBJECT
public:
    CurrConductorEditorDialog(QWidget *parent = nullptr);
    void fromProp(const CurrCircuit &c);
    void toProp(CurrCircuit &c) const;
private:
    PyLineEdit *m_name;
    PyLineEdit *m_vcRe, *m_vcIm;
    PyLineEdit *m_qcRe, *m_qcIm;
    QComboBox *m_type;
};

// Point property: name, complex vp/qp
class CurrPointPropEditorDialog : public PointPropEditorBase {
    Q_OBJECT
public:
    CurrPointPropEditorDialog(QWidget *parent = nullptr);
    void fromProp(const CurrPointProp &p);
    void toProp(CurrPointProp &p) const;
private:
    PyLineEdit *m_vp, *m_qp;
};

#endif // CURR_PROP_EDITORS_H
