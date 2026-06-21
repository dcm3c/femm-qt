// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prefs_dialog.h — View preferences dialog matching original FEMM
// Color picker with dropdown of all 55 colors, display/numeric settings.
#ifndef PREFS_DIALOG_H
#define PREFS_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

class PyLineEdit;

class PrefsDialog : public QDialog {
    Q_OBJECT
public:
    PrefsDialog(QWidget *parent = nullptr);

    // Display toggles
    bool smooth() const;
    bool showLegend() const;
    bool greyscale() const;
    bool showGrid() const;
    bool snapGrid() const;
    bool showMesh() const;
    bool showPoints() const;
    bool showNames() const;
    bool showRealContours() const;
    bool showImagContours() const;
    bool shiftH() const;
    bool showMask() const;
    int numContours() const;
    int lineIntegralPoints() const;
    int plotPoints() const;
    int densityPlotType() const;
    int editAction() const;

    void setSmooth(bool on);
    void setShowLegend(bool on);
    void setShowMesh(bool on);
    void setNumContours(int n);
    void setDensityPlotType(int t);
    void setEditAction(int a);

private slots:
    void onModifyColor();
    void onResetColors();
    void accept() override;

private:
    // Color picker
    QComboBox *m_colorList;
    QPushButton *m_modifyBtn;

    // Display toggles
    QCheckBox *m_smooth, *m_legend, *m_grey;
    QCheckBox *m_grid, *m_snap, *m_mesh, *m_pts, *m_names;
    QCheckBox *m_showAr, *m_showAi, *m_showMask;
    QCheckBox *m_shiftH;

    // Numeric
    PyLineEdit *m_numContours, *m_liPoints, *m_plotPoints;

    // Mode combos
    QComboBox *m_densityType, *m_editActionType, *m_vectorType;
};

#endif // PREFS_DIALOG_H
