// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// matlib_dialog_base.cpp — Base material library browser dialog
// All UI, folder I/O, drag-and-drop, context menus, import, and tree
// management. Delegates material-specific read/write/edit and document commit
// to the subclass via pure virtual methods. See matlib_dialog_base.h.

#include "matlib_dialog_base.h"
#include "matlib_tree.h"
#include "document.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QMessageBox>
#include <QMenu>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QEvent>
#include <QKeyEvent>
#include <QApplication>
#include <cstdio>
#include <cstring>

enum ItemType { IT_Folder = 0, IT_Material = 1 };

// Data roles (see header). Type/index reuse the original literals.
static constexpr int RoleType   = Qt::UserRole;      // ItemType
static constexpr int RoleMatIdx = Qt::UserRole + 1;  // material index
static constexpr int RoleUrl    = Qt::UserRole + 2;  // folder URL
static constexpr int RoleVendor = Qt::UserRole + 3;  // folder vendor

MatLibDialogBase::MatLibDialogBase(const QString &title, const QString &libFilename,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(700, 500);

    m_libPath = QApplication::applicationDirPath().toStdString() + "/" + libFilename.toStdString();

    auto *splitter = new QSplitter(Qt::Horizontal);

    auto *libPanel = new QWidget;
    auto *libLayout = new QVBoxLayout(libPanel);
    libLayout->setContentsMargins(0, 0, 0, 0);
    libLayout->addWidget(new QLabel(tr("Library Materials")));
    m_libTree = new MatLibTree(MatLibTree::Library);
    libLayout->addWidget(m_libTree);
    splitter->addWidget(libPanel);

    auto *modelPanel = new QWidget;
    auto *modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->addWidget(new QLabel(tr("Model Materials")));
    m_modelTree = new MatLibTree(MatLibTree::Model);
    modelLayout->addWidget(m_modelTree);
    splitter->addWidget(modelPanel);

    m_activeTree = m_libTree;

    // All operations are reached via drag-and-drop, double-click, the Delete/
    // Insert keys, and the right-click context menus — no button column.
    connect(m_libTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { modifyItem(m_libTree, item); });
    connect(m_modelTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { modifyItem(m_modelTree, item); });
    connect(m_libTree, &MatLibTree::itemDropped, this, &MatLibDialogBase::onItemDropped);
    connect(m_modelTree, &MatLibTree::itemDropped, this, &MatLibDialogBase::onItemDropped);

    // Track the active tree (so buttons/menus act on the right one) and host
    // per-item right-click menus.
    m_libTree->installEventFilter(this);
    m_modelTree->installEventFilter(this);
    m_libTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_modelTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_libTree, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { showContextMenu(m_libTree, p); });
    connect(m_modelTree, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &p) { showContextMenu(m_modelTree, p); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &MatLibDialogBase::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &MatLibDialogBase::reject);

    auto *topLayout = new QVBoxLayout(this);
    topLayout->addWidget(splitter, 1);
    topLayout->addWidget(buttons);

    // NOTE: subclass constructors must call initLibrary() after construction,
    // since loadLibrary()/populateModelTree() call virtual methods.
}

bool MatLibDialogBase::eventFilter(QObject *obj, QEvent *event)
{
    MatLibTree *tree = (obj == m_libTree)   ? m_libTree
                     : (obj == m_modelTree) ? m_modelTree
                                            : nullptr;
    if (tree) {
        if (event->type() == QEvent::FocusIn) {
            m_activeTree = tree;
        } else if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Delete) {
                m_activeTree = tree;
                deleteItem(tree, tree->currentItem());
                return true;
            }
            if (ke->key() == Qt::Key_Insert) {
                m_activeTree = tree;
                QTreeWidgetItem *folder = nullptr;
                if (tree == m_libTree) {
                    auto *cur = tree->currentItem();
                    if (cur && cur->data(0, RoleType).toInt() == IT_Folder) folder = cur;
                }
                addMaterialTo(tree, folder);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

// ================================================================
// Library file I/O
// ================================================================

static char *StripKey(char *s)
{
    char *p = strchr(s, '=');
    return p ? p + 1 : s;
}

static std::string ExtractQuoted(char *v)
{
    char *start = strchr(v, '"');
    if (!start) return std::string(v);
    start++;
    char *end = strrchr(start, '"');
    if (end) *end = 0;
    return std::string(start);
}

void MatLibDialogBase::initLibrary()
{
    loadLibrary();
    populateModelTree();
}

void MatLibDialogBase::loadLibrary()
{
    m_libTree->clear();
    readLibraryFile(m_libPath);
    m_libTree->collapseAll();
}

void MatLibDialogBase::readLibraryFile(const std::string &filename)
{
    FILE *fp = fopen(filename.c_str(), "rt");
    if (!fp) return;
    readFolderContents(fp, nullptr);
    fclose(fp);
}

QTreeWidgetItem *MatLibDialogBase::makeLibMaterialItem(int libIdx, const QString &name)
{
    auto *item = new QTreeWidgetItem;
    item->setText(0, name);
    item->setData(0, RoleType, IT_Material);
    item->setData(0, RoleMatIdx, libIdx);
    return item;
}

QTreeWidgetItem *MatLibDialogBase::makeFolderItem(const QString &name, const QString &url,
                                                  const QString &vendor)
{
    auto *item = new QTreeWidgetItem;
    item->setText(0, name);
    item->setData(0, RoleType, IT_Folder);
    item->setData(0, RoleUrl, url);
    item->setData(0, RoleVendor, vendor);
    item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    return item;
}

void MatLibDialogBase::readFolderContents(FILE *fp, QTreeWidgetItem *parent)
{
    char s[1024], q[1024];

    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;

        if (_strnicmp(q, "<beginfolder>", 13) == 0) {
            // Name/URL/vendor follow inside the folder and are applied to it by
            // the recursive call (which has this folder as its parent).
            auto *folderItem = makeFolderItem(tr("New Folder"), "", "");
            if (parent) parent->addChild(folderItem);
            else m_libTree->addTopLevelItem(folderItem);
            readFolderContents(fp, folderItem);
            continue;
        }
        if (_strnicmp(q, "<foldername>", 12) == 0) {
            if (parent) parent->setText(0, QString::fromStdString(ExtractQuoted(StripKey(s))));
            continue;
        }
        if (_strnicmp(q, "<folderurl>", 11) == 0) {
            if (parent) parent->setData(0, RoleUrl, QString::fromStdString(ExtractQuoted(StripKey(s))));
            continue;
        }
        if (_strnicmp(q, "<foldervendor>", 14) == 0) {
            if (parent) parent->setData(0, RoleVendor, QString::fromStdString(ExtractQuoted(StripKey(s))));
            continue;
        }
        if (_strnicmp(q, "<endfolder>", 11) == 0)
            return;

        if (_strnicmp(q, "<beginblock>", 12) == 0) {
            auto [matIdx, name] = readMaterialBlock(fp);
            auto *matItem = makeLibMaterialItem(matIdx, QString::fromStdString(name));
            if (parent) parent->addChild(matItem);
            else m_libTree->addTopLevelItem(matItem);
        }
    }
}

void MatLibDialogBase::saveLibrary()
{
    FILE *fp = fopen(m_libPath.c_str(), "wt");
    if (!fp) return;
    for (int i = 0; i < m_libTree->topLevelItemCount(); i++)
        writeTreeItem(m_libTree->topLevelItem(i), fp, 0);
    fclose(fp);
}

void MatLibDialogBase::writeTreeItem(QTreeWidgetItem *item, FILE *fp, int depth)
{
    int type = item->data(0, RoleType).toInt();
    if (type == IT_Folder) {
        fprintf(fp, "<BeginFolder>\n");
        fprintf(fp, "<FolderName> = \"%s\"\n", item->text(0).toStdString().c_str());
        QString url = item->data(0, RoleUrl).toString();
        QString vendor = item->data(0, RoleVendor).toString();
        if (!url.isEmpty())
            fprintf(fp, "<FolderURL> = \"%s\"\n", url.toStdString().c_str());
        if (!vendor.isEmpty())
            fprintf(fp, "<FolderVendor> = \"%s\"\n", vendor.toStdString().c_str());
        for (int i = 0; i < item->childCount(); i++)
            writeTreeItem(item->child(i), fp, depth + 1);
        fprintf(fp, "<EndFolder>\n\n");
    } else {
        int matIdx = item->data(0, RoleMatIdx).toInt();
        writeMaterialBlock(fp, matIdx);
    }
}

// ================================================================
// Model tree
// ================================================================

void MatLibDialogBase::populateModelTree()
{
    m_modelTree->clear();
    for (int i = 0; i < modelMaterialCount(); i++)
        addModelMaterialItem(i, QString::fromStdString(modelMaterialName(i)));
}

void MatLibDialogBase::addModelMaterialItem(int modelIdx, const QString &name)
{
    auto *item = new QTreeWidgetItem;
    item->setText(0, name);
    item->setData(0, RoleType, IT_Material);
    item->setData(0, RoleMatIdx, modelIdx);
    m_modelTree->addTopLevelItem(item);
}

bool MatLibDialogBase::modelTreeHasName(const QString &name) const
{
    for (int i = 0; i < m_modelTree->topLevelItemCount(); i++)
        if (m_modelTree->topLevelItem(i)->text(0) == name) return true;
    return false;
}

// ================================================================
// Add / delete / modify (shared by context menus and keyboard)
// ================================================================

void MatLibDialogBase::addMaterialTo(MatLibTree *tree, QTreeWidgetItem *targetFolder)
{
    int libIdx = addNewMaterial(this);
    if (libIdx < 0) return;
    QString name = QString::fromStdString(libMaterialName(libIdx));

    if (tree == m_modelTree) {
        if (modelTreeHasName(name)) {
            QMessageBox::information(this, tr("Duplicate"),
                tr("Material '%1' already exists in the model.").arg(name));
            return;
        }
        int modelIdx = copyLibToModel(libIdx);
        addModelMaterialItem(modelIdx, name);
    } else {
        auto *item = makeLibMaterialItem(libIdx, name);
        if (targetFolder) targetFolder->addChild(item);
        else m_libTree->addTopLevelItem(item);
        m_libTree->setCurrentItem(item);
    }
}

void MatLibDialogBase::addFolderTo(QTreeWidgetItem *targetFolder)
{
    auto *folder = makeFolderItem(tr("New Folder"), "", "");
    if (targetFolder) targetFolder->addChild(folder);
    else m_libTree->addTopLevelItem(folder);
    m_libTree->setCurrentItem(folder);
    editFolderItem(folder);  // let the user name it now; Cancel leaves "New Folder"
}

void MatLibDialogBase::deleteItem(MatLibTree *tree, QTreeWidgetItem *item)
{
    if (!item) return;
    // Membership is defined by the tree, so deleting is just removing the item
    // (working-vector entries are left orphaned and simply never committed).
    auto *parent = item->parent();
    if (parent) parent->removeChild(item);
    else tree->takeTopLevelItem(tree->indexOfTopLevelItem(item));
    delete item;
}

void MatLibDialogBase::modifyItem(MatLibTree *tree, QTreeWidgetItem *item)
{
    if (!item) return;
    int type = item->data(0, RoleType).toInt();
    if (type == IT_Material) {
        int idx = item->data(0, RoleMatIdx).toInt();
        if (tree == m_modelTree) editModelMaterial(idx, item, this);
        else editLibMaterial(idx, item, this);
    } else if (tree == m_libTree) {
        editFolderItem(item);
    }
}

// ================================================================
// Folders: edit info + visit vendor
// ================================================================

// Modal dialog editing a folder's name/vendor/URL. Returns true if accepted.
static bool editFolderInfo(QString &name, QString &url, QString &vendor, QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Folder Information"));
    auto *form = new QFormLayout(&dlg);
    auto *nameEdit = new QLineEdit(name);
    auto *vendorEdit = new QLineEdit(vendor);
    auto *urlEdit = new QLineEdit(url);
    form->addRow(QObject::tr("Name:"), nameEdit);
    form->addRow(QObject::tr("Vendor:"), vendorEdit);
    form->addRow(QObject::tr("URL:"), urlEdit);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return false;
    name = nameEdit->text();
    url = urlEdit->text();
    vendor = vendorEdit->text();
    return true;
}

void MatLibDialogBase::editFolderItem(QTreeWidgetItem *item)
{
    if (!item || item->data(0, RoleType).toInt() != IT_Folder) return;
    QString name = item->text(0);
    QString url = item->data(0, RoleUrl).toString();
    QString vendor = item->data(0, RoleVendor).toString();
    if (!editFolderInfo(name, url, vendor, this)) return;
    item->setText(0, name);
    item->setData(0, RoleUrl, url);
    item->setData(0, RoleVendor, vendor);
}

void MatLibDialogBase::visitVendor(const QString &url, const QString &name)
{
    if (url.isEmpty()) {
        QMessageBox::information(this, tr("Visit Vendor"),
            tr("No URL available for %1.").arg(name));
        return;
    }
    QDesktopServices::openUrl(QUrl(url));
}

// ================================================================
// Import materials from a model file
// ================================================================

void MatLibDialogBase::importMaterials(MatLibTree *targetTree, QTreeWidgetItem *targetFolder)
{
    QString fn = QFileDialog::getOpenFileName(this, tr("Import Materials"),
                                              QString(), modelFileFilter());
    if (fn.isEmpty()) return;
    FILE *fp = fopen(fn.toLocal8Bit().constData(), "rt");
    if (!fp) {
        QMessageBox::warning(this, tr("Import Materials"),
            tr("Could not open %1").arg(fn));
        return;
    }

    const bool toLib = (targetTree == m_libTree);
    QTreeWidgetItem *importFolder = nullptr;  // created lazily on first hit
    int imported = 0, skipped = 0;
    char s[1024], q[1024];

    while (fgets(s, 1024, fp) != NULL) {
        if (sscanf(s, "%s", q) == EOF) continue;
        if (_strnicmp(q, "<beginblock>", 12) != 0) continue;

        auto [libIdx, nameStd] = readMaterialBlock(fp);
        QString name = QString::fromStdString(nameStd);

        if (toLib) {
            if (!importFolder) {
                importFolder = makeFolderItem(tr("Imported Materials"), "", "");
                if (targetFolder) targetFolder->addChild(importFolder);
                else m_libTree->insertTopLevelItem(0, importFolder);
            }
            importFolder->addChild(makeLibMaterialItem(libIdx, name));
            imported++;
        } else {
            if (modelTreeHasName(name)) { skipped++; continue; }
            int modelIdx = copyLibToModel(libIdx);
            addModelMaterialItem(modelIdx, name);
            imported++;
        }
    }
    fclose(fp);

    if (importFolder) m_libTree->expandItem(importFolder);

    QString msg = tr("Imported %1 material(s).").arg(imported);
    if (skipped) msg += " " + tr("Skipped %1 already in the model.").arg(skipped);
    QMessageBox::information(this, tr("Import Materials"), msg);
}

// ================================================================
// Context menus
// ================================================================

void MatLibDialogBase::showContextMenu(MatLibTree *tree, const QPoint &pos)
{
    m_activeTree = tree;
    QTreeWidgetItem *item = tree->itemAt(pos);
    if (item) tree->setCurrentItem(item);

    const bool isLib = (tree == m_libTree);
    const int type = item ? item->data(0, RoleType).toInt() : -1;
    const bool isFolder = (type == IT_Folder);
    const bool isMaterial = (type == IT_Material);

    QMenu menu(this);

    if (item)
        menu.addAction(tr("Delete Selected"), this, [this, tree, item] { deleteItem(tree, item); });
    menu.addAction(tr("Add New Material"), this,
        [this, tree, item, isFolder] { addMaterialTo(tree, isFolder ? item : nullptr); });
    if (isLib)
        menu.addAction(tr("Add New Folder"), this,
            [this, item, isFolder] { addFolderTo(isFolder ? item : nullptr); });
    if (isMaterial)
        menu.addAction(tr("Modify Material"), this, [this, tree, item] { modifyItem(tree, item); });
    if (isLib && isFolder)
        menu.addAction(tr("Edit Folder Info"), this, [this, item] { editFolderItem(item); });

    // "Visit <vendor>" — for a folder with a vendor, or a material whose parent
    // folder has one (matching OF).
    if (isLib) {
        QTreeWidgetItem *vendorItem = isFolder ? item
                                    : (isMaterial ? item->parent() : nullptr);
        if (vendorItem && vendorItem->data(0, RoleType).toInt() == IT_Folder) {
            QString vendor = vendorItem->data(0, RoleVendor).toString();
            QString url = vendorItem->data(0, RoleUrl).toString();
            if (!vendor.isEmpty())
                menu.addAction(tr("Visit %1").arg(vendor), this,
                    [this, url, vendor] { visitVendor(url, vendor); });
        }
    }

    menu.addSeparator();
    menu.addAction(tr("Import Materials..."), this,
        [this, tree, item, isFolder] { importMaterials(tree, isFolder ? item : nullptr); });

    menu.exec(tree->viewport()->mapToGlobal(pos));
}

// ================================================================
// Drag-and-drop
// ================================================================

namespace {

// Resolve where a drop lands in the destination tree.
//   parent == nullptr -> top level; index < 0 -> append.
void resolveDropLocation(QTreeWidget *tree, QTreeWidgetItem *target,
                         MatLibTree::DropPos pos,
                         QTreeWidgetItem *&parent, int &index)
{
    parent = nullptr;
    index = -1;
    if (!target) return; // dropped on empty space -> append at top level

    bool targetIsFolder = (target->data(0, RoleType).toInt() == IT_Folder);
    if (pos == MatLibTree::DropOnItem && targetIsFolder) {
        parent = target; // drop into the folder
        index = -1;      // append inside it
        return;
    }
    // Above/Below (or onto a material): insert as a sibling of the target.
    parent = target->parent();
    int row = parent ? parent->indexOfChild(target)
                     : tree->indexOfTopLevelItem(target);
    index = (pos == MatLibTree::DropBelow) ? row + 1 : row;
}

QTreeWidgetItem *takeItem(QTreeWidget *tree, QTreeWidgetItem *it)
{
    if (auto *p = it->parent()) return p->takeChild(p->indexOfChild(it));
    return tree->takeTopLevelItem(tree->indexOfTopLevelItem(it));
}

void insertItem(QTreeWidget *tree, QTreeWidgetItem *parent, int index, QTreeWidgetItem *it)
{
    if (parent) {
        if (index < 0 || index > parent->childCount()) parent->addChild(it);
        else parent->insertChild(index, it);
    } else {
        if (index < 0 || index > tree->topLevelItemCount()) tree->addTopLevelItem(it);
        else tree->insertTopLevelItem(index, it);
    }
}

// Gather every material leaf under item (recursing into folders), as
// (libIndex, name) pairs.
void collectLibLeaves(QTreeWidgetItem *item, std::vector<std::pair<int, QString>> &out)
{
    if (item->data(0, RoleType).toInt() == IT_Material) {
        out.push_back({ item->data(0, RoleMatIdx).toInt(), item->text(0) });
        return;
    }
    for (int i = 0; i < item->childCount(); i++)
        collectLibLeaves(item->child(i), out);
}

} // namespace

void MatLibDialogBase::onItemDropped(MatLibTree *dst, MatLibTree *src,
                                     QTreeWidgetItem *dragged, QTreeWidgetItem *target,
                                     MatLibTree::DropPos pos)
{
    if (!dragged || !src || !dst) return;
    const bool fromLib = (src->panel() == MatLibTree::Library);
    const bool toLib   = (dst->panel() == MatLibTree::Library);
    const bool draggedIsFolder = (dragged->data(0, RoleType).toInt() == IT_Folder);

    QTreeWidgetItem *newParent = nullptr;
    int insertIndex = -1;
    resolveDropLocation(dst, target, pos, newParent, insertIndex);

    if (fromLib && toLib) {
        // ---- MOVE within the library ----------------------------------
        // Reject dropping a folder onto itself or one of its descendants.
        if (draggedIsFolder)
            for (QTreeWidgetItem *p = newParent; p; p = p->parent())
                if (p == dragged) return;

        QTreeWidgetItem *oldParent = dragged->parent();
        int oldRow = oldParent ? oldParent->indexOfChild(dragged)
                               : dst->indexOfTopLevelItem(dragged);
        QTreeWidgetItem *moved = takeItem(dst, dragged);
        if (!moved) return;
        // Removing the item shifts later siblings down by one within the same
        // parent; compensate so the drop lands where the indicator showed.
        if (oldParent == newParent && insertIndex > oldRow) insertIndex--;
        insertItem(dst, newParent, insertIndex, moved);
        dst->setCurrentItem(moved);
        return;
    }

    if (fromLib && !toLib) {
        // ---- COPY library -> model (folders flattened) ----------------
        std::vector<std::pair<int, QString>> leaves;
        collectLibLeaves(dragged, leaves);
        for (auto &leaf : leaves) {
            if (modelTreeHasName(leaf.second)) continue; // skip dups silently
            int modelIdx = copyLibToModel(leaf.first);
            addModelMaterialItem(modelIdx, leaf.second);
        }
        return;
    }

    if (!fromLib && toLib) {
        // ---- COPY model -> library ------------------------------------
        int modelIdx = dragged->data(0, RoleMatIdx).toInt();
        auto [libIdx, name] = copyModelToLib(modelIdx);
        auto *item = makeLibMaterialItem(libIdx, QString::fromStdString(name));
        insertItem(dst, newParent, insertIndex, item);
        dst->setCurrentItem(item);
        return;
    }

    // ---- MOVE within the model (reorder; model tree is flat) ----------
    int oldRow = dst->indexOfTopLevelItem(dragged);
    QTreeWidgetItem *moved = takeItem(dst, dragged);
    if (!moved) return;
    if (insertIndex > oldRow) insertIndex--;
    insertItem(dst, nullptr, insertIndex, moved);
    dst->setCurrentItem(moved);
}

// ================================================================
// OK / Cancel
// ================================================================

void MatLibDialogBase::accept()
{
    // Commit model materials to the document in current tree order, then write
    // the library to disk. Cancel (reject) skips both, discarding all edits.
    std::vector<int> order;
    for (int i = 0; i < m_modelTree->topLevelItemCount(); i++) {
        auto *it = m_modelTree->topLevelItem(i);
        if (it->data(0, RoleType).toInt() == IT_Material)
            order.push_back(it->data(0, RoleMatIdx).toInt());
    }
    commitModel(order);
    saveLibrary();
    QDialog::accept();
}
