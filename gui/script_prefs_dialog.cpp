// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// script_prefs_dialog.cpp - see script_prefs_dialog.h.
#include "script_prefs_dialog.h"

#include "pybridge_host.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
const char *kInterpreterKey = "pybridge/interpreter";
const char *kBackendKey = "pybridge/backend";
}  // namespace

void ScriptPrefsDialog::applySaved()
{
    QSettings s;
    femm_pybridge_set_interpreter(
        s.value(kInterpreterKey).toString().toStdString());
    femm_pybridge_set_backend(s.value(kBackendKey).toString().toStdString());
}

ScriptPrefsDialog::ScriptPrefsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Scripting Preferences"));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_interpreter = new QLineEdit;
    m_interpreter->setPlaceholderText(tr("(detect automatically)"));
    m_interpreter->setMinimumWidth(340);
    auto *browse = new QPushButton(tr("Browse..."));
    connect(browse, &QPushButton::clicked, this, &ScriptPrefsDialog::onBrowse);
    auto *row = new QHBoxLayout;
    row->addWidget(m_interpreter, 1);
    row->addWidget(browse);
    form->addRow(tr("Python interpreter:"), row);

    m_backend = new QComboBox;
    m_backend->addItem(tr("Automatic"), QString());
    m_backend->addItem("qtagg", "qtagg");
    m_backend->addItem("tkagg", "tkagg");
#ifdef Q_OS_MACOS
    m_backend->addItem("macosx", "macosx");
#endif
    form->addRow(tr("Plot window backend:"), m_backend);
    layout->addLayout(form);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    auto *detect = new QPushButton(tr("Restart && Re-detect"));
    connect(detect, &QPushButton::clicked, this, &ScriptPrefsDialog::onDetect);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel);
    buttons->addButton(detect, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::accepted, this, &ScriptPrefsDialog::onAccepted);
    layout->addWidget(buttons);

    QSettings s;
    m_interpreter->setText(s.value(kInterpreterKey).toString());
    int idx = m_backend->findData(s.value(kBackendKey).toString());
    m_backend->setCurrentIndex(idx < 0 ? 0 : idx);
    refreshStatus();
}

void ScriptPrefsDialog::refreshStatus()
{
    if (femm_pybridge_running()) {
        m_status->setText(tr("Running: %1")
            .arg(QString::fromStdString(femm_pybridge_interpreter())));
        return;
    }
    const auto &tried = femm_pybridge_attempts();
    if (tried.empty()) {
        // Nothing has been attempted yet: the worker starts on first use, so
        // saying "not running" here would read as a fault when it is not one.
        m_status->setText(tr("The plotting bridge starts when a script first "
                             "imports matplotlib."));
        return;
    }
    QString why = tr("No usable Python was found. Tried:");
    for (const auto &t : tried)
        why += "\n    " + QString::fromStdString(t);
    m_status->setText(why);
}

void ScriptPrefsDialog::onBrowse()
{
#ifdef Q_OS_WIN
    const QString filter = tr("Executables (*.exe);;All files (*)");
#else
    const QString filter = tr("All files (*)");
#endif
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select a Python interpreter"), m_interpreter->text(), filter);
    if (!path.isEmpty()) m_interpreter->setText(path);
}

void ScriptPrefsDialog::onAccepted()
{
    QSettings s;
    s.setValue(kInterpreterKey, m_interpreter->text());
    s.setValue(kBackendKey, m_backend->currentData().toString());
    applySaved();
}

void ScriptPrefsDialog::onDetect()
{
    // Apply what is on screen first, so "re-detect" tests the settings the
    // user is looking at rather than the ones last saved.
    QSettings s;
    s.setValue(kInterpreterKey, m_interpreter->text());
    s.setValue(kBackendKey, m_backend->currentData().toString());
    applySaved();
    femm_pybridge_restart();
    try {
        femm_pybridge_ensure_started();
    } catch (const pybridge_error &) {
        // The attempt list carries the detail; refreshStatus() shows it.
    }
    refreshStatus();
}
