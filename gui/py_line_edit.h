// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_line_edit.h - QLineEdit with inline Python expression evaluation
//
//
// Select text and press Ctrl+L (or right-click -> Evaluate) to replace
// the selection with the evaluated result. Used in property dialogs for
// numeric input fields so users can type expressions like "25.4/2" or
// "sqrt(2)*10".
#ifndef PY_LINE_EDIT_H
#define PY_LINE_EDIT_H

#include <QLineEdit>

class PyLineEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit PyLineEdit(QWidget *parent = nullptr);

    void evaluateSelection();
    double evaluateAsDouble(double defaultVal = 0.0) const;
    void evaluateAsComplex(double &re, double &im) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
};

#endif // PY_LINE_EDIT_H
