// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// curr_matlib_dialog.cpp — Current flow material library (condlib.dat)
// Thin subclass of MatLibDialogBase for CurrMaterialProp.

#include "curr_matlib_dialog.h"
#include "curr_prop_editors.h"
#include "document.h"
#include "curr_document.h"
#include <cstdio>
#include <cstring>

static CurrDocument *asCurr(DocumentBase *doc) { return dynamic_cast<CurrDocument*>(doc); }

static char *StripKey(char *s) { char *p = strchr(s, '='); return p ? p + 1 : s; }
static std::string ExtractQuoted(char *v) {
    char *start = strchr(v, '"'); if (!start) return std::string(v);
    start++; char *end = strrchr(start, '"'); if (end) *end = 0;
    return std::string(start);
}

CurrMatLibDialog::CurrMatLibDialog(DocumentBase *doc, QWidget *parent)
    : MatLibDialogBase(tr("Current Flow Material Library"), "condlib.dat", parent), m_doc(doc)
{
    m_modelMaterials = asCurr(doc)->blockproplist;  // working copy
    initLibrary();
}

std::pair<int, std::string> CurrMatLibDialog::readMaterialBlock(FILE *fp)
{
    char s[1024], q[1024];
    CurrMaterialProp m;
    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;
        if (_strnicmp(q, "<endblock>", 9) == 0) break;
        char *v;
        if (_strnicmp(q, "<blockname>", 11) == 0) { v = StripKey(s); m.BlockName = ExtractQuoted(v); }
        if (_strnicmp(q, "<ox>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ox); }
        if (_strnicmp(q, "<oy>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.oy); }
        if (_strnicmp(q, "<ex>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ex); }
        if (_strnicmp(q, "<ey>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ey); }
        if (_strnicmp(q, "<ltx>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.ltx); }
        if (_strnicmp(q, "<lty>", 5) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.lty); }
    }
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return {idx, m.BlockName};
}

void CurrMatLibDialog::writeMaterialBlock(FILE *fp, int libIdx)
{
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    auto &m = m_libMaterials[libIdx];
    fprintf(fp, "<BeginBlock>\n");
    fprintf(fp, "<BlockName> = \"%s\"\n", m.BlockName.c_str());
    fprintf(fp, "<ox> = %.17g\n", m.ox);
    fprintf(fp, "<oy> = %.17g\n", m.oy);
    fprintf(fp, "<ex> = %.17g\n", m.ex);
    fprintf(fp, "<ey> = %.17g\n", m.ey);
    fprintf(fp, "<ltx> = %.17g\n", m.ltx);
    fprintf(fp, "<lty> = %.17g\n", m.lty);
    fprintf(fp, "<EndBlock>\n\n");
}

int CurrMatLibDialog::modelMaterialCount() const { return (int)m_modelMaterials.size(); }
std::string CurrMatLibDialog::modelMaterialName(int idx) const { return m_modelMaterials[idx].BlockName; }
int CurrMatLibDialog::copyLibToModel(int libIdx) {
    int idx = (int)m_modelMaterials.size();
    m_modelMaterials.push_back(m_libMaterials[libIdx]);
    return idx;
}
std::pair<int, std::string> CurrMatLibDialog::copyModelToLib(int modelIdx) {
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m_modelMaterials[modelIdx]);
    return {idx, m_modelMaterials[modelIdx].BlockName};
}
void CurrMatLibDialog::commitModel(const std::vector<int> &order) {
    auto &bp = asCurr(m_doc)->blockproplist;
    bp.clear();
    for (int idx : order)
        if (idx >= 0 && idx < (int)m_modelMaterials.size())
            bp.push_back(m_modelMaterials[idx]);
}
std::string CurrMatLibDialog::libMaterialName(int idx) const { return m_libMaterials[idx].BlockName; }
QString CurrMatLibDialog::modelFileFilter() const { return tr("FEMM Current Flow (*.fec);;All Files (*)"); }

int CurrMatLibDialog::addNewMaterial(QWidget *parent) {
    CurrMaterialEditorDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return -1;
    CurrMaterialProp m; dlg.toProp(m);
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return idx;
}

void CurrMatLibDialog::editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    CurrMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_libMaterials[libIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_libMaterials[libIdx]);
        item->setText(0, QString::fromStdString(m_libMaterials[libIdx].BlockName));
    }
}

void CurrMatLibDialog::editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (modelIdx < 0 || modelIdx >= (int)m_modelMaterials.size()) return;
    CurrMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_modelMaterials[modelIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_modelMaterials[modelIdx]);
        item->setText(0, QString::fromStdString(m_modelMaterials[modelIdx].BlockName));
    }
}
