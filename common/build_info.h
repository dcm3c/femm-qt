// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// build_info.h - which build of femm-qt is this?
//
// A user reporting a problem has to be able to say what they are running. The
// version (5.0.YYYYMMDD) names a release, but test builds are made between
// releases and all carry the last release's number. FEMM 4.2 told builds apart
// by the compile date in the About box (femm/femm.cpp, CAboutDlg::CAboutDlg:
// `m_compiledate = __DATE__;`), and femm-qt did the same until 2026-10-13.
//
// It now uses a BUILD NUMBER instead: the commit count of the source, e.g.
// 1108, with "-dirty" when tracked files had uncommitted changes. Unlike a
// date it names the code, not the day: two builds of the same commit agree,
// and a rebuild next week of unchanged code is not mistaken for a new one.
// Unlike a commit hash (considered and rejected earlier for this reason) it
// carries an order, so two testers can tell whose build is newer. The same
// number is in the installer and DMG names. cmake/BuildInfo.cmake says where
// it comes from, including for a build from the source zip, which has no .git.
//
// The implementation is GENERATED (common/build_info.cpp.in, written by
// cmake/BuildInfo.cmake on every build) rather than a plain source file. A
// constant compiled in by hand, or by __DATE__ in the old scheme, freezes at
// whenever that file was last compiled, and a file holding nothing but a
// version string is never edited, so it would go on claiming a stale build for
// months. That failure is silent, and it defeats the only purpose this has.
#pragma once

const char *femm_version();   // "5.0.20261013", project(VERSION) in full
const char *femm_build();     // "1108", or "1108-dirty"; "0" if unknown
