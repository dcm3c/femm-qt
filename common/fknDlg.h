// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// fknDlg.h — CLI stub replacing MFC dialog
// Renders console progress bars and status text to stderr.
#ifndef FKNDLG_H
#define FKNDLG_H

#include "StdAfx.h"
#include "resource.h"
#include <functional>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

// Console progress bar
struct CProgressCtrl {
    int pos;
    std::function<void(int)> onProgress;
    CProgressCtrl() : pos(0) {}
    void SetPos(int p) {
        pos = p; if (pos < 0) pos = 0; if (pos > 100) pos = 100;
        if (onProgress) onProgress(pos);
    }
};

// Console dialog — renders progress bars to stderr, updated in-place.
// Uses ANSI cursor-up (\033[A) to overwrite two lines when both bars active.
class CFknDlg {
    char label1[256];
    char label2[256];
    bool started;     // true after first render (so we know to emit initial \n)
    bool twoLines;    // true if last render used two lines
    bool vtEnabled;   // true if VT100 sequences are available
    bool piped;       // true if stderr is a pipe (not a terminal)

    static bool isTTY() {
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode = 0;
        return GetConsoleMode(h, &mode) != 0;
#else
        return isatty(fileno(stderr));
#endif
    }

    static bool enableVT() {
#ifdef _WIN32
        HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode)) return false;
        return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
        return true;  // Unix terminals support ANSI by default
#endif
    }

    void formatBar(int pct, char *buf) {
        int filled = (pct * 20 + 50) / 100;
        buf[0] = '[';
        for (int i = 0; i < 20; i++)
            buf[1+i] = (i < filled) ? '#' : '.';
        buf[21] = ']';
        buf[22] = 0;
    }

    void padTo(char *s, int width) {
        int len = (int)strlen(s);
        while (len < width) s[len++] = ' ';
        s[len] = 0;
    }

    // Format one progress line: fixed-width prefix, bar, pct, then label text
    // e.g. "Solver:  [##########..........] 50%  QMR Solver"
    void formatLine(char *out, int outsz, const char *prefix,
                    int pct, const char *label) {
        char bar[64];
        formatBar(pct, bar);
        snprintf(out, outsz, "%-9s %s %3d%%  %s", prefix, bar, pct, label);
        padTo(out, 75);
    }

    void render() {
        if (piped) {
            // Piped output: each bar on its own line for easy parsing.
            // No \r overwrites — the parent process reads complete lines.
            char line[512];
            formatLine(line, sizeof(line), "Solver:", m_prog1.pos, label1);
            fprintf(stderr, "%s\n", line);
            if (label2[0]) {
                formatLine(line, sizeof(line), "Overall:", m_prog2.pos, label2);
                fprintf(stderr, "%s\n", line);
            }
            fflush(stderr);
            return;
        }

        // First render: emit \n so we don't overwrite the line above us
        if (!started) { fprintf(stderr, "\n"); started = true; }
        // If we previously wrote two lines, move cursor up to overwrite both
        if (twoLines && vtEnabled)
            fprintf(stderr, "\033[A");

        char line[512];
        formatLine(line, sizeof(line), "Solver:", m_prog1.pos, label1);

        if (label2[0] && vtEnabled) {
            // Two separate lines, cursor-up on next render
            fprintf(stderr, "\r%s\n", line);
            formatLine(line, sizeof(line), "Overall:", m_prog2.pos, label2);
            fprintf(stderr, "\r%s", line);
            twoLines = true;
        } else if (label2[0]) {
            // No VT support — fall back to single line
            fprintf(stderr, "\r%s | ", line);
            formatLine(line, sizeof(line), "Overall:", m_prog2.pos, label2);
            fprintf(stderr, "%s", line);
            twoLines = false;
        } else {
            fprintf(stderr, "\r%s", line);
            twoLines = false;
        }
        fflush(stderr);
    }

public:
    CProgressCtrl m_prog1;
    CProgressCtrl m_prog2;
    CString ComLine;

    CFknDlg() : started(false), twoLines(false) {
        label1[0] = 0; label2[0] = 0;
        piped = !isTTY();
        vtEnabled = piped ? false : enableVT();
    }

    void SetDlgItemText(int id, const char* text) {
        if (id == IDC_FRAME1) {
            strncpy(label1, text, 255); label1[255] = 0;
        }
        else if (id == IDC_FRAME2) {
            strncpy(label2, text, 255); label2[255] = 0;
        }
        render();
    }
    void SetWindowText(const char* text) {
        fprintf(stderr, "\r%s\n", text);
    }
    void InvalidateRect(void* p, int b) { (void)p; (void)b; }
    void UpdateWindow() { render(); }
};

#endif
