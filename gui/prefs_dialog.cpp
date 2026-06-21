// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prefs_dialog.cpp — View preferences dialog matching original FEMM
// Ported from femm42src/femm/viewpref.cpp
// Color picker, display toggles, numeric settings, mode selectors.

#include "prefs_dialog.h"
#include "view_colors.h"
#include "py_line_edit.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QColorDialog>
#include <QMessageBox>
#include <QSettings>

PrefsDialog::PrefsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    auto *mainLayout = new QVBoxLayout(this);

    // --- Color picker ---
    auto *colorBox = new QGroupBox(tr("Colors"));
    auto *colorLayout = new QHBoxLayout(colorBox);
    m_colorList = new QComboBox;
    for (int i = 0; i < CI_COUNT; i++)
        m_colorList->addItem(colorName(i));
    m_modifyBtn = new QPushButton(tr("Modify..."));
    auto *resetBtn = new QPushButton(tr("Reset All"));
    colorLayout->addWidget(m_colorList, 1);
    colorLayout->addWidget(m_modifyBtn);
    colorLayout->addWidget(resetBtn);
    connect(m_modifyBtn, &QPushButton::clicked, this, &PrefsDialog::onModifyColor);
    connect(resetBtn, &QPushButton::clicked, this, &PrefsDialog::onResetColors);
    mainLayout->addWidget(colorBox);

    // --- Display options ---
    auto *displayBox = new QGroupBox(tr("Display"));
    auto *dLayout = new QVBoxLayout(displayBox);
    auto *dRow1 = new QHBoxLayout;
    auto *dRow2 = new QHBoxLayout;
    m_smooth = new QCheckBox(tr("Smoothing"));
    m_legend = new QCheckBox(tr("Show Legend"));
    m_grey = new QCheckBox(tr("Greyscale"));
    m_shiftH = new QCheckBox(tr("Shift H (PM)"));
    dRow1->addWidget(m_smooth); dRow1->addWidget(m_legend);
    dRow1->addWidget(m_grey); dRow1->addWidget(m_shiftH);
    m_grid = new QCheckBox(tr("Grid"));
    m_snap = new QCheckBox(tr("Snap"));
    m_mesh = new QCheckBox(tr("Mesh"));
    m_pts = new QCheckBox(tr("Points"));
    m_names = new QCheckBox(tr("Names"));
    dRow2->addWidget(m_grid); dRow2->addWidget(m_snap);
    dRow2->addWidget(m_mesh);
    dRow2->addWidget(m_pts); dRow2->addWidget(m_names);
    dLayout->addLayout(dRow1);
    dLayout->addLayout(dRow2);

    auto *dRow3 = new QHBoxLayout;
    m_showAr = new QCheckBox(tr("Real A Contours"));
    m_showAi = new QCheckBox(tr("Imag A Contours"));
    m_showMask = new QCheckBox(tr("Show Mask"));
    dRow3->addWidget(m_showAr); dRow3->addWidget(m_showAi);
    dRow3->addWidget(m_showMask);
    dLayout->addLayout(dRow3);
    mainLayout->addWidget(displayBox);

    // --- Numeric settings ---
    auto *numBox = new QGroupBox(tr("Numeric"));
    auto *numForm = new QFormLayout(numBox);
    m_numContours = new PyLineEdit;
    numForm->addRow(tr("Number of contours:"), m_numContours);
    m_liPoints = new PyLineEdit;
    numForm->addRow(tr("Line integral points:"), m_liPoints);
    m_plotPoints = new PyLineEdit;
    numForm->addRow(tr("Plot points:"), m_plotPoints);
    mainLayout->addWidget(numBox);

    // --- Mode selectors ---
    auto *modeBox = new QGroupBox(tr("Default Modes"));
    auto *modeForm = new QFormLayout(modeBox);
    m_densityType = new QComboBox;
    m_densityType->addItem(tr("No density plot"));
    m_densityType->addItem(tr("|B| (magnitude)"));
    m_densityType->addItem(tr("|H| (magnitude)"));
    m_densityType->addItem(tr("|J| (current density)"));
    m_densityType->addItem(tr("log |B|"));
    modeForm->addRow(tr("Density plot:"), m_densityType);
    m_editActionType = new QComboBox;
    m_editActionType->addItem(tr("Point values"));
    m_editActionType->addItem(tr("Contour"));
    m_editActionType->addItem(tr("Area"));
    modeForm->addRow(tr("Edit action:"), m_editActionType);
    m_vectorType = new QComboBox;
    m_vectorType->addItem(tr("Off"));
    m_vectorType->addItem(tr("Flux Density (B)"));
    m_vectorType->addItem(tr("Field Intensity (H)"));
    modeForm->addRow(tr("Vector plot:"), m_vectorType);
    mainLayout->addWidget(modeBox);

    // --- OK / Cancel ---
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PrefsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Load current settings
    QSettings settings;
    m_smooth->setChecked(settings.value("pp/smooth", true).toBool());
    m_legend->setChecked(settings.value("pp/legend", true).toBool());
    m_grey->setChecked(settings.value("pp/greyscale", false).toBool());
    m_grid->setChecked(settings.value("pp/grid", false).toBool());
    m_snap->setChecked(settings.value("pp/snap", false).toBool());
    m_mesh->setChecked(settings.value("pp/mesh", false).toBool());
    m_pts->setChecked(settings.value("pp/points", true).toBool());
    m_names->setChecked(settings.value("pp/names", false).toBool());
    m_showAr->setChecked(settings.value("pp/showAr", true).toBool());
    m_showAi->setChecked(settings.value("pp/showAi", false).toBool());
    m_showMask->setChecked(settings.value("pp/showMask", false).toBool());
    m_shiftH->setChecked(settings.value("pp/shiftH", true).toBool());
    m_numContours->setText(settings.value("pp/numContours", "19").toString());
    m_liPoints->setText(settings.value("pp/liPoints", "400").toString());
    m_plotPoints->setText(settings.value("pp/plotPoints", "1500").toString());
    m_densityType->setCurrentIndex(settings.value("pp/densityType", 1).toInt());
    m_editActionType->setCurrentIndex(settings.value("pp/editAction", 0).toInt());
    m_vectorType->setCurrentIndex(settings.value("pp/vectorType", 0).toInt());
}

void PrefsDialog::onModifyColor()
{
    int idx = m_colorList->currentIndex();
    if (idx < 0 || idx >= CI_COUNT) return;

    QColor current = viewColors().c[idx];
    QColor chosen = QColorDialog::getColor(current, this,
        tr("Choose color for: %1").arg(colorName(idx)));
    if (chosen.isValid())
        viewColors().c[idx] = chosen;
}

void PrefsDialog::onResetColors()
{
    if (QMessageBox::question(this, tr("Reset Colors"),
        tr("Reset all color preferences to defaults?"),
        QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
    {
        viewColors().resetDefaults();
    }
}

void PrefsDialog::accept()
{
    // Save all settings
    QSettings settings;
    settings.setValue("pp/smooth", m_smooth->isChecked());
    settings.setValue("pp/legend", m_legend->isChecked());
    settings.setValue("pp/greyscale", m_grey->isChecked());
    settings.setValue("pp/grid", m_grid->isChecked());
    settings.setValue("pp/snap", m_snap->isChecked());
    settings.setValue("pp/mesh", m_mesh->isChecked());
    settings.setValue("pp/points", m_pts->isChecked());
    settings.setValue("pp/names", m_names->isChecked());
    settings.setValue("pp/showAr", m_showAr->isChecked());
    settings.setValue("pp/showAi", m_showAi->isChecked());
    settings.setValue("pp/showMask", m_showMask->isChecked());
    settings.setValue("pp/shiftH", m_shiftH->isChecked());
    settings.setValue("pp/numContours", numContours());
    settings.setValue("pp/liPoints", lineIntegralPoints());
    settings.setValue("pp/plotPoints", plotPoints());
    settings.setValue("pp/densityType", m_densityType->currentIndex());
    settings.setValue("pp/editAction", m_editActionType->currentIndex());
    settings.setValue("pp/vectorType", m_vectorType->currentIndex());

    // Save colors
    viewColors().save();

    QDialog::accept();
}

// Accessors
bool PrefsDialog::smooth() const { return m_smooth->isChecked(); }
bool PrefsDialog::showLegend() const { return m_legend->isChecked(); }
bool PrefsDialog::greyscale() const { return m_grey->isChecked(); }
bool PrefsDialog::showGrid() const { return m_grid->isChecked(); }
bool PrefsDialog::snapGrid() const { return m_snap->isChecked(); }
bool PrefsDialog::showMesh() const { return m_mesh->isChecked(); }
bool PrefsDialog::showPoints() const { return m_pts->isChecked(); }
bool PrefsDialog::showNames() const { return m_names->isChecked(); }
bool PrefsDialog::showRealContours() const { return m_showAr->isChecked(); }
bool PrefsDialog::showImagContours() const { return m_showAi->isChecked(); }
bool PrefsDialog::shiftH() const { return m_shiftH->isChecked(); }
bool PrefsDialog::showMask() const { return m_showMask->isChecked(); }
int PrefsDialog::densityPlotType() const { return m_densityType->currentIndex(); }
int PrefsDialog::editAction() const { return m_editActionType->currentIndex(); }

int PrefsDialog::numContours() const {
    int n = (int)m_numContours->evaluateAsDouble(19);
    return qBound(4, n, 999);
}
int PrefsDialog::lineIntegralPoints() const {
    int n = (int)m_liPoints->evaluateAsDouble(400);
    return qBound(10, n, 100000);
}
int PrefsDialog::plotPoints() const {
    int n = (int)m_plotPoints->evaluateAsDouble(1500);
    return qBound(10, n, 100000);
}

void PrefsDialog::setSmooth(bool on) { m_smooth->setChecked(on); }
void PrefsDialog::setShowLegend(bool on) { m_legend->setChecked(on); }
void PrefsDialog::setShowMesh(bool on) { m_mesh->setChecked(on); }
void PrefsDialog::setNumContours(int n) { m_numContours->setText(QString::number(n)); }
void PrefsDialog::setDensityPlotType(int t) { m_densityType->setCurrentIndex(t); }
void PrefsDialog::setEditAction(int a) { m_editActionType->setCurrentIndex(a); }
