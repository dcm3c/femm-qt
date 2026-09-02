// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// material_lib_path.h - where the material libraries are read from and written
// to, on every platform.
#pragma once

#include <string>

// The four libraries are the only files femm-qt writes beside itself, and they
// are written more readily than it looks: MatLibDialogBase::accept() saves
// unconditionally, so merely opening the Material Library and pressing OK
// rewrites one. Everything else resolved through exeDir() -- the solvers, the
// PyBridge worker, python/ -- is read-only.
//
// Writing them beside the program is wrong wherever the program directory is
// not the user's: a signed .app is read-only; /usr/share on an FHS install is
// root-owned; c:\femm-qt is shared by every user of the machine; and
// linux_install's `rm -rf` deletes the lot on the next upgrade. So the writable
// copy is per-user on ALL platforms, and the program directory carries a
// read-only original.
//
// READS come from the user's copy when it exists and the shipped original
// otherwise. WRITES always go to the user's copy. That split is the whole
// design, and it is why a user who never edits a material keeps tracking the
// shipped library and receives new stock materials with each release: the fork
// happens on the first SAVE, not on the first read. Copying eagerly on read --
// which is what the first macOS implementation did -- froze every user's
// library the moment they so much as opened the dialog.
//
// `name` is one of "matlib.dat" (magnetics), "statlib.dat" (electrostatics),
// "heatlib.dat" (heat flow) or "condlib.dat" (current flow).

// Where to READ a library from: the user's copy if it exists, else the shipped
// original. Never creates anything.
std::string materialLibPath(const char *name);

// Where to WRITE a library: always the user's copy. Creates the directory, and
// on the first call for this library records the shipped original's hash as the
// seed, so a later change to the shipped library becomes detectable. Returns ""
// if the directory cannot be created, which the caller should report rather
// than write to a path that will not work.
std::string materialLibWritePath(const char *name);

// True when the shipped library differs from the one the user's copy forked
// from, and the user has not already been shown this version. False when there
// is no user copy (they are reading the shipped one already, so there is
// nothing to offer) or when nothing has changed.
//
// This exists because there is no other honest way to detect it. mtime cannot
// do it: the user's copy is rewritten by every dialog OK whether or not
// anything changed, and the shipped copy's mtime records when it was INSTALLED,
// not when its contents last changed -- `cp -R` stamps the current time. So a
// prompt driven by mtime both misses real updates and cries wolf on ones that
// never happened.
bool materialLibUpdateAvailable(const char *name);

// Record the shipped library as seen, so the user is not asked about this
// version again -- whether they took the update or declined it.
void materialLibMarkSeen(const char *name);

// The read-only original that ships with the program, wherever it lives on this
// platform. Exposed so the GUI can copy it over the user's copy when they accept
// an update; ordinary reads should use materialLibPath() instead.
std::string materialLibShippedPath(const char *name);
