// SPDX-License-Identifier: MIT
#include "utf8_io.h"

#ifdef _WIN32
#  include <cwchar>       // _wfopen
#  include <cerrno>       // errno, EACCES
#  include <windows.h>
#  include <shellapi.h>   // CommandLineToArgvW
#endif

// UTF-8 std::string -> filesystem::path. Uses the non-deprecated UTF-8 entry
// point for each C++ standard. Never throws: the UTF-8 -> native conversion
// raises filesystem_error on an invalid byte sequence (e.g. an already-ANSI
// path), which as an uncaught exception would take down the host process; on bad
// input, fall back to interpreting the bytes in the native encoding so a wrong
// path merely fails to open rather than terminating.
std::filesystem::path femm_path(const std::string &p) {
    try {
#if defined(__cpp_lib_char8_t)
        std::u8string u8(p.size(), u8'\0');              // C++20: char8_t path ctor
        for (std::size_t i = 0; i < p.size(); ++i)
            u8[i] = static_cast<char8_t>(p[i]);
        return std::filesystem::path(std::move(u8));
#else
        return std::filesystem::u8path(p);               // C++17: u8path (not deprecated here)
#endif
    } catch (const std::exception &) {
        return std::filesystem::path(p);                 // not valid UTF-8: native-encoding fallback
    }
}

std::string femm_u8string(const std::filesystem::path &p) {
    try {
        auto u8 = p.u8string();                          // std::string (C++17) / std::u8string (C++20)
        return std::string(u8.begin(), u8.end());
    } catch (const std::exception &) {
        return p.string();
    }
}

FILE *femm_fopen(const char *path, const char *mode) {
#ifdef _WIN32
    std::filesystem::path wp = femm_path(path);
    wchar_t wmode[8] = {};                               // modes are short ASCII ("rt","wb",...)
    for (int i = 0; i < 7 && mode[i]; ++i)
        wmode[i] = static_cast<wchar_t>(mode[i]);
    // A just-written file under OneDrive/Defender can be briefly locked while it
    // is synced/scanned, so _wfopen fails with errno==EACCES (Windows sharing
    // violation, unlike POSIX advisory locks). Retry a bounded number of times
    // with backoff. The success path never sleeps, and any other error (ENOENT,
    // a genuine permission denial, ...) returns at once -- errno is only read
    // once !fp is established, so a stale EACCES can't trigger a spurious retry.
    FILE *fp = _wfopen(wp.c_str(), wmode);
    for (int attempt = 0; !fp && errno == EACCES && attempt < 10; ++attempt) {
        Sleep(20 * (attempt + 1));                       // ~20..200ms steps, ~1.1s worst case
        fp = _wfopen(wp.c_str(), wmode);
    }
    return fp;
#else
    return std::fopen(path, mode);
#endif
}

FILE *femm_freopen(const char *path, const char *mode, FILE *stream) {
#ifdef _WIN32
    std::filesystem::path wp = femm_path(path);
    wchar_t wmode[8] = {};
    for (int i = 0; i < 7 && mode[i]; ++i)
        wmode[i] = static_cast<wchar_t>(mode[i]);
    return _wfreopen(wp.c_str(), wmode, stream);
#else
    return std::freopen(path, mode, stream);
#endif
}

#ifdef _WIN32
std::wstring femm_widen(const std::string &utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    return w;
}
#endif

std::vector<std::string> femm_utf8_args(int argc, char **argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int wargc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        out.reserve(wargc);
        for (int i = 0; i < wargc; ++i)
            out.push_back(femm_u8string(std::filesystem::path(wargv[i])));
        LocalFree(wargv);
        return out;
    }
    // wide fetch failed (unexpected): fall through to the narrow argv below.
#endif
    out.reserve(argc);
    for (int i = 0; i < argc; ++i)
        out.emplace_back(argv[i]);
    return out;
}
