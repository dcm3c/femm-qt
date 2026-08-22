// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// material_lib_path.h - where the writable material libraries live.
#pragma once

#include <string>

// Absolute path to a writable material-library file. `name` is one of
// "matlib.dat" (magnetics), "statlib.dat" (electrostatics), "heatlib.dat"
// (heat flow) or "condlib.dat" (current flow).
//
// The four libraries are the only files femm-qt WRITES next to itself, and they
// are written more readily than it looks: MatLibDialogBase::accept() saves
// unconditionally, so merely opening the Material Library and pressing OK
// rewrites one. Everything else resolved through exeDir() -- the solvers, the
// PyBridge worker, the python/ package directory -- is read-only.
//
// On macOS that is fatal to a distributed app. A code-signed bundle in
// /Applications is effectively read-only: a write into Contents/MacOS/
// invalidates the signature, needs admin rights, and under App Translocation
// may not even land where it was aimed. So on macOS the writable copy moves to
// the per-user location and the bundle carries only a read-only original to
// seed it from. See docs/macos-appsupport-material-libs.md.
//
// Windows and Linux keep resolving to exeDir() for now. Neither is broken:
// the Windows installer deliberately targets c:\femm-qt with
// PrivilegesRequired=lowest rather than Program Files, and Linux users build
// into their own tree. Both would gain a branch here rather than a new
// mechanism -- which is the point of having one resolver.
std::string materialLibPath(const char *name);
