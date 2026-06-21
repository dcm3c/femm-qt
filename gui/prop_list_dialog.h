// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// prop_list_dialog.h — Generic property list dialog (Add/Delete/Modify)
// Common pattern used by Materials, Boundaries, Circuits, Point Properties.
#ifndef PROP_LIST_DIALOG_H
#define PROP_LIST_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <functional>
#include <vector>
#include <string>

class DocumentBase;

class PropertyListDialog : public QDialog
{
    Q_OBJECT

public:
    // propType: 0=PointProps, 1=Boundaries, 2=Materials, 3=Circuits
    PropertyListDialog(DocumentBase *doc, int propType, QWidget *parent = nullptr);

private slots:
    void onAdd();
    void onDelete();
    void onModify();

private:
    void populateList();
    // Opens the appropriate editor dialog. Returns true if accepted.
    bool openEditor(int index);  // -1 for new

    DocumentBase *m_doc;
    int m_propType;
    QListWidget *m_list;
};

#endif // PROP_LIST_DIALOG_H
