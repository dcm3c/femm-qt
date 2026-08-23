// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// monoFont(pt) -- the fixed-width font for consoles, solver logs and any other
// widget whose contents are meant to line up in columns.
//
// It exists because "Consolas" was hardcoded in eight places. That is a
// Windows font and ships nowhere else, so on macOS and Linux Qt could not
// resolve it and fell back to the DEFAULT family -- which is proportional.
// Numeric output that is supposed to align in columns did not, and Qt printed
//
//   qt.qpa.fonts: Populating font family aliases took 56 ms. Replace uses of
//   missing font family "Consolas" with one that exists to avoid this cost.
//
// on every start, because resolving a missing family makes Qt walk every font
// on the system. Reported from an M1, 2026-08-23.
//
// Each platform names a font it actually has, rather than one list with
// fallbacks: a QFont whose first family is missing pays that same lookup even
// when a later entry would match. Windows keeps Consolas, so nothing changes
// there. Menlo has shipped with macOS since 10.6. Elsewhere Qt is asked for
// the system's own fixed font, which is the only portable way to phrase the
// question -- DejaVu Sans Mono on most Linux systems, whatever the desktop
// prefers otherwise.
//
// The Monospace style hint is set regardless, so that a system missing even
// its own default still gets something fixed-width rather than the UI font.
#ifndef FEMM_MONO_FONT_H
#define FEMM_MONO_FONT_H

#include <QFont>
#include <QFontDatabase>

inline QFont monoFont(int pointSize)
{
#if defined(Q_OS_WIN)
    QFont f(QStringLiteral("Consolas"), pointSize);
#elif defined(Q_OS_MAC)
    QFont f(QStringLiteral("Menlo"), pointSize);
#else
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(pointSize);
#endif
    f.setStyleHint(QFont::Monospace);
    return f;
}

#endif // FEMM_MONO_FONT_H
