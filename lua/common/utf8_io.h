// SPDX-License-Identifier: MIT
#ifndef FEMM_UTF8_IO_H
#define FEMM_UTF8_IO_H

// UTF-8 filesystem shims.
//
// Contract: every narrow (std::string / const char*) path inside femm-qt is
// UTF-8. On Windows a narrow fopen / std::fstream / std::filesystem::path
// interprets its bytes in the active (ANSI) code page, so a path containing a
// character outside it -- an umlaut in a German user or model folder, CJK, etc.
// -- silently fails to open. These shims route through the wide (_wfopen /
// wchar_t) API instead. On POSIX, UTF-8 is already the native encoding, so every
// shim is a straight pass-through and ASCII paths are byte-identical everywhere.
//
// This mirrors the self-contained fsPath() in tangle/tangle.cpp; it lives here
// (MIT, in femm_common) so libfemm, the GUI, and the GPL solvers can share one
// copy. tangle keeps its own so it still builds standalone.

#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

// Open a UTF-8 path. Drop-in for fopen(); returns nullptr on failure like fopen.
FILE *femm_fopen(const char *utf8Path, const char *mode);
inline FILE *femm_fopen(const std::string &utf8Path, const char *mode) {
    return femm_fopen(utf8Path.c_str(), mode);
}

// Reopen a stream on a UTF-8 path. Drop-in for freopen() (e.g. to switch a file
// to binary mode after sniffing it).
FILE *femm_freopen(const char *utf8Path, const char *mode, FILE *stream);

// Build a std::filesystem::path from a UTF-8 string. Use in place of the plain
// fs::path(std::string) ctor (which decodes ANSI on Windows) before fs::absolute
// et al., and for std::ifstream/ofstream ctors. Never throws.
std::filesystem::path femm_path(const std::string &utf8Path);

// UTF-8 std::string out of a std::filesystem::path. Use in place of
// path.string() (which yields ANSI on Windows) to keep the bytes UTF-8.
std::string femm_u8string(const std::filesystem::path &p);

#ifdef _WIN32
// Widen an arbitrary UTF-8 string to UTF-16 for a wide Win32 API (e.g. the
// command line for CreateProcessW). Windows-only; callers are already in
// _WIN32-guarded code. (For a wchar_t path coming BACK from Win32, wrap it in a
// std::filesystem::path and use femm_u8string().)
std::wstring femm_widen(const std::string &utf8);
#endif

// Decode a console program's arguments to UTF-8. On Windows the CRT hands a
// narrow main() an ANSI-mangled argv, so re-fetch the wide command line
// (GetCommandLineW) and transcode; on POSIX argv is already UTF-8 and is
// returned verbatim. Qt GUIs should use QCoreApplication::arguments() instead --
// Qt already decodes the wide command line itself.
std::vector<std::string> femm_utf8_args(int argc, char **argv);

#endif // FEMM_UTF8_IO_H
