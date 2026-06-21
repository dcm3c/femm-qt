// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_line_edit.cpp - QLineEdit with inline Python expression evaluation
//
//

#include "complex.h"
#include "py_line_edit.h"
#include "py_state.h"
// NB: deliberately does NOT include py_bind.h. Qt's slots/signals/emit
// keyword macros collide with MicroPython's obj.h. See py_console.cpp.

#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <string>

PyLineEdit::PyLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
}

void PyLineEdit::evaluateSelection()
{
    QString sel;
    bool hasSelection = this->hasSelectedText();

    if (hasSelection)
        sel = selectedText();
    else
        sel = text();

    if (sel.isEmpty()) return;

    std::string result;
    if (py_eval_string(sel.toStdString().c_str(), &result) && !result.empty()) {
        if (hasSelection)
            insert(QString::fromUtf8(result.c_str()));
        else
            setText(QString::fromUtf8(result.c_str()));
    }
}

double PyLineEdit::evaluateAsDouble(double defaultVal) const
{
    QString t = text().trimmed();
    if (t.isEmpty()) return defaultVal;

    bool ok;
    double v = t.toDouble(&ok);
    if (ok) return v;

    double out;
    if (py_eval_double(t.toStdString().c_str(), &out))
        return out;

    return defaultVal;
}

void PyLineEdit::evaluateAsComplex(double &re, double &im) const
{
    re = 0; im = 0;
    QString t = text().trimmed();
    if (t.isEmpty()) return;

    bool ok;
    double v = t.toDouble(&ok);
    if (ok) { re = v; return; }

    py_eval_complex(t.toStdString().c_str(), &re, &im);
}

void PyLineEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier) {
        evaluateSelection();
        return;
    }

    if (event->key() == Qt::Key_Return &&
        (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
        evaluateSelection();
        return;
    }

    QLineEdit::keyPressEvent(event);
}

void PyLineEdit::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();

    menu->addSeparator();
    QAction *evalAction = menu->addAction(tr("Evaluate (Ctrl+L)"));
    evalAction->setEnabled(!text().isEmpty());
    connect(evalAction, &QAction::triggered, this, &PyLineEdit::evaluateSelection);

    menu->exec(event->globalPos());
    delete menu;
}
