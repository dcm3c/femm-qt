// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_line_edit.cpp - QLineEdit with inline Python expression evaluation
//
//

#include "complex.h"
#include "complex_format.h"   // femmFmtComplex — shared compact display formatter
#include "py_line_edit.h"
#include "py_state.h"
// NB: deliberately does NOT include py_bind.h. Qt's slots/signals/emit
// keyword macros collide with MicroPython's obj.h. See py_console.cpp.

#include <QKeyEvent>
#include <QFocusEvent>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <string>

// Significant figures in a field's compact (unfocused) display form. Matches
// the post-processor readouts; keeps long values readable and their exponents
// visible without overflowing a narrow box.
static constexpr int kDisplayDigits = 8;

PyLineEdit::PyLineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    // Capture a programmatically-set value (a dialog loading a stored number)
    // as the canonical full-precision value, and show a compact rounded form.
    // Discriminate a load from the user typing by focus -- a field being
    // populated isn't focused; keying off isModified() would risk rounding
    // mid-keystroke if Qt set that flag after emitting textChanged. The focused
    // paths (evaluate, and focus-in showing the full value) capture explicitly.
    connect(this, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (m_rounding || hasFocus()) return;
        m_canonical = t.trimmed();
        setDisplayText(roundedForDisplay(m_canonical));
    });
}

void PyLineEdit::setDisplayText(const QString &s)
{
    m_rounding = true;
    setText(s);
    setCursorPosition(0);   // show the start, not the scrolled-to-end tail
    setModified(false);     // a display/canonical update, not a user edit
    m_rounding = false;
}

QString PyLineEdit::sourceText() const
{
    // Unedited field -> the canonical (full-precision) value; edited -> as typed.
    if (!isModified() && !m_canonical.isEmpty())
        return m_canonical.trimmed();
    return text().trimmed();
}

QString PyLineEdit::roundedForDisplay(const QString &s) const
{
    bool ok;
    double v = s.toDouble(&ok);
    if (ok) return QString::number(v, 'g', kDisplayDigits);
    // Complex ("20+10j") only if it even looks numeric -- leave names alone.
    if (!s.isEmpty()) {
        QChar c = s.at(0);
        if (c.isDigit() || c == '+' || c == '-' || c == '.') {
            double re = 0, im = 0;
            if (py_eval_complex(s.toStdString().c_str(), &re, &im))
                return femmFmtComplex(CComplex(re, im), kDisplayDigits);
        }
    }
    return s;   // not a number (a name/label) -> unchanged
}

void PyLineEdit::focusInEvent(QFocusEvent *event)
{
    QLineEdit::focusInEvent(event);
    if (event->reason() == Qt::PopupFocusReason) return;  // see focusOutEvent
    // Editing operates on the FULL value, so nothing is silently lost.
    if (!m_canonical.isEmpty() && m_canonical != text())
        setDisplayText(m_canonical);
}

void PyLineEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    // A popup taking focus -- our own context menu -- is not the user leaving
    // the field. Reformatting here would call setText()/setCursorPosition(),
    // which clear the selection the user right-clicked in order to Copy. Leave
    // the field (and the selection) alone; the real focus-out still reformats.
    if (event->reason() == Qt::PopupFocusReason) return;
    // If the user edited the field, their text is the new full value.
    if (isModified())
        m_canonical = text().trimmed();
    // Back to the compact form.
    if (!m_canonical.isEmpty())
        setDisplayText(roundedForDisplay(m_canonical));
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
        if (hasSelection) {
            insert(QString::fromUtf8(result.c_str()));
        } else {
            // Whole-field replace. The field has focus, so show the full result
            // (focused == full) and record it as the canonical value; it rounds
            // to the compact form once focus leaves.
            m_canonical = QString::fromUtf8(result.c_str()).trimmed();
            setDisplayText(m_canonical);
        }
    }
}

double PyLineEdit::evaluateAsDouble(double defaultVal) const
{
    QString t = sourceText();
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
    QString t = sourceText();
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
