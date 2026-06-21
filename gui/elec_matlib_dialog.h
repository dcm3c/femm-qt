// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// elec_matlib_dialog.h — Electrostatics material library browser dialog
// Subclasses MatLibDialogBase with EMaterialProp and statlib.dat format.
#ifndef ELEC_MATLIB_DIALOG_H
#define ELEC_MATLIB_DIALOG_H

#include "matlib_dialog_base.h"
#include "elec_types.h"
#include <vector>

class DocumentBase;

class ElecMatLibDialog : public MatLibDialogBase {
    Q_OBJECT
public:
    ElecMatLibDialog(DocumentBase *doc, QWidget *parent = nullptr);

protected:
    std::pair<int, std::string> readMaterialBlock(FILE *fp) override;
    void writeMaterialBlock(FILE *fp, int libIdx) override;
    int modelMaterialCount() const override;
    std::string modelMaterialName(int idx) const override;
    int copyLibToModel(int libIdx) override;
    std::pair<int, std::string> copyModelToLib(int modelIdx) override;
    void commitModel(const std::vector<int> &orderedModelIndices) override;
    int addNewMaterial(QWidget *parent) override;
    void editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) override;
    void editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) override;
    std::string libMaterialName(int idx) const override;
    QString modelFileFilter() const override;

private:
    DocumentBase *m_doc;
    std::vector<EMaterialProp> m_libMaterials;
    std::vector<EMaterialProp> m_modelMaterials;  // working copy; committed on OK
};

#endif // ELEC_MATLIB_DIALOG_H
