// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// point_prop_editor_base.h — Base class for point/nodal property editor dialogs
// Provides the common "name + two mutually-exclusive field groups" pattern
// used by all four physics types (magnetics, electrostatics, heat, current flow).
#ifndef POINT_PROP_EDITOR_BASE_H
#define POINT_PROP_EDITOR_BASE_H

#include <QDialog>
#include <QGroupBox>
#include <QRadioButton>
#include <QFormLayout>
#include <QVector>

class PyLineEdit;

class PointPropEditorBase : public QDialog {
    Q_OBJECT
public:
    // radioALabel / radioBLabel:  text for the two radio buttons
    // groupATitle / groupBTitle:  titles for the two group boxes
    PointPropEditorBase(const QString &radioALabel,
                        const QString &radioBLabel,
                        const QString &groupATitle,
                        const QString &groupBTitle,
                        QWidget *parent = nullptr);

    void setName(const QString &name);
    QString name() const;

protected:
    // Subclasses add their PyLineEdit fields to these layouts, then call
    // addGroupAField / addGroupBField so the base can enable/disable them.
    QFormLayout *groupALayout() const { return m_groupAForm; }
    QFormLayout *groupBLayout() const { return m_groupBForm; }

    void addGroupAField(PyLineEdit *field);
    void addGroupBField(PyLineEdit *field);

    // Call from fromProp() to set the initial radio-button state.
    // isPotential == true  → group A enabled, group B disabled
    // isPotential == false → group B enabled, group A disabled
    void selectMode(bool isPotential);

private slots:
    void onSetA();
    void onSetI();

private:
    PyLineEdit  *m_name;
    QRadioButton *m_radioA, *m_radioB;
    QGroupBox    *m_groupA, *m_groupB;
    QFormLayout  *m_groupAForm, *m_groupBForm;
    QVector<PyLineEdit*> m_groupAFields, m_groupBFields;
};

#endif // POINT_PROP_EDITOR_BASE_H
