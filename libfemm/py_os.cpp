// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_os.cpp - the `os` and `os.path` modules. See py_os.h for why these are
// native rather than MicroPython's own.
//
// Conventions, all three shared with every other file-taking FEMM command:
//   * paths arrive as UTF-8 and are widened at the OS boundary (femm_path),
//     results come back through femm_u8string -- never fs::path(std::string),
//     whose implicit conversion decodes as ANSI on Windows;
//   * fixpath() runs on every incoming path, so a script that says
//     "c:\temp\x.fem" reaches os.remove and mi_saveas as the same file;
//   * separators come back as '/', which is what fixpath leaves behind and what
//     the rest of the program stores.
//
// Failures raise OSError with a message, not RuntimeError: `except OSError` is
// what a script written against CPython or MicroPython will say. py_bind.h's
// os_error carries that distinction across the C++/MicroPython boundary.
#include "py_os.h"

#include "py_bind.h"
#include "script_callbacks.h"   // fixpath
#include "utf8_io.h"

extern "C" {
#include "py/objmodule.h"
#include "py/runtime.h"
}

#include <cerrno>
#include <cstdlib>          // std::getenv, used in the POSIX branch below
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#endif

namespace fs = std::filesystem;

namespace {

// Every path argument goes through this: unescape + backslash-to-slash (so the
// spelling matches the FEMM commands), then widen for the OS.
fs::path in(const std::string &utf8) { return femm_path(fixpath(utf8)); }

std::string out(const fs::path &p) {
    std::string s = femm_u8string(p);
    for (auto &c : s) if (c == '\\') c = '/';
    return s;
}

// std::filesystem reports failures through an error_code we then turn into the
// exception the script sees. Naming the path in the message matters more here
// than elsewhere: half the reason to call os.* is to find out which path was
// wrong.
[[noreturn]] void fail(const std::string &what, const std::string &path,
                       const std::error_code &ec) {
    // default_error_condition() maps the platform code to the generic (errno)
    // category where an equivalent exists -- on Windows the raw value is a
    // Win32 error, which is not what `e.errno == errno.ENOENT` compares
    // against. Where there is no equivalent it comes back 0, and os_error then
    // raises with the message alone.
    throw femm_py::os_error(what + " '" + path + "': " + ec.message(),
                            ec.default_error_condition().category() ==
                                std::generic_category()
                                ? ec.default_error_condition().value() : 0);
}

// MicroPython's os.stat returns a 10-tuple; so does this. Same order as
// CPython's stat_result, which is where that shape comes from:
//   mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime
// Sizes and times come from the platform stat so they mean what the platform
// means, rather than filesystem_error-prone conversions of file_time_type.
std::vector<long> stat_fields(const std::string &path) {
    const std::string fixed = fixpath(path);
#ifdef _WIN32
    struct _stat64 st;
    if (_wstat64(femm_widen(fixed).c_str(), &st) != 0)
#else
    struct stat st;
    if (::stat(fixed.c_str(), &st) != 0)
#endif
        throw femm_py::os_error("stat '" + fixed + "': no such file or directory", ENOENT);

    return { (long)st.st_mode, (long)st.st_ino, (long)st.st_dev,
             (long)st.st_nlink, (long)st.st_uid, (long)st.st_gid,
             (long)st.st_size, (long)st.st_atime, (long)st.st_mtime,
             (long)st.st_ctime };
}

// --- os.path string surgery -----------------------------------------------
// Pure string work, done here rather than in the init script so that os.path
// is a real module from the first line of a session and cannot be half-built
// if the script ever fails to run.

size_t last_sep(const std::string &p) {
    return p.find_last_of('/');
}

std::string join2(const std::string &a, const std::string &b) {
    if (b.empty()) return a;
    // An absolute second component wins, as in CPython.
    if (b[0] == '/' || (b.size() > 1 && b[1] == ':')) return b;
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

// The extension split point, or npos. A leading dot on the final component is
// part of the name (".femmrc" has no extension), matching CPython.
size_t ext_pos(const std::string &p) {
    size_t slash = last_sep(p);
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return std::string::npos;
    if (slash != std::string::npos && dot < slash) return std::string::npos;
    size_t nameStart = (slash == std::string::npos) ? 0 : slash + 1;
    if (dot == nameStart) return std::string::npos;
    return dot;
}

}  // namespace

void femm_os_chdir(const std::string &path) {
    const std::string fixed = fixpath(path);
    std::error_code ec;
    fs::current_path(femm_path(fixed), ec);
    if (ec) fail("chdir", fixed, ec);
}

void register_os_py() {
    using femm_py::Module;

    // mp_obj_new_module() registers into the loaded-modules dict, so `import os`
    // finds these with no import hook. The names carry a dot for the submodule
    // because that is what __name__ should read; MicroPython has no dotted
    // import, so `import os.path` will not work -- `import os` then os.path, or
    // `from os import path`, both do.
    mp_obj_t osmod   = mp_obj_new_module(qstr_from_str("os"));
    mp_obj_t pathmod = mp_obj_new_module(qstr_from_str("os.path"));

    Module os;   os.globals   = mp_obj_module_get_globals(osmod);
    Module path; path.globals = mp_obj_module_get_globals(pathmod);

    // --- os ----------------------------------------------------------------
    os.def("getcwd", []() {
        std::error_code ec;
        fs::path p = fs::current_path(ec);
        if (ec) fail("getcwd", "", ec);
        return out(p);
    });

    os.def("chdir", [](const std::string &p) { femm_os_chdir(p); });

    os.def("listdir", [](std::optional<std::string> p) {
        const std::string dir = fixpath(p.value_or("."));
        std::error_code ec;
        std::vector<std::string> names;
        for (fs::directory_iterator it(femm_path(dir), ec), end; it != end; it.increment(ec)) {
            if (ec) fail("listdir", dir, ec);
            names.push_back(out(it->path().filename()));
        }
        if (ec) fail("listdir", dir, ec);
        return names;
    });

    // os.getenv(name[, default]) -- read-only, which is the half that matters.
    // Its absence turned up twice from opposite directions: micropython-lib's
    // datetime conformance suite fails 25 tests without it, and `tempfile`
    // cannot find a temp directory (there is no /tmp on Windows). Widened on
    // Windows so a variable holding a non-ASCII path -- TEMP under an umlauted
    // user name is the everyday case -- comes back as the UTF-8 every other
    // femm command expects, rather than mangled through the ANSI CRT.
    os.def("getenv", [](const std::string &name,
                        std::optional<std::string> fallback)
                        -> std::optional<std::string> {
#ifdef _WIN32
        std::wstring wname = femm_widen(name);
        const wchar_t *w = _wgetenv(wname.c_str());
        // femm_u8string() is the wide->UTF-8 direction we already own; a
        // fs::path is just the vehicle for it.
        if (w) return femm_u8string(std::filesystem::path(w));
#else
        const char *v = std::getenv(name.c_str());
        if (v) return std::string(v);
#endif
        return fallback;
    });

    os.def("mkdir", [](const std::string &p) {
        const std::string dir = fixpath(p);
        std::error_code ec;
        // create_directory, not create_directories: os.mkdir makes ONE level and
        // fails if the parent is missing. os.makedirs below is the other one.
        if (!fs::create_directory(femm_path(dir), ec) || ec) {
            if (ec) fail("mkdir", dir, ec);
            throw femm_py::os_error("mkdir '" + dir + "': already exists", EEXIST);
        }
    });

    os.def("makedirs", [](const std::string &p) {
        const std::string dir = fixpath(p);
        std::error_code ec;
        fs::create_directories(femm_path(dir), ec);
        if (ec) fail("makedirs", dir, ec);
    });

    os.def("remove", [](const std::string &p) {
        const std::string f = fixpath(p);
        std::error_code ec;
        if (fs::is_directory(femm_path(f), ec))
            throw femm_py::os_error("remove '" + f + "': is a directory");
        if (!fs::remove(femm_path(f), ec) || ec) {
            if (ec) fail("remove", f, ec);
            throw femm_py::os_error("remove '" + f + "': no such file");
        }
    });

    os.def("rmdir", [](const std::string &p) {
        const std::string d = fixpath(p);
        std::error_code ec;
        if (!fs::is_directory(femm_path(d), ec))
            throw femm_py::os_error("rmdir '" + d + "': not a directory");
        // remove(), not remove_all(): rmdir refuses a non-empty directory, and
        // silently deleting a tree because a script passed the wrong name is
        // not a favour.
        if (!fs::remove(femm_path(d), ec) || ec) {
            if (ec) fail("rmdir", d, ec);
            throw femm_py::os_error("rmdir '" + d + "': directory not empty");
        }
    });

    os.def("rename", [](const std::string &src, const std::string &dst) {
        const std::string s = fixpath(src), d = fixpath(dst);
        std::error_code ec;
        fs::rename(femm_path(s), femm_path(d), ec);
        if (ec) fail("rename '" + s + "' ->", d, ec);
    });

    os.def("stat", [](const std::string &p) { return stat_fields(p); });

    os.alias("unlink", "remove");

    // '/' rather than the platform separator: fixpath turns every backslash into
    // one, so '/' is what a FEMM path actually contains on all three platforms.
    mp_obj_dict_store(MP_OBJ_FROM_PTR(os.globals),
                      MP_OBJ_NEW_QSTR(qstr_from_str("sep")), mp_obj_new_str("/", 1));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(os.globals),
                      MP_OBJ_NEW_QSTR(qstr_from_str("path")), pathmod);

    // --- os.path -----------------------------------------------------------
    path.def("exists", [](const std::string &p) {
        std::error_code ec;
        return fs::exists(in(p), ec) && !ec;
    });
    path.def("isdir", [](const std::string &p) {
        std::error_code ec;
        return fs::is_directory(in(p), ec) && !ec;
    });
    path.def("isfile", [](const std::string &p) {
        std::error_code ec;
        return fs::is_regular_file(in(p), ec) && !ec;
    });
    path.def("getsize", [](const std::string &p) {
        return (long)stat_fields(p)[6];
    });
    path.def("abspath", [](const std::string &p) {
        std::error_code ec;
        fs::path a = fs::absolute(in(p), ec);
        if (ec) fail("abspath", fixpath(p), ec);
        return out(a);
    });

    // The string half. Deliberately NOT fs::path::parent_path()/filename():
    // those consult the platform's separator rules, and a FEMM path is
    // '/'-separated everywhere by the time it gets here.
    path.def("join", [](const std::string &a, std::optional<std::string> b,
                        std::optional<std::string> c, std::optional<std::string> d) {
        std::string r = fixpath(a);
        if (b) r = join2(r, fixpath(*b));
        if (c) r = join2(r, fixpath(*c));
        if (d) r = join2(r, fixpath(*d));
        return r;
    });
    path.def("dirname", [](const std::string &p) {
        const std::string s = fixpath(p);
        size_t i = last_sep(s);
        return i == std::string::npos ? std::string() : s.substr(0, i);
    });
    path.def("basename", [](const std::string &p) {
        const std::string s = fixpath(p);
        size_t i = last_sep(s);
        return i == std::string::npos ? s : s.substr(i + 1);
    });
    path.def("split", [](const std::string &p) {
        const std::string s = fixpath(p);
        size_t i = last_sep(s);
        if (i == std::string::npos) return std::make_tuple(std::string(), s);
        return std::make_tuple(s.substr(0, i), s.substr(i + 1));
    });
    path.def("splitext", [](const std::string &p) {
        const std::string s = fixpath(p);
        size_t i = ext_pos(s);
        if (i == std::string::npos) return std::make_tuple(s, std::string());
        return std::make_tuple(s.substr(0, i), s.substr(i));
    });
    mp_obj_dict_store(MP_OBJ_FROM_PTR(path.globals),
                      MP_OBJ_NEW_QSTR(qstr_from_str("sep")), mp_obj_new_str("/", 1));
}
