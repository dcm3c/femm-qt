// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// heat_matlib_dialog.cpp — Heat flow material library (heatlib.dat)
// Thin subclass of MatLibDialogBase for HMaterialProp.

#include "heat_matlib_dialog.h"
#include "heat_prop_editors.h"
#include "document.h"
#include "heat_document.h"
#include <cstdio>
#include <cstring>

static HeatDocument *asHeat(DocumentBase *doc) { return dynamic_cast<HeatDocument*>(doc); }

static char *StripKey(char *s) { char *p = strchr(s, '='); return p ? p + 1 : s; }
static std::string ExtractQuoted(char *v) {
    char *start = strchr(v, '"'); if (!start) return std::string(v);
    start++; char *end = strrchr(start, '"'); if (end) *end = 0;
    return std::string(start);
}

HeatMatLibDialog::HeatMatLibDialog(DocumentBase *doc, QWidget *parent)
    : MatLibDialogBase(tr("Heat Flow Material Library"), "heatlib.dat", parent), m_doc(doc)
{
    m_modelMaterials = asHeat(doc)->blockproplist;  // working copy
    initLibrary();
}

std::pair<int, std::string> HeatMatLibDialog::readMaterialBlock(FILE *fp)
{
    char s[1024], q[1024];
    HMaterialProp m;
    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;
        if (_strnicmp(q, "<endblock>", 9) == 0) break;
        char *v;
        if (_strnicmp(q, "<blockname>", 11) == 0) { v = StripKey(s); m.BlockName = ExtractQuoted(v); }
        if (_strnicmp(q, "<Kx>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Kx); }
        if (_strnicmp(q, "<Ky>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Ky); }
        if (_strnicmp(q, "<Kt>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Kt); }
        if (_strnicmp(q, "<qv>", 4) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.qv); }
        if (_strnicmp(q, "<TKPoints>", 10) == 0) {
            v = StripKey(s);
            sscanf(v, "%i", &m.npts);
            if (m.npts < 0) m.npts = 0;
            if (m.npts > 128) m.npts = 128;
            for (int j = 0; j < m.npts; j++) {
                if (fgets(s, 1024, fp) != NULL) {
                    double t = 0, k = 0;
                    sscanf(s, "%lf\t%lf", &t, &k);
                    m.Kn[j] = CComplex(t, k);
                }
            }
        }
    }
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return {idx, m.BlockName};
}

void HeatMatLibDialog::writeMaterialBlock(FILE *fp, int libIdx)
{
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    auto &m = m_libMaterials[libIdx];
    fprintf(fp, "<BeginBlock>\n");
    fprintf(fp, "<BlockName> = \"%s\"\n", m.BlockName.c_str());
    fprintf(fp, "<Kx> = %.17g\n", m.Kx);
    fprintf(fp, "<Ky> = %.17g\n", m.Ky);
    fprintf(fp, "<Kt> = %.17g\n", m.Kt);
    fprintf(fp, "<qv> = %.17g\n", m.qv);
    fprintf(fp, "<TKPoints> = %i\n", m.npts);
    for (int j = 0; j < m.npts; j++) {
        fprintf(fp, "%.17g\t%.17g\n", m.Kn[j].re, m.Kn[j].im);
    }
    fprintf(fp, "<EndBlock>\n\n");
}

int HeatMatLibDialog::modelMaterialCount() const { return (int)m_modelMaterials.size(); }
std::string HeatMatLibDialog::modelMaterialName(int idx) const { return m_modelMaterials[idx].BlockName; }
int HeatMatLibDialog::copyLibToModel(int libIdx) {
    int idx = (int)m_modelMaterials.size();
    m_modelMaterials.push_back(m_libMaterials[libIdx]);
    return idx;
}
std::pair<int, std::string> HeatMatLibDialog::copyModelToLib(int modelIdx) {
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m_modelMaterials[modelIdx]);
    return {idx, m_modelMaterials[modelIdx].BlockName};
}
void HeatMatLibDialog::commitModel(const std::vector<int> &order) {
    auto &bp = asHeat(m_doc)->blockproplist;
    bp.clear();
    for (int idx : order)
        if (idx >= 0 && idx < (int)m_modelMaterials.size())
            bp.push_back(m_modelMaterials[idx]);
}
std::string HeatMatLibDialog::libMaterialName(int idx) const { return m_libMaterials[idx].BlockName; }
QString HeatMatLibDialog::modelFileFilter() const { return tr("FEMM Heat Flow (*.feh);;All Files (*)"); }

int HeatMatLibDialog::addNewMaterial(QWidget *parent) {
    HeatMaterialEditorDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return -1;
    HMaterialProp m; dlg.toProp(m);
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return idx;
}

void HeatMatLibDialog::editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    HeatMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_libMaterials[libIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_libMaterials[libIdx]);
        item->setText(0, QString::fromStdString(m_libMaterials[libIdx].BlockName));
    }
}

void HeatMatLibDialog::editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (modelIdx < 0 || modelIdx >= (int)m_modelMaterials.size()) return;
    HeatMaterialEditorDialog dlg(parent);
    dlg.fromProp(m_modelMaterials[modelIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        dlg.toProp(m_modelMaterials[modelIdx]);
        item->setText(0, QString::fromStdString(m_modelMaterials[modelIdx].BlockName));
    }
}
