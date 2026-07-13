// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_console.cpp - single-pane Python terminal REPL.
//
// One QPlainTextEdit holds the whole session. Text before m_inputStart (the
// position right after the current ">>> " prompt) is read-only transcript; the
// user edits only from there to the end of the document. Enter runs a complete
// statement, or inserts a continuation line for an incomplete one (decided by
// py_repl_continue, MicroPython's REPL completeness check). Up/Down recall
// history. Output from print()/tracebacks streams in via writeRaw during a run,
// after which a fresh prompt is drawn.

#include "py_console.h"
#include "py_state.h"
// NB: deliberately does NOT include py_bind.h / MicroPython headers — obj.h
// uses `slots` as a struct field, colliding with Qt's keyword macro. py_state.h
// is the clean C++ surface.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

static PyConsole *s_console = nullptr;

PyConsole *pyConsoleInstance() { return s_console; }

static void py_console_sink(const char *s, size_t n) {
    if (s_console) s_console->writeRaw(s, (int)n);
}
static void py_console_vm_hook() {
    if (s_console) s_console->scriptTick();
}

// ---------------------------------------------------------------------------
// Inner edit widget: forwards key/paste events to the PyConsole logic.
// ---------------------------------------------------------------------------
class ConsoleEdit : public QPlainTextEdit
{
public:
    PyConsole *owner = nullptr;
    explicit ConsoleEdit(PyConsole *o) : owner(o) {}

protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (owner && owner->handleKeyPress(e)) return;
        QPlainTextEdit::keyPressEvent(e);
    }
    void insertFromMimeData(const QMimeData *src) override {
        if (owner) owner->handlePaste(src);
        else QPlainTextEdit::insertFromMimeData(src);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        QPlainTextEdit::mouseReleaseEvent(e);   // let it finish the click/selection
        if (owner) owner->handleMouseRelease();
    }
};

// ---------------------------------------------------------------------------

PyConsole::PyConsole(QWidget *parent)
    : QDockWidget(tr("Python Console"), parent)
{
    s_console = this;

    m_term = new ConsoleEdit(this);
    m_term->setFont(QFont("Consolas", 9));
    m_term->setUndoRedoEnabled(false);
    m_term->setMaximumBlockCount(5000);
    m_term->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setWidget(m_term);

    m_stopPoll = new QTimer(this);
    m_stopPoll->setInterval(100);
    connect(m_stopPoll, &QTimer::timeout, this, [this]() {
        if (!py_is_running()) {
            m_stopPoll->stop();
            emit scriptRunningChanged(false);
            drawPrompt();   // restore the prompt after any run (incl. File>Run
        }                   // Script, IPC, or an interrupt). No-op if one exists.
    });

    set_py_output_sink(py_console_sink);
    set_py_vm_hook(py_console_vm_hook);

    drawPrompt();
}

// --- output ---------------------------------------------------------------

void PyConsole::ensureTrailingNewline()
{
    QTextDocument *d = m_term->document();
    if (d->isEmpty()) return;
    QTextCursor c(d);
    c.movePosition(QTextCursor::End);
    if (c.positionInBlock() != 0) {       // document does not already end in '\n'
        c.insertText("\n");
        m_term->setTextCursor(c);
    }
}

void PyConsole::writeRaw(const char *s, int n)
{
    // First output of a run started outside the console (its m_inputStart is
    // still >= 0, unlike a console-typed command which set it to -1 before
    // exec): retire the idle prompt so output begins on a fresh line, and arm
    // the end-of-run poll so a new prompt is drawn when it finishes. Done here
    // (not only on the VM-loop tick) because the script's first print can beat
    // the first tick.
    if (m_inputStart >= 0) {
        ensureTrailingNewline();
        m_inputStart = -1;
        if (!m_stopPoll->isActive()) {
            m_stopPoll->start();
            emit scriptRunningChanged(true);
        }
    }
    QTextCursor c(m_term->document());
    c.movePosition(QTextCursor::End);
    c.insertText(QString::fromUtf8(s, n));
    m_term->setTextCursor(c);
    m_term->verticalScrollBar()->setValue(m_term->verticalScrollBar()->maximum());
}

void PyConsole::appendOutput(const QString &text)
{
    ensureTrailingNewline();
    QTextCursor c(m_term->document());
    c.movePosition(QTextCursor::End);
    c.insertText(text);
    m_term->setTextCursor(c);
    m_term->verticalScrollBar()->setValue(m_term->verticalScrollBar()->maximum());
}

void PyConsole::clearOutput()
{
    m_term->clear();
    m_inputStart = -1;
    drawPrompt();
}

void PyConsole::drawPrompt()
{
    if (m_inputStart >= 0) return;          // a prompt is already active
    ensureTrailingNewline();
    QTextCursor c(m_term->document());
    c.movePosition(QTextCursor::End);
    c.insertText(">>> ");
    m_inputStart = c.position();
    m_term->setTextCursor(c);
    m_term->verticalScrollBar()->setValue(m_term->verticalScrollBar()->maximum());
}

// --- input region helpers -------------------------------------------------

QString PyConsole::inputText() const
{
    if (m_inputStart < 0) return QString();
    QTextCursor c(m_term->document());
    c.setPosition(m_inputStart);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return c.selectedText().replace(QChar(0x2029), '\n');  // QTextCursor uses U+2029
}

void PyConsole::setInputText(const QString &s)
{
    QTextCursor c(m_term->document());
    c.setPosition(m_inputStart);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.insertText(s);
    moveCursorToInputEnd();
}

void PyConsole::moveCursorToInputEnd()
{
    QTextCursor c(m_term->document());
    c.movePosition(QTextCursor::End);
    m_term->setTextCursor(c);
}

bool PyConsole::cursorInInput() const
{
    if (m_inputStart < 0) return false;
    QTextCursor c = m_term->textCursor();
    return c.anchor() >= m_inputStart && c.position() >= m_inputStart;
}

void PyConsole::recallHistory(int dir)
{
    if (m_history.isEmpty() && m_historyIdx < 0) return;
    if (dir < 0) {                              // older
        if (m_historyIdx + 1 < m_history.size()) {
            m_historyIdx++;
            setInputText(m_history[m_historyIdx]);
        }
    } else {                                    // newer
        if (m_historyIdx > 0) {
            m_historyIdx--;
            setInputText(m_history[m_historyIdx]);
        } else if (m_historyIdx == 0) {
            m_historyIdx = -1;
            setInputText(QString());
        }
    }
}

// --- evaluation -----------------------------------------------------------

void PyConsole::evaluateInput()
{
    QString src = inputText();

    // Incomplete statement -> add a continuation line and keep editing.
    if (!src.trimmed().isEmpty()) {
        QByteArray u = src.toUtf8();
        if (py_repl_continue(u.constData())) {
            moveCursorToInputEnd();
            m_term->insertPlainText("\n");
            m_term->verticalScrollBar()->setValue(m_term->verticalScrollBar()->maximum());
            return;
        }
    }

    // Complete: finish the command line, then run it.
    moveCursorToInputEnd();
    m_term->insertPlainText("\n");
    m_inputStart = -1;                          // input region closed; drawPrompt() will reopen

    if (!src.trimmed().isEmpty()) {
        m_history.prepend(src);
        if (m_history.size() > 200) m_history.removeLast();
        m_historyIdx = -1;

        QByteArray u = src.toUtf8();
        char errmsg[1024] = {};
        int status = exec_python(u.constData(), errmsg, sizeof(errmsg));
        if (status != 0)
            appendOutput(QString("Error: %1").arg(errmsg));
        else if (!py_last_display().empty())
            appendOutput(QString::fromUtf8(py_last_display().c_str()));
    }

    drawPrompt();
}

// --- key / paste handling -------------------------------------------------

bool PyConsole::handleKeyPress(QKeyEvent *e)
{
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool isText = !e->text().isEmpty() && (e->text().at(0).isPrint() || key == Qt::Key_Tab);

    // While a script runs, block edits but let navigation/copy through.
    if (py_is_running()) {
        if (key == Qt::Key_Return || key == Qt::Key_Enter ||
            key == Qt::Key_Backspace || key == Qt::Key_Delete || isText)
            return true;
        return false;
    }

    // Enter: run, or continue an incomplete statement.
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (mods & Qt::ShiftModifier) {          // force a continuation line
            if (!cursorInInput()) moveCursorToInputEnd();
            m_term->insertPlainText("\n");
            return true;
        }
        evaluateInput();
        return true;
    }

    // History at the input region's edges (otherwise arrows move within input).
    QTextCursor startCur(m_term->document());
    startCur.setPosition(m_inputStart >= 0 ? m_inputStart : 0);
    const bool onFirstInputLine =
        (m_inputStart >= 0) && (m_term->textCursor().blockNumber() == startCur.blockNumber());
    const bool onLastLine =
        m_term->textCursor().blockNumber() == m_term->document()->blockCount() - 1;

    if (key == Qt::Key_Up && onFirstInputLine)   { recallHistory(-1); return true; }
    if (key == Qt::Key_Down && onLastLine)       { recallHistory(+1); return true; }

    // Home -> just after the prompt, when on the prompt line.
    if (key == Qt::Key_Home && !(mods & Qt::ShiftModifier) && onFirstInputLine) {
        QTextCursor c = m_term->textCursor();
        c.setPosition(m_inputStart);
        m_term->setTextCursor(c);
        return true;
    }

    // Backspace must not eat the prompt or transcript.
    if (key == Qt::Key_Backspace) {
        if (m_inputStart < 0) return true;
        if (!cursorInInput()) { moveCursorToInputEnd(); return true; }
        QTextCursor c = m_term->textCursor();
        if (!c.hasSelection() && c.position() <= m_inputStart) return true;  // block
        return false;
    }

    // Typing anywhere snaps into the input region first.
    if (isText) {
        if (m_inputStart < 0) return true;
        if (!cursorInInput()) moveCursorToInputEnd();
        return false;   // let the base widget insert the character
    }

    // Arrows / copy / page nav / selection: default behaviour (free movement,
    // so the transcript can be selected and copied).
    return false;
}

void PyConsole::handlePaste(const QMimeData *src)
{
    if (py_is_running() || m_inputStart < 0) return;
    if (!cursorInInput()) moveCursorToInputEnd();
    QString text = src->hasText() ? src->text() : QString();
    if (!text.isEmpty())
        m_term->insertPlainText(text);
}

void PyConsole::handleMouseRelease()
{
    if (m_inputStart < 0) return;                 // running: leave selection for copy
    QTextCursor c = m_term->textCursor();
    // A drag-selection is preserved (so transcript text can be copied); a bare
    // click that landed in the read-only region returns the caret to the input.
    if (!c.hasSelection() && c.position() < m_inputStart)
        moveCursorToInputEnd();
}

// --- script-run pump (unchanged behaviour) --------------------------------

void PyConsole::scriptTick()
{
    if (!m_stopPoll->isActive()) {
        m_stopPoll->start();
        emit scriptRunningChanged(true);
        // A run started outside the console (File>Run Script, IPC, dofile) while
        // a prompt sat idle: retire it so output starts on a fresh line and the
        // end-of-run poll knows to draw a new prompt. (Console-typed commands
        // already set m_inputStart = -1 before exec, so this is a no-op there.)
        if (m_inputStart >= 0) {
            ensureTrailingNewline();
            m_inputStart = -1;
        }
    }
    static QElapsedTimer since_pump;
    if (since_pump.isValid() && since_pump.elapsed() < 30)
        return;
    since_pump.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}
