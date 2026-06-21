// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// output_dialog.h — Small modeless dialog for displaying multi-line results
// Stays on top without stealing focus. Content is selectable/copyable.
#ifndef OUTPUT_DIALOG_H
#define OUTPUT_DIALOG_H

#include <QDialog>
#include <QTextEdit>

class OutputDialog : public QDialog {
    Q_OBJECT
public:
    explicit OutputDialog(QWidget *parent = nullptr);

    // Set the text content (replaces previous)
    void setOutput(const QString &text);

    // Append text
    void appendOutput(const QString &text);

    // Clear
    void clear();

private:
    QTextEdit *m_text;
};

#endif // OUTPUT_DIALOG_H
