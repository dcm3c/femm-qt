// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// matlib_tree.h — QTreeWidget with drag-and-drop for the material library dialog.
// Shared by the library and model panels of all four physics material browsers.
// Drop *semantics* (copy vs. move, folder handling, index bookkeeping) live in
// MatLibDialogBase, which connects to itemDropped(); this widget only detects a
// drop and forwards it. Qt provides the drag image, drop indicator, and
// autoscroll for free, so none of that has to be hand-coded.
#ifndef MATLIB_TREE_H
#define MATLIB_TREE_H

#include <QTreeWidget>

class MatLibTree : public QTreeWidget {
    Q_OBJECT
public:
    enum Panel { Library, Model };
    // Where the drop landed relative to the target item. (A public mirror of
    // QAbstractItemView::DropIndicatorPosition, which is protected and so cannot
    // appear in a signal signature.)
    enum DropPos { DropOnItem, DropAbove, DropBelow };
    explicit MatLibTree(Panel panel, QWidget *parent = nullptr);
    Panel panel() const { return m_panel; }

signals:
    // Emitted by the destination tree when an item is dropped onto it. The
    // dialog performs the actual add/move/copy; the event itself is consumed so
    // the base view never moves rows on its own.
    void itemDropped(MatLibTree *dst, MatLibTree *src, QTreeWidgetItem *dragged,
                     QTreeWidgetItem *target, MatLibTree::DropPos pos);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    Panel m_panel;
};

#endif // MATLIB_TREE_H
