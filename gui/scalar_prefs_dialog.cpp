// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// scalar_prefs_dialog.cpp — Postprocessor preferences for scalar potential problems
// Ported from femm42src/femm/bv_Pref.cpp

#include "scalar_prefs_dialog.h"
#include "view_colors.h"
#include "py_line_edit.h"
#include "vector_choice.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QColorDialog>
#include <QMessageBox>
#include <QSettings>

ScalarPrefsDialog::ScalarPrefsDialog(QWidget *parent, int physicsType) : QDialog(parent)
{
    m_prefix = (physicsType == 3) ? "co" : (physicsType == 2) ? "ho" : "eo";
    setWindowTitle(tr("Postprocessor Preferences"));

    auto *mainLayout = new QVBoxLayout(this);

    // Colors
    auto *colorBox = new QGroupBox(tr("Colors"));
    auto *colorLayout = new QHBoxLayout(colorBox);
    m_colorList = new QComboBox;
    for (int i = 0; i < CI_COUNT; i++)
        m_colorList->addItem(colorName(i));
    auto *modifyBtn = new QPushButton(tr("Modify..."));
    auto *resetBtn = new QPushButton(tr("Reset All"));
    colorLayout->addWidget(m_colorList, 1);
    colorLayout->addWidget(modifyBtn);
    colorLayout->addWidget(resetBtn);
    connect(modifyBtn, &QPushButton::clicked, this, &ScalarPrefsDialog::onModifyColor);
    connect(resetBtn, &QPushButton::clicked, this, &ScalarPrefsDialog::onResetColors);
    mainLayout->addWidget(colorBox);

    // Display options
    auto *displayBox = new QGroupBox(tr("Display"));
    auto *dLayout = new QFormLayout(displayBox);

    m_smooth = new QCheckBox(tr("Smooth data"));
    m_legend = new QCheckBox(tr("Show legend"));
    m_grey = new QCheckBox(tr("Greyscale"));
    m_grid = new QCheckBox(tr("Show grid"));
    m_snap = new QCheckBox(tr("Snap to grid"));
    m_mesh = new QCheckBox(tr("Show mesh"));
    m_pts = new QCheckBox(tr("Show points"));
    m_names = new QCheckBox(tr("Show block names"));
    m_showContours = new QCheckBox((physicsType == 2) ? tr("Show temperature contours") : tr("Show potential contours"));
    m_showMask = new QCheckBox(tr("Show stress tensor mask"));

    dLayout->addRow(m_smooth);
    dLayout->addRow(m_legend);
    dLayout->addRow(m_grey);
    dLayout->addRow(m_grid);
    dLayout->addRow(m_snap);
    dLayout->addRow(m_mesh);
    dLayout->addRow(m_pts);
    dLayout->addRow(m_names);
    dLayout->addRow(m_showContours);
    dLayout->addRow(m_showMask);
    mainLayout->addWidget(displayBox);

    // Numeric
    auto *numBox = new QGroupBox(tr("Numeric"));
    auto *nLayout = new QFormLayout(numBox);

    m_numContours = new PyLineEdit; m_numContours->setText("19");
    nLayout->addRow(tr("Number of contours:"), m_numContours);

    m_liPoints = new PyLineEdit; m_liPoints->setText("400");
    nLayout->addRow(tr("Line integral points:"), m_liPoints);

    m_plotPoints = new PyLineEdit; m_plotPoints->setText("1500");
    nLayout->addRow(tr("Plot points:"), m_plotPoints);
    mainLayout->addWidget(numBox);

    // Default modes
    auto *modeBox = new QGroupBox(tr("Default Modes"));
    auto *mLayout = new QFormLayout(modeBox);

    m_densityType = new QComboBox;
    m_densityType->addItem(tr("Off"));
    if (physicsType == 3) {
        m_densityType->addItem(tr("|V|"));
        m_densityType->addItem(tr("Re(V)"));
        m_densityType->addItem(tr("Im(V)"));
        m_densityType->addItem(tr("|J|"));
        m_densityType->addItem(tr("Re(J)"));
        m_densityType->addItem(tr("Im(J)"));
        m_densityType->addItem(tr("|E|"));
        m_densityType->addItem(tr("Re(E)"));
        m_densityType->addItem(tr("Im(E)"));
        m_densityType->addItem(tr("|Jc|"));
        m_densityType->addItem(tr("Re(Jc)"));
        m_densityType->addItem(tr("Im(Jc)"));
        m_densityType->addItem(tr("|Jd|"));
    } else if (physicsType == 2) {
        m_densityType->addItem(tr("T (temperature)"));
        m_densityType->addItem(tr("|F| (heat flux)"));
        m_densityType->addItem(tr("|G| (gradient)"));
    } else {
        m_densityType->addItem(tr("|D|"));
        m_densityType->addItem(tr("|E|"));
        m_densityType->addItem(tr("V (voltage)"));
        m_densityType->addItem(tr("Energy density"));
    }
    mLayout->addRow(tr("Density plot:"), m_densityType);

    m_editActionType = new QComboBox;
    m_editActionType->addItem(tr("Point values"));
    m_editActionType->addItem(tr("Contour"));
    m_editActionType->addItem(tr("Area"));
    mLayout->addRow(tr("Edit action:"), m_editActionType);
    m_vectorType = new QComboBox;
    m_vectorType->addItem(tr("Off"));
    if (physicsType == 2) {
        m_vectorType->addItem(tr("Heat Flux (F)"));
        m_vectorType->addItem(tr("Temperature Gradient (G)"));
    } else if (physicsType == 3) {
        // The Vector Plot dialog's own two: the widget draws J or E at the
        // phase on its slider, which covers FEMM 4.2's separate Re and Im.
        m_vectorType->addItem(tr("Current Density (J)"));
        m_vectorType->addItem(tr("Electric Field (E)"));
    } else {
        m_vectorType->addItem(tr("D (displacement)"));
        m_vectorType->addItem(tr("E (electric field)"));
    }
    mLayout->addRow(tr("Vector plot:"), m_vectorType);
    mainLayout->addWidget(modeBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ScalarPrefsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Load current settings
    QSettings settings;
    int defaultDensity = (physicsType == 3) ? 1 : (physicsType == 2) ? 1 : 3; // curr: |V|, heat: T, elec: V
    m_smooth->setChecked(settings.value(m_prefix + "/smooth", true).toBool());
    m_legend->setChecked(settings.value(m_prefix + "/legend", true).toBool());
    m_grey->setChecked(settings.value(m_prefix + "/greyscale", false).toBool());
    m_grid->setChecked(settings.value(m_prefix + "/grid", false).toBool());
    m_snap->setChecked(settings.value(m_prefix + "/snap", false).toBool());
    m_mesh->setChecked(settings.value(m_prefix + "/mesh", false).toBool());
    m_pts->setChecked(settings.value(m_prefix + "/points", true).toBool());
    m_names->setChecked(settings.value(m_prefix + "/names", false).toBool());
    m_showContours->setChecked(settings.value(m_prefix + "/showContours", true).toBool());
    m_showMask->setChecked(settings.value(m_prefix + "/showMask", false).toBool());
    m_numContours->setText(settings.value(m_prefix + "/numContours", "19").toString());
    m_liPoints->setText(settings.value(m_prefix + "/liPoints", "400").toString());
    m_plotPoints->setText(settings.value(m_prefix + "/plotPoints", "1500").toString());
    m_densityType->setCurrentIndex(settings.value(m_prefix + "/densityType", defaultDensity).toInt());
    m_editActionType->setCurrentIndex(settings.value(m_prefix + "/editAction", 0).toInt());
    // A current-flow value saved from the old six-entry list (Re(J), Re(E),
    // Im(J), ...) lands on its field.
    int vt = settings.value(m_prefix + "/vectorType", 0).toInt();
    m_vectorType->setCurrentIndex(vt > 0 ? vectorFieldOf(vt) + 1 : 0);
}

void ScalarPrefsDialog::onModifyColor()
{
    int idx = m_colorList->currentIndex();
    if (idx < 0 || idx >= CI_COUNT) return;
    QColor current = viewColors().c[idx];
    QColor chosen = QColorDialog::getColor(current, this,
        tr("Choose color for: %1").arg(colorName(idx)));
    if (chosen.isValid()) viewColors().c[idx] = chosen;
}

void ScalarPrefsDialog::onResetColors()
{
    if (QMessageBox::question(this, tr("Reset Colors"),
        tr("Reset all colors to defaults?"),
        QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
        viewColors().resetDefaults();
}

void ScalarPrefsDialog::accept()
{
    QSettings settings;
    settings.setValue(m_prefix + "/smooth", m_smooth->isChecked());
    settings.setValue(m_prefix + "/legend", m_legend->isChecked());
    settings.setValue(m_prefix + "/greyscale", m_grey->isChecked());
    settings.setValue(m_prefix + "/grid", m_grid->isChecked());
    settings.setValue(m_prefix + "/snap", m_snap->isChecked());
    settings.setValue(m_prefix + "/mesh", m_mesh->isChecked());
    settings.setValue(m_prefix + "/points", m_pts->isChecked());
    settings.setValue(m_prefix + "/names", m_names->isChecked());
    settings.setValue(m_prefix + "/showContours", m_showContours->isChecked());
    settings.setValue(m_prefix + "/showMask", m_showMask->isChecked());
    settings.setValue(m_prefix + "/numContours", numContours());
    settings.setValue(m_prefix + "/liPoints", lineIntegralPoints());
    settings.setValue(m_prefix + "/plotPoints", plotPoints());
    settings.setValue(m_prefix + "/densityType", m_densityType->currentIndex());
    settings.setValue(m_prefix + "/editAction", m_editActionType->currentIndex());
    settings.setValue(m_prefix + "/vectorType", m_vectorType->currentIndex());
    viewColors().save();
    QDialog::accept();
}

bool ScalarPrefsDialog::smooth() const { return m_smooth->isChecked(); }
bool ScalarPrefsDialog::showLegend() const { return m_legend->isChecked(); }
bool ScalarPrefsDialog::greyscale() const { return m_grey->isChecked(); }
bool ScalarPrefsDialog::showGrid() const { return m_grid->isChecked(); }
bool ScalarPrefsDialog::snapGrid() const { return m_snap->isChecked(); }
bool ScalarPrefsDialog::showMesh() const { return m_mesh->isChecked(); }
bool ScalarPrefsDialog::showPoints() const { return m_pts->isChecked(); }
bool ScalarPrefsDialog::showNames() const { return m_names->isChecked(); }
bool ScalarPrefsDialog::showContours() const { return m_showContours->isChecked(); }
bool ScalarPrefsDialog::showMask() const { return m_showMask->isChecked(); }
int ScalarPrefsDialog::numContours() const { return (int)m_numContours->evaluateAsDouble(19); }
int ScalarPrefsDialog::lineIntegralPoints() const { return (int)m_liPoints->evaluateAsDouble(400); }
int ScalarPrefsDialog::plotPoints() const { return (int)m_plotPoints->evaluateAsDouble(150); }
int ScalarPrefsDialog::densityPlotType() const { return m_densityType->currentIndex(); }
int ScalarPrefsDialog::editAction() const { return m_editActionType->currentIndex(); }
void ScalarPrefsDialog::setSmooth(bool on) { m_smooth->setChecked(on); }
void ScalarPrefsDialog::setShowLegend(bool on) { m_legend->setChecked(on); }
void ScalarPrefsDialog::setShowMesh(bool on) { m_mesh->setChecked(on); }
void ScalarPrefsDialog::setShowPoints(bool on) { m_pts->setChecked(on); }
void ScalarPrefsDialog::setNumContours(int n) { m_numContours->setText(QString::number(n)); }
void ScalarPrefsDialog::setDensityPlotType(int t) { m_densityType->setCurrentIndex(t); }
void ScalarPrefsDialog::setEditAction(int a) { m_editActionType->setCurrentIndex(a); }
