// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

// StdAfx.cpp — CLI MsgBox implementations (print to stderr)

#include "StdAfx.h"

int MsgBox(CString s)
{
    fprintf(stderr, "%s\n", (const char*)s);
    return IDOK;
}

int MsgBox(PSTR sz, ...)
{
    va_list args;
    va_start(args, sz);
    vfprintf(stderr, sz, args);
    va_end(args);
    fprintf(stderr, "\n");
    return IDOK;
}
