// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// pre_prefs_dialog.cpp — Preprocessor preferences dialog

#include "pre_prefs_dialog.h"
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

// Color names for the preprocessor subset
static const int preColorIndices[] = {
    CI_Selected, CI_Background, CI_Mesh, CI_Block, CI_Line,
    CI_Grid, CI_Node, CI_Name
};
static const int numPreColors = sizeof(preColorIndices) / sizeof(preColorIndices[0]);

PrePrefsDialog::PrePrefsDialog(int physicsType, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Preprocessor Preferences"));

    auto *mainLayout = new QVBoxLayout(this);

    // Colors
    auto *colorBox = new QGroupBox(tr("Colors"));
    auto *colorLayout = new QHBoxLayout(colorBox);
    m_colorList = new QComboBox;
    for (int i = 0; i < numPreColors; i++)
        m_colorList->addItem(colorName(preColorIndices[i]));
    auto *modifyBtn = new QPushButton(tr("Modify..."));
    auto *resetBtn = new QPushButton(tr("Reset All"));
    colorLayout->addWidget(m_colorList, 1);
    colorLayout->addWidget(modifyBtn);
    colorLayout->addWidget(resetBtn);
    connect(modifyBtn, &QPushButton::clicked, this, &PrePrefsDialog::onModifyColor);
    connect(resetBtn, &QPushButton::clicked, this, &PrePrefsDialog::onResetColors);
    mainLayout->addWidget(colorBox);

    // Display options
    auto *displayBox = new QGroupBox(tr("Display"));
    auto *dLayout = new QFormLayout(displayBox);
    m_showGrid = new QCheckBox(tr("Show Grid"));
    m_snapGrid = new QCheckBox(tr("Snap to Grid"));
    m_showNames = new QCheckBox(tr("Show Block Names"));
    dLayout->addRow(m_showGrid);
    dLayout->addRow(m_snapGrid);
    dLayout->addRow(m_showNames);
    mainLayout->addWidget(displayBox);

    // Default problem settings
    auto *defaultsBox = new QGroupBox(tr("Defaults for New Documents"));
    auto *dfLayout = new QFormLayout(defaultsBox);

    m_lengthUnits = new QComboBox;
    m_lengthUnits->addItem(tr("Inches"));
    m_lengthUnits->addItem(tr("Millimeters"));
    m_lengthUnits->addItem(tr("Centimeters"));
    m_lengthUnits->addItem(tr("Meters"));
    m_lengthUnits->addItem(tr("Mils"));
    m_lengthUnits->addItem(tr("Micrometers"));
    dfLayout->addRow(tr("Length Units:"), m_lengthUnits);

    m_problemType = new QComboBox;
    m_problemType->addItem(tr("Planar"));
    m_problemType->addItem(tr("Axisymmetric"));
    dfLayout->addRow(tr("Problem Type:"), m_problemType);

    m_precision = new PyLineEdit;
    dfLayout->addRow(tr("Precision:"), m_precision);

    m_minAngle = new PyLineEdit;
    dfLayout->addRow(tr("Min Angle (deg):"), m_minAngle);

    m_depth = new PyLineEdit;
    dfLayout->addRow(tr("Depth:"), m_depth);

    m_frequency = new PyLineEdit;
    if (physicsType == 0 || physicsType == 3) {
        dfLayout->addRow(tr("Frequency (Hz):"), m_frequency);
    } else {
        m_frequency->hide();
    }

    m_smartMesh = new QCheckBox(tr("Smart Mesh"));
    dfLayout->addRow(m_smartMesh);

    mainLayout->addWidget(defaultsBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PrePrefsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    // Load current settings
    QSettings settings;
    m_showGrid->setChecked(settings.value("editor/showGrid", true).toBool());
    m_snapGrid->setChecked(settings.value("editor/snapToGrid", false).toBool());
    m_showNames->setChecked(settings.value("editor/showNames", true).toBool());
    m_lengthUnits->setCurrentIndex(settings.value("defaults/lengthUnits", 1).toInt());
    m_problemType->setCurrentIndex(settings.value("defaults/problemType", 0).toInt());
    m_precision->setText(settings.value("defaults/precision", "1e-8").toString());
    m_minAngle->setText(settings.value("defaults/minAngle", "30").toString());
    m_depth->setText(settings.value("defaults/depth", "1").toString());
    m_smartMesh->setChecked(settings.value("defaults/smartMesh", true).toBool());
    double defFreq = (physicsType == 3) ? 60.0 : 0.0;
    m_frequency->setText(settings.value("defaults/frequency", QString::number(defFreq)).toString());
}

void PrePrefsDialog::onModifyColor()
{
    int listIdx = m_colorList->currentIndex();
    if (listIdx < 0 || listIdx >= numPreColors) return;
    int colorIdx = preColorIndices[listIdx];

    QColor current = viewColors().c[colorIdx];
    QColor chosen = QColorDialog::getColor(current, this,
        tr("Choose color for: %1").arg(colorName(colorIdx)));
    if (chosen.isValid())
        viewColors().c[colorIdx] = chosen;
}

void PrePrefsDialog::onResetColors()
{
    if (QMessageBox::question(this, tr("Reset Colors"),
        tr("Reset preprocessor colors to defaults?"),
        QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok)
    {
        viewColors().resetDefaults();
    }
}

void PrePrefsDialog::accept()
{
    QSettings settings;
    settings.setValue("editor/showGrid", m_showGrid->isChecked());
    settings.setValue("editor/snapToGrid", m_snapGrid->isChecked());
    settings.setValue("editor/showNames", m_showNames->isChecked());
    settings.setValue("defaults/lengthUnits", m_lengthUnits->currentIndex());
    settings.setValue("defaults/problemType", m_problemType->currentIndex());
    settings.setValue("defaults/precision", precision());
    settings.setValue("defaults/minAngle", minAngle());
    settings.setValue("defaults/depth", depth());
    settings.setValue("defaults/smartMesh", m_smartMesh->isChecked());
    if (m_frequency->isVisible())
        settings.setValue("defaults/frequency", frequency());
    viewColors().save();
    QDialog::accept();
}

bool PrePrefsDialog::showGrid() const { return m_showGrid->isChecked(); }
bool PrePrefsDialog::snapGrid() const { return m_snapGrid->isChecked(); }
bool PrePrefsDialog::showNames() const { return m_showNames->isChecked(); }
bool PrePrefsDialog::smartMesh() const { return m_smartMesh->isChecked(); }
double PrePrefsDialog::precision() const { return m_precision->evaluateAsDouble(1e-8); }
double PrePrefsDialog::minAngle() const { return m_minAngle->evaluateAsDouble(30); }
double PrePrefsDialog::depth() const { return m_depth->evaluateAsDouble(1); }
int PrePrefsDialog::lengthUnits() const { return m_lengthUnits->currentIndex(); }
int PrePrefsDialog::problemType() const { return m_problemType->currentIndex(); }
void PrePrefsDialog::setShowGrid(bool on) { m_showGrid->setChecked(on); }
void PrePrefsDialog::setSnapGrid(bool on) { m_snapGrid->setChecked(on); }
void PrePrefsDialog::setShowNames(bool on) { m_showNames->setChecked(on); }
void PrePrefsDialog::setSmartMesh(bool on) { m_smartMesh->setChecked(on); }
void PrePrefsDialog::setPrecision(double p) { m_precision->setText(QString::number(p, 'g', 15)); }
void PrePrefsDialog::setMinAngle(double a) { m_minAngle->setText(QString::number(a, 'g', 15)); }
void PrePrefsDialog::setDepth(double d) { m_depth->setText(QString::number(d, 'g', 15)); }
void PrePrefsDialog::setLengthUnits(int u) { m_lengthUnits->setCurrentIndex(u); }
void PrePrefsDialog::setProblemType(int t) { m_problemType->setCurrentIndex(t); }
double PrePrefsDialog::frequency() const { return m_frequency->evaluateAsDouble(0); }
void PrePrefsDialog::setFrequency(double f) { m_frequency->setText(QString::number(f, 'g', 15)); }
