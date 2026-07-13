// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_console.h - Python interactive console as a dock widget.
//
// Single-pane terminal REPL: one scrollback in which the user types after a
// ">>> " prompt. Everything above the current prompt is read-only transcript;
// Enter runs a complete statement (or starts a "... " continuation line for an
// incomplete one, via py_repl_continue); Up/Down recall history.
//
#ifndef PY_CONSOLE_H
#define PY_CONSOLE_H

#include <QDockWidget>
#include <QStringList>

class QTimer;
class QKeyEvent;
class QMimeData;
class ConsoleEdit;

class PyConsole : public QDockWidget
{
    Q_OBJECT

public:
    explicit PyConsole(QWidget *parent = nullptr);

    // Append a paragraph (errors, REPL result) on its own line.
    void appendOutput(const QString &text);
    // Append raw bytes from the MicroPython output sink (print/tracebacks).
    void writeRaw(const char *s, int n);
    // Clear the scrollback and redraw a fresh prompt (clearconsole command).
    void clearOutput();

    // Called from the MicroPython VM-loop hook while any script runs. Pumps the
    // Qt event loop (rate-limited) so the UI stays live and Esc can interrupt,
    // and signals start/end via scriptRunningChanged.
    void scriptTick();

    // Called by ConsoleEdit (the inner widget) to delegate input handling.
    // Returns true if the event was consumed.
    bool handleKeyPress(QKeyEvent *e);
    void handlePaste(const QMimeData *src);
    // After a mouse click: a plain click in the read-only transcript returns the
    // caret to the input line; a drag-selection is left alone (so it can copy).
    void handleMouseRelease();

signals:
    void scriptRunningChanged(bool running);

private:
    ConsoleEdit *m_term = nullptr;
    QTimer *m_stopPoll = nullptr;   // detects when a running script has finished
    QStringList m_history;
    int m_historyIdx = -1;
    int m_inputStart = -1;          // doc pos where editable input begins; -1 = none/running

    // --- REPL plumbing ---
    void drawPrompt();              // append ">>> " and arm the input region
    void evaluateInput();           // run (or continue) the current input
    QString inputText() const;      // text from m_inputStart to end of document
    void setInputText(const QString &s);
    void ensureTrailingNewline();
    bool cursorInInput() const;     // is the edit cursor within the input region?
    void moveCursorToInputEnd();
    void recallHistory(int dir);    // dir -1 = older, +1 = newer
};

// Global accessor (set when console is created).
PyConsole *pyConsoleInstance();

#endif  // PY_CONSOLE_H
