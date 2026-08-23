// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// output_dialog.cpp — Small modeless results display dialog
// Ported concept from OF's output window, modernized for Qt.

#include "output_dialog.h"

#include <QVBoxLayout>
#include <QFont>
#include <QFontMetrics>
#include <QScreen>
#include <QApplication>

#include "mono_font.h"
OutputDialog::OutputDialog(QWidget *parent)
    : QDialog(parent, Qt::Tool)
{
    setWindowTitle(tr("Output"));

    m_text = new QTextEdit;
    m_text->setReadOnly(true);
    m_text->setFont(monoFont(9));
    m_text->setLineWrapMode(QTextEdit::NoWrap);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_text);

    resize(360, 200);
}

void OutputDialog::setOutput(const QString &text)
{
    m_text->setPlainText(text);

    // Auto-size to content, within reasonable bounds
    QFontMetrics fm(m_text->font());
    QStringList lines = text.split('\n');
    int maxWidth = 0;
    for (auto &line : lines) {
        int w = fm.horizontalAdvance(line);
        if (w > maxWidth) maxWidth = w;
    }
    int h = fm.height() * (lines.size() + 1) + 30;
    int w = maxWidth + 50;

    // Clamp to reasonable size
    QScreen *scr = QApplication::primaryScreen();
    int maxW = scr ? scr->availableGeometry().width() / 2 : 600;
    int maxH = scr ? scr->availableGeometry().height() / 2 : 400;
    if (w < 200) w = 200;
    if (h < 80) h = 80;
    if (w > maxW) w = maxW;
    if (h > maxH) h = maxH;

    resize(w, h);
    show();
    raise();
}

void OutputDialog::appendOutput(const QString &text)
{
    m_text->append(text);
    show();
    raise();
}

void OutputDialog::clear()
{
    m_text->clear();
}
