// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// script_prefs_dialog.h - which CPython the matplotlib bridge should use.
//
// Application-wide rather than per-document, which is why it is not part of
// the pre/post preference dialogs: it configures the scripting engine, not a
// drawing. Settings persist in QSettings and are pushed into libfemm, which
// holds no Qt and so cannot read them itself.
#ifndef SCRIPT_PREFS_DIALOG_H
#define SCRIPT_PREFS_DIALOG_H

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class ScriptPrefsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScriptPrefsDialog(QWidget *parent = nullptr);

    // Read the stored preferences and apply them to the bridge. Called once at
    // startup, before any script runs.
    static void applySaved();

private slots:
    void onBrowse();
    void onDetect();
    void onAccepted();

private:
    void refreshStatus();

    QLineEdit *m_interpreter;
    QComboBox *m_backend;
    QLabel *m_status;
};

#endif  // SCRIPT_PREFS_DIALOG_H
