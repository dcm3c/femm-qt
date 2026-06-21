// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// matlib_dialog_base.h — Base class for material library browser dialogs
// Handles the dual-tree UI, folder I/O (incl. URL/vendor metadata),
// drag-and-drop, context menus, import, and OK/Cancel logic. Subclasses
// provide the material type, editor dialog, and file format.
//
// Working-copy model: the library lives in the subclass's lib vector and is
// written to disk only on OK; the model materials likewise live in a subclass
// working vector and are committed to the document only on OK (Cancel discards
// both). The trees are the source of truth for membership, order, and folder
// metadata — commit walks the model tree, mirroring OF's OnOK().
//
// Tree item data roles:
//   Qt::UserRole     -> ItemType (folder/material)
//   Qt::UserRole + 1 -> material index (materials only)
//   Qt::UserRole + 2 -> folder URL    (folders only)
//   Qt::UserRole + 3 -> folder vendor (folders only)
#ifndef MATLIB_DIALOG_BASE_H
#define MATLIB_DIALOG_BASE_H

#include <QDialog>
#include <QTreeWidget>
#include "matlib_tree.h"
#include <string>
#include <utility>
#include <vector>

class MatLibDialogBase : public QDialog {
    Q_OBJECT
public:
    MatLibDialogBase(const QString &title, const QString &libFilename,
                     QWidget *parent = nullptr);

protected:
    // --- Library materials (working copy; committed to disk on OK) ----------

    // Read one material block from fp (positioned after <BeginBlock>), up to
    // <EndBlock>. Store it in the lib working vector. Return (libIndex, name).
    virtual std::pair<int, std::string> readMaterialBlock(FILE *fp) = 0;
    // Write one library material to fp (between <BeginBlock>/<EndBlock>).
    virtual void writeMaterialBlock(FILE *fp, int libIdx) = 0;
    virtual std::string libMaterialName(int idx) const = 0;

    // --- Model materials (working copy; committed to the document on OK) -----

    virtual int modelMaterialCount() const = 0;
    virtual std::string modelMaterialName(int idx) const = 0;
    // Append a copy of library material libIdx to the model working copy;
    // return the new model index.
    virtual int copyLibToModel(int libIdx) = 0;
    // Append a copy of model material modelIdx to the library working copy;
    // return (libIndex, name).
    virtual std::pair<int, std::string> copyModelToLib(int modelIdx) = 0;
    // Rebuild the document's material list from the model working copy, taking
    // entries in the given order (working-copy indices, in model-tree order).
    virtual void commitModel(const std::vector<int> &orderedModelIndices) = 0;

    // --- Editors / import ---------------------------------------------------

    virtual int addNewMaterial(QWidget *parent) = 0;
    virtual void editLibMaterial(int libIdx, QTreeWidgetItem *item, QWidget *parent) = 0;
    virtual void editModelMaterial(int modelIdx, QTreeWidgetItem *item, QWidget *parent) = 0;
    // File-open filter for Import Materials, e.g. "FEMM Magnetics (*.fem)".
    virtual QString modelFileFilter() const = 0;

    // Subclass constructors must call this after their working copies exist.
    void initLibrary();

    bool eventFilter(QObject *obj, QEvent *event) override;

    MatLibTree *m_libTree;
    MatLibTree *m_modelTree;

private slots:
    void onItemDropped(MatLibTree *dst, MatLibTree *src, QTreeWidgetItem *dragged,
                       QTreeWidgetItem *target, MatLibTree::DropPos pos);
    void accept() override;

private:
    void loadLibrary();
    void saveLibrary();
    void populateModelTree();
    void readLibraryFile(const std::string &filename);
    void readFolderContents(FILE *fp, QTreeWidgetItem *parent);
    void writeTreeItem(QTreeWidgetItem *item, FILE *fp, int depth);
    bool modelTreeHasName(const QString &name) const;
    void addModelMaterialItem(int modelIdx, const QString &name);

    // Tree item factories
    QTreeWidgetItem *makeLibMaterialItem(int libIdx, const QString &name);
    QTreeWidgetItem *makeFolderItem(const QString &name, const QString &url,
                                    const QString &vendor);

    // Operations parameterized by target (shared by buttons and context menus)
    void showContextMenu(MatLibTree *tree, const QPoint &pos);
    void addMaterialTo(MatLibTree *tree, QTreeWidgetItem *targetFolder);
    void addFolderTo(QTreeWidgetItem *targetFolder);
    void deleteItem(MatLibTree *tree, QTreeWidgetItem *item);
    void modifyItem(MatLibTree *tree, QTreeWidgetItem *item);
    void editFolderItem(QTreeWidgetItem *folderItem);
    void visitVendor(const QString &url, const QString &name);
    void importMaterials(MatLibTree *targetTree, QTreeWidgetItem *targetFolder);

    std::string m_libPath;
    MatLibTree *m_activeTree;  // last tree to gain focus / be right-clicked
};

#endif // MATLIB_DIALOG_BASE_H
