// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_dialogs.h — Property dialogs for FEMM geometry entities
// Ported from femm42src/femm OpBlkDlg, OpSegDlg, OpArcSegDlg, OpNodeDlg, OpGrp
#ifndef PROP_DIALOGS_H
#define PROP_DIALOGS_H

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <vector>
#include <string>

class PyLineEdit;
class DocumentBase;

// ================================================================
// Group dialog — sets group number for all selected entities
// ================================================================
class GroupDialog : public QDialog {
    Q_OBJECT
public:
    GroupDialog(QWidget *parent = nullptr);
    int group() const;
    void setGroup(int g);
private:
    PyLineEdit *m_group;
};

// ================================================================
// Node properties dialog — boundary marker + group
// ================================================================
class NodePropDialog : public QDialog {
    Q_OBJECT
public:
    NodePropDialog(DocumentBase *doc, QWidget *parent = nullptr);
    int selectedProp() const { return m_combo->currentIndex(); }
    int selectedConductor() const { return m_condCombo ? m_condCombo->currentIndex() : 0; }
    int group() const;
    double lfs() const;
    void setSelectedProp(int idx) { m_combo->setCurrentIndex(idx); }
    void setSelectedConductor(int idx) { if (m_condCombo) m_condCombo->setCurrentIndex(idx); }
    void setGroup(int g);
    void setLfs(double v);
private slots:
    void onAutoMeshToggled(bool on);
private:
    QComboBox *m_combo;
    QComboBox *m_condCombo = nullptr;
    PyLineEdit *m_group;
    QCheckBox *m_autoMesh;
    PyLineEdit *m_lfs;
};

// ================================================================
// Segment properties dialog — boundary, mesh size, automesh, hide, group
// ================================================================
class SegmentPropDialog : public QDialog {
    Q_OBJECT
public:
    SegmentPropDialog(DocumentBase *doc, QWidget *parent = nullptr);
    int selectedProp() const { return m_combo->currentIndex(); }
    int selectedConductor() const { return m_condCombo ? m_condCombo->currentIndex() : 0; }
    double meshSize() const;
    bool autoMesh() const;
    bool hidden() const;
    int group() const;
    void setSelectedProp(int idx) { m_combo->setCurrentIndex(idx); }
    void setSelectedConductor(int idx) { if (m_condCombo) m_condCombo->setCurrentIndex(idx); }
    void setMeshSize(double s);
    void setAutoMesh(bool on);
    void setHidden(bool h);
    void setGroup(int g);
private slots:
    void onAutoMeshToggled(bool on);
private:
    QComboBox *m_combo;
    QComboBox *m_condCombo = nullptr;  // conductor (electrostatics/heat/current)
    PyLineEdit *m_meshSize;
    QCheckBox *m_autoMesh;
    QCheckBox *m_hide;
    PyLineEdit *m_group;
};

// ================================================================
// Arc segment properties dialog — boundary, max angle, hide, group
// ================================================================
class ArcPropDialog : public QDialog {
    Q_OBJECT
public:
    ArcPropDialog(DocumentBase *doc, QWidget *parent = nullptr);
    int selectedProp() const { return m_combo->currentIndex(); }
    int selectedConductor() const { return m_condCombo ? m_condCombo->currentIndex() : 0; }
    double maxSegAngle() const;
    bool hidden() const;
    int group() const;
    void setSelectedProp(int idx) { m_combo->setCurrentIndex(idx); }
    void setSelectedConductor(int idx) { if (m_condCombo) m_condCombo->setCurrentIndex(idx); }
    void setMaxSegAngle(double a);
    void setHidden(bool h);
    void setGroup(int g);
private:
    QComboBox *m_combo;
    QComboBox *m_condCombo = nullptr;
    PyLineEdit *m_maxSeg;
    QCheckBox *m_hide;
    PyLineEdit *m_group;
};

// ================================================================
// Block label properties dialog — material, circuit, mesh, magdir, turns, group
// ================================================================
class BlockPropDialog : public QDialog {
    Q_OBJECT
public:
    BlockPropDialog(DocumentBase *doc, QWidget *parent = nullptr);
    int selectedMaterial() const { return m_matCombo->currentIndex(); }
    int selectedCircuit() const { return m_circCombo->currentIndex(); }
    double meshSize() const;
    bool autoMesh() const;
    double magDir() const;
    QString magDirFctn() const;
    int turns() const;
    int group() const;
    bool isExternal() const;
    bool isDefault() const;

    void setSelectedMaterial(int idx) { m_matCombo->setCurrentIndex(idx); }
    void setSelectedCircuit(int idx);
    void setMeshSize(double s);
    void setAutoMesh(bool on);
    void setMagDir(double d);
    void setMagDirFctn(const QString &f);
    void setTurns(int t);
    void setGroup(int g);
    void setExternal(bool e);
    void setDefault(bool d);
    void setProblemType(int pt);

private slots:
    void onAutoMeshToggled(bool on);
    void onMaterialChanged(int idx);
    void onCircuitChanged(int idx);

private:
    DocumentBase *m_doc;
    QComboBox *m_matCombo;
    QComboBox *m_circCombo;
    PyLineEdit *m_meshSize;
    QCheckBox *m_autoMesh;
    PyLineEdit *m_magDir;
    PyLineEdit *m_turns;
    PyLineEdit *m_group;
    QCheckBox *m_external;
    QCheckBox *m_default;
    QLabel *m_magDirLabel;
    QLabel *m_turnsLabel;
    int m_problemType = 0;
};

#endif // PROP_DIALOGS_H
