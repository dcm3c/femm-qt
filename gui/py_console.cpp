// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_console.cpp - Python interactive console.
//

#include "py_console.h"
#include "py_state.h"
// NB: deliberately does NOT include py_bind.h. Pulling the MicroPython headers
// into a Qt translation unit collides with Qt's `slots`/`signals`/`emit`
// keyword macros (obj.h uses `slots` as a struct field). py_state.h is clean.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

// Global console instance for the output sink and registered functions.
static PyConsole *s_console = nullptr;

PyConsole *pyConsoleInstance() { return s_console; }

// MicroPython stdout sink -> console output (print, tracebacks, sys.stdout).
static void py_console_sink(const char *s, size_t n) {
    if (s_console) s_console->writeRaw(s, (int)n);
}

// MicroPython VM-loop hook (fires every 1024 bytecode branches while a script
// executes, on the GUI thread — all exec_python call sites are GUI-thread).
static void py_console_vm_hook() {
    if (s_console) s_console->scriptTick();
}

PyConsole::PyConsole(QWidget *parent)
    : QDockWidget(tr("Python Console"), parent)
{
    s_console = this;

    auto *container = new QWidget;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    m_output = new QPlainTextEdit;
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    m_output->setFont(QFont("Consolas", 9));
    layout->addWidget(m_output, 1);

    m_input = new QPlainTextEdit;
    m_input->setFont(QFont("Consolas", 9));
    m_input->setPlaceholderText(tr("Enter Python command... (Shift+Enter to run)"));
    m_input->setMaximumHeight(QFontMetrics(m_input->font()).lineSpacing() * 4 + 10);
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    setWidget(container);

    // While a script runs, this poll notices when it finishes (its timer
    // events arrive via the main loop, or via our own pumping in scriptTick
    // while a script is still executing) and signals the end.
    m_stopPoll = new QTimer(this);
    m_stopPoll->setInterval(100);
    connect(m_stopPoll, &QTimer::timeout, this, [this]() {
        if (!py_is_running()) {
            m_stopPoll->stop();
            emit scriptRunningChanged(false);
        }
    });

    // Route all MicroPython stdout (print/tracebacks/REPL) into this console.
    set_py_output_sink(py_console_sink);
    // Pump the GUI from the VM loop so long scripts don't freeze the UI and
    // can be interrupted.
    set_py_vm_hook(py_console_vm_hook);
}

void PyConsole::scriptTick()
{
    if (!m_stopPoll->isActive()) {   // first tick of this run
        m_stopPoll->start();
        emit scriptRunningChanged(true);
    }
    // Rate-limit the pump: the hook fires every 1024 bytecode branches, but
    // pumping more often than ~30 ms buys nothing and costs script speed.
    static QElapsedTimer since_pump;
    if (since_pump.isValid() && since_pump.elapsed() < 30)
        return;
    since_pump.start();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void PyConsole::appendOutput(const QString &text)
{
    m_output->appendPlainText(text);
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void PyConsole::writeRaw(const char *s, int n)
{
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(QString::fromUtf8(s, n));
    m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
}

void PyConsole::clearOutput()
{
    m_output->clear();
}

void PyConsole::onEvaluate()
{
    // Event pumping (scriptTick) means the user can still type here while a
    // script runs; don't re-enter the interpreter from the console.
    if (py_is_running()) {
        appendOutput(tr("(a script is running — press Esc to interrupt it)"));
        return;
    }

    QString cmd = m_input->toPlainText().trimmed();
    if (cmd.isEmpty()) return;

    // Add to history
    m_history.prepend(cmd);
    if (m_history.size() > 200) m_history.removeLast();
    m_historyIdx = -1;

    // Echo command to output
    appendOutput(cmd);
    m_input->clear();

    // Execute. print()/traceback output arrives via the sink (writeRaw)
    // during the call; the expression's value (if any) is echoed after.
    char errmsg[1024] = {};
    int status = exec_python(cmd.toStdString().c_str(), errmsg, sizeof(errmsg));
    if (status != 0)
        appendOutput(QString("Error: %1").arg(errmsg));
    else if (!py_last_display().empty())
        appendOutput(QString::fromUtf8(py_last_display().c_str()));
}

bool PyConsole::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // (Esc-to-interrupt is handled application-wide by MainWindow's event
        // filter, so it works whether or not the console has focus.)
        // Shift+Enter or Ctrl+Enter executes
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            onEvaluate();
            return true;
        }
        // Up/Down navigate history when input is a single line
        if (ke->key() == Qt::Key_Up && m_input->document()->blockCount() <= 1) {
            if (m_historyIdx + 1 < m_history.size()) {
                m_historyIdx++;
                m_input->setPlainText(m_history[m_historyIdx]);
                m_input->moveCursor(QTextCursor::End);
            }
            return true;
        }
        if (ke->key() == Qt::Key_Down && m_input->document()->blockCount() <= 1) {
            if (m_historyIdx > 0) {
                m_historyIdx--;
                m_input->setPlainText(m_history[m_historyIdx]);
                m_input->moveCursor(QTextCursor::End);
            } else if (m_historyIdx == 0) {
                m_historyIdx = -1;
                m_input->clear();
            }
            return true;
        }
    }
    return QDockWidget::eventFilter(obj, event);
}
