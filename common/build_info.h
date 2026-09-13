// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// build_info.h - which build of femm-qt is this?
//
// A user reporting a problem has to be able to say what they are running, and
// "5.0" is not enough once there is more than one 5.0 in circulation. FEMM 4.2
// answered this with the compile date in the About box (femm/femm.cpp,
// CAboutDlg::CAboutDlg: `m_compiledate = __DATE__;`) and this is the same
// answer, written the way the source archives already spell a date:
// "09Sep2026", which is what packaging/buildsource.bat puts in the zip name.
// So the About box and the file a tester downloaded agree on sight.
//
// A commit hash was considered and rejected: nobody outside the maintainer's
// machine can resolve one, and unlike a date it carries no order, so two
// testers could not tell which of them had the newer build.
//
// The implementation is GENERATED (common/build_info.cpp.in, written by
// cmake/BuildInfo.cmake on every build) rather than a plain source file holding
// __DATE__. The reason is that __DATE__ is the date its own translation unit
// was last compiled, and a file containing nothing but a version string is
// never edited, so it would freeze at whenever the tree was first built and go
// on claiming that date for months. That failure is silent, and it defeats the
// only purpose this file has.
#pragma once

const char *femm_version();      // "5.0"
const char *femm_build_date();   // "09Sep2026"
