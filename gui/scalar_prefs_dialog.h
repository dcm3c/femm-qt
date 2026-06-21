// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_prefs_dialog.h — Postprocessor preferences for scalar potential problems
#ifndef SCALAR_PREFS_DIALOG_H
#define SCALAR_PREFS_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

class PyLineEdit;

class ScalarPrefsDialog : public QDialog {
    Q_OBJECT
public:
    ScalarPrefsDialog(QWidget *parent = nullptr, int physicsType = 1);

    bool smooth() const;
    bool showLegend() const;
    bool greyscale() const;
    bool showGrid() const;
    bool snapGrid() const;
    bool showMesh() const;
    bool showPoints() const;
    bool showNames() const;
    bool showContours() const;
    bool showMask() const;
    int numContours() const;
    int lineIntegralPoints() const;
    int plotPoints() const;
    int densityPlotType() const;
    int editAction() const;

    void setSmooth(bool on);
    void setShowLegend(bool on);
    void setShowMesh(bool on);
    void setShowPoints(bool on);
    void setNumContours(int n);
    void setDensityPlotType(int t);
    void setEditAction(int a);

private slots:
    void onModifyColor();
    void onResetColors();
    void accept() override;

private:
    QComboBox *m_colorList;
    QCheckBox *m_smooth, *m_legend, *m_grey;
    QCheckBox *m_grid, *m_snap, *m_mesh, *m_pts, *m_names;
    QCheckBox *m_showContours, *m_showMask;
    PyLineEdit *m_numContours, *m_liPoints, *m_plotPoints;
    QComboBox *m_densityType, *m_editActionType, *m_vectorType;
    QString m_prefix; // "eo", "ho", or "co"
};

#endif // SCALAR_PREFS_DIALOG_H
