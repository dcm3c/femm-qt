// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// solver_progress_dialog.cpp — Dialog showing solver progress with abort capability

#include "solver_progress_dialog.h"
#include <QApplication>
#include <QRegularExpression>

// Set by runSolver whenever a visible run ends non-zero; consumed by
// takeFailureReported(). See the header for why it exists.
static bool s_failureReported = false;

bool SolverProgressDialog::takeFailureReported()
{
    bool reported = s_failureReported;
    s_failureReported = false;
    return reported;
}

SolverProgressDialog::SolverProgressDialog(QWidget *parent)
    : QDialog(parent), m_proc(nullptr), m_exitCode(-1),
      m_aborted(false), m_finished(false)
{
    setWindowTitle(tr("Solver"));
    setMinimumWidth(500);

    auto *layout = new QVBoxLayout(this);

    // Row 1: status text (solver name, phase, statistics)
    m_statusLabel = new QLabel(tr("Starting solver..."));
    QFont boldFont = m_statusLabel->font();
    boldFont.setBold(true);
    m_statusLabel->setFont(boldFont);

    // Row 2: linear solver progress (CG/QMR)
    m_solverLabel = new QLabel(tr("Linear Solver:"));
    m_solverBar = new QProgressBar;
    m_solverBar->setRange(0, 100);
    m_solverBar->setValue(0);

    // Row 3: Newton-Raphson / overall progress
    m_overallLabel = new QLabel(tr("Overall:"));
    m_overallBar = new QProgressBar;
    m_overallBar->setRange(0, 100);
    m_overallBar->setValue(0);
    m_overallLabel->setVisible(false);
    m_overallBar->setVisible(false);

    // Log area for detailed output
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFont(QFont("Consolas", 9));
    m_log->setMaximumHeight(160);

    m_abortBtn = new QPushButton(tr("Abort"));

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_solverLabel);
    layout->addWidget(m_solverBar);
    layout->addWidget(m_overallLabel);
    layout->addWidget(m_overallBar);
    layout->addWidget(m_log);
    layout->addWidget(m_abortBtn);

    connect(m_abortBtn, &QPushButton::clicked, this, &SolverProgressDialog::onAbort);
}

int SolverProgressDialog::runSolver(const QString &exePath, const QString &basePath)
{
    m_exitCode = -1;
    m_aborted = false;
    m_finished = false;
    m_partialLine.clear();
    s_failureReported = false;

    m_proc = new QProcess(this);
    m_proc->setProgram(exePath);
    m_proc->setArguments({ basePath });
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::readyReadStandardOutput,
            this, &SolverProgressDialog::onReadyRead);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &SolverProgressDialog::onFinished);
    // finished() is never emitted if the process fails to start (a missing or
    // unrunnable solver executable), so without this the wait loop below spins
    // forever. Report it in the log like any other failure.
    connect(m_proc, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) return;
                m_log->appendPlainText(tr("Could not start the solver: %1")
                                           .arg(m_proc->program()));
                m_finished = true;
            });

    m_proc->start();
    show();

    // Pump events until the process finishes
    while (!m_finished)
        QApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);

    // Drain any remaining output
    onReadyRead();

    delete m_proc;
    m_proc = nullptr;

    // On failure, keep the dialog up so the solver's captured output (the log
    // pane, which holds the actual diagnostic like "Meshing failed") stays
    // visible instead of vanishing the instant the process exits. Turn the
    // Abort button into Close and wait modally for the user to dismiss it.
    // (A user-initiated abort is excluded -- they already know it stopped.)
    if (m_exitCode != 0 && !m_aborted) {
        m_statusLabel->setText(tr("Solver FAILED (exit code %1) - see the log below")
                                   .arg(m_exitCode));
        m_solverLabel->setVisible(false);
        m_solverBar->setVisible(false);
        m_overallLabel->setVisible(false);
        m_overallBar->setVisible(false);
        m_abortBtn->setText(tr("Close"));
        disconnect(m_abortBtn, &QPushButton::clicked, this, &SolverProgressDialog::onAbort);
        connect(m_abortBtn, &QPushButton::clicked, this, &QDialog::accept);
        exec();  // modal until the user closes it
    }

    // Either the user read the failure above, or they aborted the run
    // themselves. Both mean the caller need not say it again.
    if (m_exitCode != 0) s_failureReported = true;

    hide();
    return m_exitCode;
}

void SolverProgressDialog::onReadyRead()
{
    if (!m_proc) return;
    QByteArray data = m_proc->readAll();
    if (data.isEmpty()) return;

    // The solver uses \r to overwrite progress lines in-place and \n for
    // actual new lines.  Split on both so we can parse each fragment.
    QString text = m_partialLine + QString::fromLocal8Bit(data);
    m_partialLine.clear();

    // Split on \n first, then handle \r within each chunk
    QStringList newlineChunks = text.split('\n');

    for (int i = 0; i < newlineChunks.size(); i++) {
        QString chunk = newlineChunks[i];
        bool isLast = (i == newlineChunks.size() - 1);

        // The last chunk may be incomplete (no trailing \n yet)
        if (isLast && !text.endsWith('\n')) {
            // Split on \r — only the last fragment matters (earlier ones
            // were overwritten in the console)
            QStringList crParts = chunk.split('\r');
            // Parse every \r fragment for progress (updates bars)
            for (const QString &part : crParts) {
                QString trimmed = part.trimmed();
                if (!trimmed.isEmpty()) parseLine(trimmed);
            }
            // Save the last fragment as partial for next read
            m_partialLine = crParts.last();
            continue;
        }

        // Complete line — split on \r and process each overwrite
        QStringList crParts = chunk.split('\r');
        for (const QString &part : crParts) {
            QString trimmed = part.trimmed();
            if (!trimmed.isEmpty()) parseLine(trimmed);
        }
    }
}

void SolverProgressDialog::parseLine(const QString &line)
{
    // Progress bar format from CFknDlg::formatLine:
    //   "Solver:   [##########..........] 50%  Conjugate Gradient Solver"
    //   "Overall:  [####................] 20%  Newton Iteration(3) Relax=1.0000"
    static QRegularExpression rxProgress(
        R"(^(Solver|Overall):\s*\[.{20}\]\s*(\d+)%\s*(.*))");

    auto m = rxProgress.match(line);
    if (m.hasMatch()) {
        QString which = m.captured(1);
        int pct = m.captured(2).toInt();
        QString label = m.captured(3).trimmed();

        if (which == "Solver") {
            m_solverBar->setValue(pct);
            if (!label.isEmpty())
                m_solverLabel->setText(label + ":");
        } else {
            if (!m_overallLabel->isVisible()) {
                m_overallLabel->setVisible(true);
                m_overallBar->setVisible(true);
                // Grow the dialog to accommodate the new row
                adjustSize();
            }
            m_overallBar->setValue(pct);
            if (!label.isEmpty())
                m_overallLabel->setText(label + ":");
        }
        return;
    }

    // ANSI escape sequences — strip them
    static QRegularExpression rxAnsi(R"(\x1b\[[0-9;]*[A-Za-z])");
    QString clean = line;
    clean.remove(rxAnsi);
    clean = clean.trimmed();
    if (clean.isEmpty()) return;

    // Solver identification line (e.g. "RadialMagneticBearing - fkern")
    // goes into the bold status label at the top.
    static QRegularExpression rxSolverName(
        R"(^.+\s+-\s+(fkern|belasolve|hsolve|csolve)\s*$)");
    if (rxSolverName.match(clean).hasMatch())
        m_statusLabel->setText(clean);

    // Everything goes to the log
    m_log->appendPlainText(clean);
}

void SolverProgressDialog::onAbort()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_aborted = true;
        m_proc->kill();
        m_log->appendPlainText(tr("--- Solver aborted by user ---"));
    }
}

void SolverProgressDialog::onFinished(int exitCode, QProcess::ExitStatus status)
{
    (void)status;
    m_exitCode = m_aborted ? -1 : exitCode;
    m_finished = true;
}
