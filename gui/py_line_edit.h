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
//
// Long-value display: while a field is NOT focused it shows a compact,
// rounded form (so a value like AWG(20) = 0.811826439259007 fits the box and
// its exponent stays visible); focusing the field swaps in the full-resolution
// value, so editing always operates on the real number -- nothing is silently
// truncated. The full value is retained as the "canonical" string and is what
// evaluateAsDouble/Complex return unless the user actually edited the field.
#ifndef PY_LINE_EDIT_H
#define PY_LINE_EDIT_H

#include <QLineEdit>
#include <QString>

class QFocusEvent;

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
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    // Full-precision value behind the (possibly rounded) display. Authoritative
    // for reads unless the user has edited the field.
    QString m_canonical;
    bool m_rounding = false;   // re-entrancy guard around our own setText

    void setDisplayText(const QString &s);           // setText without re-capturing
    QString roundedForDisplay(const QString &s) const;
    QString sourceText() const;   // canonical if unedited, else the field text
};

#endif // PY_LINE_EDIT_H
