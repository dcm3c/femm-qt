// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// matlib_dialog.cpp — Magnetics material library (matlib.dat)
// Thin subclass of MatLibDialogBase for FMaterialProp.

#include "matlib_dialog.h"
#include "prop_editors.h"
#include "document.h"
#include "mag_document.h"
#include <cstdio>
#include <cstring>

static MagDocument *asMag(DocumentBase *doc) { return dynamic_cast<MagDocument*>(doc); }

static char *StripKey(char *s) { char *p = strchr(s, '='); return p ? p + 1 : s; }
static std::string ExtractQuoted(char *v) {
    char *start = strchr(v, '"'); if (!start) return std::string(v);
    start++; char *end = strrchr(start, '"'); if (end) *end = 0;
    return std::string(start);
}

MatLibDialog::MatLibDialog(DocumentBase *doc, QWidget *parent)
    : MatLibDialogBase(tr("Material Library"), "matlib.dat", parent), m_doc(doc)
{
    m_modelMaterials = asMag(doc)->blockproplist;  // working copy
    initLibrary();
}

std::pair<int, std::string> MatLibDialog::readMaterialBlock(FILE *fp)
{
    char s[1024], q[1024];
    FMaterialProp m;
    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;
        if (_strnicmp(q, "<endblock>", 9) == 0) break;
        char *v;
        if (_strnicmp(q, "<blockname>", 11) == 0) { v = StripKey(s); m.BlockName = ExtractQuoted(v); }
        if (_strnicmp(q, "<Mu_x>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.mu_x); }
        if (_strnicmp(q, "<Mu_y>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.mu_y); }
        if (_strnicmp(q, "<H_c>", 5) == 0 && _strnicmp(q, "<H_cAngle>", 10) != 0) { v = StripKey(s); sscanf(v, "%lf", &m.H_c); }
        if (_strnicmp(q, "<H_cAngle>", 10) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Theta_m); }
        if (_strnicmp(q, "<J_re>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Jsrc.re); }
        if (_strnicmp(q, "<J_im>", 6) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Jsrc.im); }
        if (_strnicmp(q, "<Sigma>", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Cduct); }
        if (_strnicmp(q, "<d_lam>", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Lam_d); }
        if (_strnicmp(q, "<Phi_h>", 7) == 0 && _strnicmp(q, "<Phi_hx>", 8) != 0 && _strnicmp(q, "<Phi_hy>", 8) != 0) { v = StripKey(s); sscanf(v, "%lf", &m.Theta_hn); }
        if (_strnicmp(q, "<Phi_hx>", 8) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Theta_hx); }
        if (_strnicmp(q, "<Phi_hy>", 8) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.Theta_hy); }
        if (_strnicmp(q, "<LamType>", 9) == 0) { v = StripKey(s); sscanf(v, "%i", &m.LamType); }
        if (_strnicmp(q, "<LamFill>", 9) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.LamFill); }
        if (_strnicmp(q, "<NStrands>", 10) == 0) { v = StripKey(s); sscanf(v, "%i", &m.NStrands); }
        if (_strnicmp(q, "<WireD>", 7) == 0) { v = StripKey(s); sscanf(v, "%lf", &m.WireD); }
        if (_strnicmp(q, "<BHPoints>", 10) == 0) {
            v = StripKey(s); sscanf(v, "%i", &m.BHpoints);
            m.BHdata.resize(m.BHpoints);
            for (int j = 0; j < m.BHpoints; j++) { fgets(s, 1024, fp); sscanf(s, "%lf %lf", &m.BHdata[j].re, &m.BHdata[j].im); }
        }
    }
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return {idx, m.BlockName};
}

void MatLibDialog::writeMaterialBlock(FILE *fp, int libIdx)
{
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    auto &m = m_libMaterials[libIdx];
    fprintf(fp, "<BeginBlock>\n");
    fprintf(fp, "<BlockName> = \"%s\"\n", m.BlockName.c_str());
    fprintf(fp, "<Mu_x> = %.17g\n", m.mu_x);
    fprintf(fp, "<Mu_y> = %.17g\n", m.mu_y);
    fprintf(fp, "<H_c> = %.17g\n", m.H_c);
    fprintf(fp, "<H_cAngle> = %.17g\n", m.Theta_m);
    fprintf(fp, "<J_re> = %.17g\n", m.Jsrc.re);
    fprintf(fp, "<J_im> = %.17g\n", m.Jsrc.im);
    fprintf(fp, "<Sigma> = %.17g\n", m.Cduct);
    fprintf(fp, "<d_lam> = %.17g\n", m.Lam_d);
    fprintf(fp, "<Phi_h> = %.17g\n", m.Theta_hn);
    fprintf(fp, "<Phi_hx> = %.17g\n", m.Theta_hx);
    fprintf(fp, "<Phi_hy> = %.17g\n", m.Theta_hy);
    fprintf(fp, "<LamType> = %i\n", m.LamType);
    fprintf(fp, "<LamFill> = %.17g\n", m.LamFill);
    fprintf(fp, "<NStrands> = %i\n", m.NStrands);
    fprintf(fp, "<WireD> = %.17g\n", m.WireD);
    fprintf(fp, "<BHPoints> = %i\n", m.BHpoints);
    for (int j = 0; j < m.BHpoints; j++)
        fprintf(fp, "%.17g\t%.17g\n", m.BHdata[j].re, m.BHdata[j].im);
    fprintf(fp, "<EndBlock>\n\n");
}

int MatLibDialog::modelMaterialCount() const { return (int)m_modelMaterials.size(); }
std::string MatLibDialog::modelMaterialName(int idx) const { return m_modelMaterials[idx].BlockName; }
int MatLibDialog::copyLibToModel(int libIdx) {
    int idx = (int)m_modelMaterials.size();
    m_modelMaterials.push_back(m_libMaterials[libIdx]);
    return idx;
}
std::pair<int, std::string> MatLibDialog::copyModelToLib(int modelIdx) {
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m_modelMaterials[modelIdx]);
    return {idx, m_modelMaterials[modelIdx].BlockName};
}
void MatLibDialog::commitModel(const std::vector<int> &order) {
    auto &bp = asMag(m_doc)->blockproplist;
    bp.clear();
    for (int idx : order)
        if (idx >= 0 && idx < (int)m_modelMaterials.size())
            bp.push_back(m_modelMaterials[idx]);
}
std::string MatLibDialog::libMaterialName(int idx) const { return m_libMaterials[idx].BlockName; }
QString MatLibDialog::modelFileFilter() const { return tr("FEMM Magnetics (*.fem);;All Files (*)"); }

int MatLibDialog::addNewMaterial(QWidget *parent) {
    MaterialEditorDialog dlg(parent);
    if (dlg.exec() != QDialog::Accepted) return -1;
    FMaterialProp m; dlg.toProp(m);
    int idx = (int)m_libMaterials.size();
    m_libMaterials.push_back(m);
    return idx;
}

void MatLibDialog::editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (libIdx < 0 || libIdx >= (int)m_libMaterials.size()) return;
    MaterialEditorDialog dlg(parent);
    dlg.fromProp(m_libMaterials[libIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        FMaterialProp m = m_libMaterials[libIdx];
        dlg.toProp(m);
        m_libMaterials[libIdx] = m;
        item->setText(0, QString::fromStdString(m.BlockName));
    }
}

void MatLibDialog::editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) {
    if (modelIdx < 0 || modelIdx >= (int)m_modelMaterials.size()) return;
    MaterialEditorDialog dlg(parent);
    dlg.fromProp(m_modelMaterials[modelIdx]);
    if (dlg.exec() == QDialog::Accepted) {
        FMaterialProp m = m_modelMaterials[modelIdx];
        dlg.toProp(m);
        m_modelMaterials[modelIdx] = m;
        item->setText(0, QString::fromStdString(m.BlockName));
    }
}
