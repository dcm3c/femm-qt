// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// StdAfx.h — CLI replacement for MFC precompiled header
// Provides lightweight CString compatibility and MsgBox via stderr

#ifndef STDAFX_H
#define STDAFX_H

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <string>

// Windows type compatibility
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef PSTR
typedef char* PSTR;
#endif

#ifndef _WIN32
#include <strings.h>  // strncasecmp
#define _strnicmp strncasecmp
#endif

// Lightweight CString replacement
class CString {
    std::string s;
public:
    CString() {}
    CString(const char* p) : s(p ? p : "") {}
    CString(const std::string& str) : s(str) {}

    operator const char*() const { return s.c_str(); }
    const char* operator=(const char* p) { s = p ? p : ""; return s.c_str(); }
    CString& operator=(const CString& o) { s = o.s; return *this; }
    CString& operator+=(const char* p) { if(p) s += p; return *this; }
    CString& operator+=(const CString& o) { s += o.s; return *this; }
    CString operator+(const char* p) const { CString r(*this); r.s += (p?p:""); return r; }
    friend CString operator+(const char* p, const CString& c) { CString r(p); r.s += c.s; return r; }
    bool operator==(const char* p) const { return s == (p?p:""); }
    bool operator==(const CString& o) const { return s == o.s; }
    bool operator!=(const char* p) const { return !(*this == p); }

    int GetLength() const { return (int)s.size(); }
    bool IsEmpty() const { return s.empty(); }
    void Empty() { s.clear(); }
    CString Left(int n) const { return CString(s.substr(0, n).c_str()); }
    CString Mid(int pos) const { return CString(s.substr(pos).c_str()); }
    CString Mid(int pos, int len) const { return CString(s.substr(pos, len).c_str()); }
    int Find(const char* sub, int start=0) const {
        auto p = s.find(sub, start);
        return p == std::string::npos ? -1 : (int)p;
    }
    int ReverseFind(char c) const {
        auto p = s.rfind(c);
        return p == std::string::npos ? -1 : (int)p;
    }
    void Format(const char* fmt, ...) {
        char buf[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        s = buf;
    }
    void FormatV(const char* fmt, va_list args) {
        char buf[2048];
        vsnprintf(buf, sizeof(buf), fmt, args);
        s = buf;
    }
};

// MsgBox → stderr
int MsgBox(CString s);
int MsgBox(PSTR sz, ...);

#define IDOK 1
#define IDCANCEL 2

// Windows API compatibility
#ifndef DeleteFile
#define DeleteFile(f) remove(f)
#endif

#ifndef Sleep
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(ms) usleep((ms)*1000)
#endif
#endif

// Suppress IsWindow check (always true in CLI mode)
#ifndef IsWindow
#define IsWindow(h) (1)
#endif

#endif // STDAFX_H
