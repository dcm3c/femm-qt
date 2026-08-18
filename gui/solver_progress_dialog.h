// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// solver_progress_dialog.h — Dialog showing solver progress with abort capability
#ifndef SOLVER_PROGRESS_DIALOG_H
#define SOLVER_PROGRESS_DIALOG_H

#include <QDialog>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QProcess>
#include <QVBoxLayout>

class SolverProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SolverProgressDialog(QWidget *parent = nullptr);

    // Launch solver and block until finished (pumping events).
    // Returns process exit code, or -1 if aborted.
    int runSolver(const QString &exePath, const QString &basePath);

    // True if the last visible run ended badly and the user has already been
    // told -- either by the modal failure state of this dialog (which shows the
    // solver's own diagnostic) or by their own Abort click. Callers use it to
    // suppress a second, less informative pop-up. Reading consumes the flag, so
    // it cannot leak into a later run.
    static bool takeFailureReported();

private slots:
    void onReadyRead();
    void onAbort();
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    void parseLine(const QString &line);

    QLabel *m_statusLabel;       // Top: solver status text
    QLabel *m_solverLabel;       // "Linear Solver"
    QProgressBar *m_solverBar;   // CG/QMR iteration progress
    QLabel *m_overallLabel;      // "Newton Iteration"
    QProgressBar *m_overallBar;  // Newton-Raphson / overall progress
    QPlainTextEdit *m_log;
    QPushButton *m_abortBtn;
    QProcess *m_proc;
    int m_exitCode;
    bool m_aborted;
    bool m_finished;
    QString m_partialLine;
};

#endif // SOLVER_PROGRESS_DIALOG_H
