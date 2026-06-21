// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// matlib_tree.cpp — see matlib_tree.h.

#include "matlib_tree.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

MatLibTree::MatLibTree(Panel panel, QWidget *parent)
    : QTreeWidget(parent), m_panel(panel)
{
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
}

static bool fromMatLibTree(const QObject *src)
{
    return qobject_cast<const MatLibTree *>(src) != nullptr;
}

void MatLibTree::startDrag(Qt::DropActions)
{
    // Force copy semantics so the base view's default move logic never deletes
    // our source rows. MatLibDialogBase does every add/remove/move itself.
    QTreeWidget::startDrag(Qt::CopyAction);
}

void MatLibTree::dragEnterEvent(QDragEnterEvent *event)
{
    if (fromMatLibTree(event->source()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void MatLibTree::dragMoveEvent(QDragMoveEvent *event)
{
    if (!fromMatLibTree(event->source())) {
        event->ignore();
        return;
    }
    // Let the base view update the drop indicator and autoscroll, then accept.
    QTreeWidget::dragMoveEvent(event);
    event->acceptProposedAction();
}

void MatLibTree::dropEvent(QDropEvent *event)
{
    auto *src = qobject_cast<MatLibTree *>(event->source());
    if (!src) {
        event->ignore();
        return;
    }
    QTreeWidgetItem *dragged = src->currentItem();
    if (!dragged) {
        event->ignore();
        return;
    }

    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    DropPos pos = DropOnItem;
    switch (dropIndicatorPosition()) {
    case AboveItem: pos = DropAbove; break;
    case BelowItem: pos = DropBelow; break;
    default:        pos = DropOnItem; break; // OnItem / OnViewport
    }

    emit itemDropped(this, src, dragged, target, pos);

    // The drop has already been applied; stop the base class from also moving
    // or inserting rows.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
}
