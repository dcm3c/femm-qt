// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_os.h - the `os` and `os.path` modules for the embedded interpreter.
//
// Why femm-qt implements these rather than enabling MicroPython's own: every
// call in extmod/vfs_posix.c reaches the filesystem through the narrow POSIX
// entry points, which on Windows decode as ANSI. os.mkdir("C:/temp/Prufung")
// with an umlaut would create a DIFFERENT directory from the one mi_saveas()
// writes into, since libfemm widens to UTF-16 at the OS boundary (utf8_io.h).
// The two would round-trip consistently within Python and disagree with the
// rest of the program -- the worst kind of wrong. These implementations use
// the same helpers the document code does, so a path means one thing.
#ifndef PY_OS_H
#define PY_OS_H

#include <string>

// Build the `os` and `os.path` modules. Called once from init_py(), after the
// VFS is mounted (open() and os.* then agree about the working directory).
void register_os_py();

// Change the process working directory. The single implementation behind both
// os.chdir and the FEMM-native setcurrentdirectory/chdir globals -- one state,
// one way to set it. Throws on failure. `path` is UTF-8, already fixpath'd.
void femm_os_chdir(const std::string &path);

#endif  // PY_OS_H
