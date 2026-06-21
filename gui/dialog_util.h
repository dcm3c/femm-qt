// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// dialog_util.h — Small utilities for GUI dialogs
#ifndef DIALOG_UTIL_H
#define DIALOG_UTIL_H

#include <QDialog>
#include <QFontMetrics>

// Ensure the dialog is at least wide enough to display its window title.
// The padding accounts for the window icon, frame margins, and the
// minimize/maximize/close buttons rendered by the OS window manager.
inline void ensureTitleVisible(QDialog *dlg)
{
    int titleWidth = dlg->fontMetrics().horizontalAdvance(dlg->windowTitle()) + 200;
    if (titleWidth > dlg->minimumWidth())
        dlg->setMinimumWidth(titleWidth);
}

#endif // DIALOG_UTIL_H
