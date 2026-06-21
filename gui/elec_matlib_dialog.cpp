// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_matlib_dialog.cpp — Electrostatics material library (statlib.dat)
// Thin subclass of MatLibDialogBase for EMaterialProp.

#include "elec_matlib_dialog.h"
#include "elec_prop_editors.h"
#include "document.h"
#include "elec_document.h"
#include <cstdio>
#include <cstring>

static ElecDocument *asElec(DocumentBase *doc) { return dynamic_cast<ElecDocument*>(doc); }

static char *StripKey(char *s) { char *p = strchr(s, '='); return p ? p + 1 : s; }
static std::string ExtractQuoted(char *v) {
    char *start = strchr(v, '"'); if (!start) return std::string(v);
    start++; char *end = strrchr(start, '"'); if (end) *end = 0;
    return std::string(start);
}

ElecMatLibDialog::ElecMatLibDialog(DocumentBase *doc, QWidget *parent)
    : MatLibDialogBase(tr("Electrostatics Material Library"), "statlib.dat", parent), m_doc(doc)
{
    m_modelMaterials = asElec(doc)->blockproplist;  // working copy
    initLibrary();
}

std::pair<int, std::string> ElecMatLibDialog::readMaterialBlock(FILE *fp)
{
    char s[1024], q[1024];
    EMaterialProp m;
    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;
        if (_strnicmp(q, "<endblock>", 9) == 0) break;
        char *v;
        if (_strnicmp(q, "<blockname>", 11) == 0) { v = StripKey(s); m.BlockName = ExtractQuoted(v); }
        if (_strnicmp(q, "<ex>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ex); }
        if (_strnicmp(q, "<ey>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ey); }
        if (_strnicmp(q, "<qv>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.qv); }
    }
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return {idx, m.BlockName};
}

void ElecMatLibDialog::writeMaterialBlock(FILE *fp, int libIdx)
{
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    auto &m = m_libMaterials[libIdx];
    fprintf(fp, "<BeginBlock>\n");
    fprintf(fp, "<BlockName> = \"%s\"\n", m.BlockName.c_str());
    fprintf(fp, "<ex> = %.17g\n", m.ex);
    fprintf(fp, "<ey> = %.17g\n", m.ey);
    fprintf(fp, "<qv> = %.17g\n", m.qv);
    fprintf(fp, "<EndBlock>\n\n");
}

int ElecMatLibDialog::modelMaterialCount() const { return (int)m_modelMaterials.size(); }
std::string ElecMatLibDialog::modelMaterialName(int idx) const { return m_modelMaterials[idx].BlockName; }
int ElecMatLibDialog::copyLibToModel(int libIdx) {
    int idx = (int)m_modelMaterials.size();
    m_modelMaterials.push_back(m_libMaterials[libIdx]);
    return idx;
}
std::pair<int, std::string> ElecMatLibDialog::copyModelToLib(int modelIdx) {
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m_modelMaterials[modelIdx]);
    return {idx, m_modelMaterials[modelIdx].BlockName};
}
void ElecMatLibDialog::commitModel(const std::vector<int> &order) {
    auto &bp = asElec(m_doc)->blockproplist;
    bp.clear();
    for (int idx : order)
        if (idx >= 0 && idx < (int)m_modelMaterials.size())
            bp.push_back(m_modelMaterials[idx]);
}
std::string ElecMatLibDialog::libMaterialName(int idx) const { return m_libMaterials[idx].BlockName; }
QString ElecMatLibDialog::modelFileFilter() const { return tr("FEMM Electrostatics (*.fee);;All Files (*)"); }

int ElecMatLibDialog::addNewMaterial(QWidget *parent) {
    ElecMaterialEditorDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return -1;
    EMaterialProp m; dlg.toProp(m);
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return idx;
}

void ElecMatLibDialog::editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    ElecMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_libMaterials[libIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_libMaterials[libIdx]);
        item->setText(0, QString::fromStdString(m_libMaterials[libIdx].BlockName));
    }
}

void ElecMatLibDialog::editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (modelIdx < 0 || modelIdx >= (int)m_modelMaterials.size()) return;
    ElecMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_modelMaterials[modelIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_modelMaterials[modelIdx]);
        item->setText(0, QString::fromStdString(m_modelMaterials[modelIdx].BlockName));
    }
}
