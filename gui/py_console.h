// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_console.h - Python interactive console as a dock widget
//
#ifndef PY_CONSOLE_H
#define PY_CONSOLE_H

#include <QDockWidget>
#include <QPlainTextEdit>
#include <QStringList>

class QTimer;

class PyConsole : public QDockWidget
{
    Q_OBJECT

public:
    explicit PyConsole(QWidget *parent = nullptr);

    // Append a paragraph (command echo, errors, REPL result).
    void appendOutput(const QString &text);
    // Append raw bytes from the MicroPython output sink (print/tracebacks),
    // without inserting an extra paragraph break.
    void writeRaw(const char *s, int n);
    // Clear the output area. The GUI view-command handler routes the
    // showconsole/hideconsole/clearconsole commands (registered in init_py)
    // to show()/hide()/clearOutput() on pyConsoleInstance().
    void clearOutput();

    // Called from the MicroPython VM-loop hook while any script runs (console
    // command, File>Run Script, IPC). Pumps the Qt event loop (rate-limited)
    // so the UI stays live and Esc can interrupt, and signals start/end via
    // scriptRunningChanged.
    void scriptTick();

signals:
    // Emitted (from the VM-loop hook) when interpreter execution starts
    // (true) and finishes (false). MainWindow uses this to show/clear the
    // "press Esc to stop" status message, independent of console visibility.
    void scriptRunningChanged(bool running);

private slots:
    void onEvaluate();

private:
    QPlainTextEdit *m_output;
    QPlainTextEdit *m_input;
    QTimer *m_stopPoll;   // detects when the running script has finished
    QStringList m_history;
    int m_historyIdx = -1;

    bool eventFilter(QObject *obj, QEvent *event) override;
};

// Global accessor (set when console is created).
PyConsole *pyConsoleInstance();

#endif  // PY_CONSOLE_H
