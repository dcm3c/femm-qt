// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// material_lib_path.cpp - see material_lib_path.h.
#include "material_lib_path.h"
#include "script_callbacks.h"   // exeDir()
#include "utf8_io.h"            // femm_path: UTF-8 -> filesystem::path

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

#ifdef __APPLE__
static bool fileExists(const std::string &utf8Path)
{
    std::error_code ec;
    return fs::is_regular_file(femm_path(utf8Path), ec);
}
#endif

std::string materialLibPath(const char *name)
{
#ifdef __APPLE__
    // Qt is deliberately not used here: libfemm sits below the GUI and is also
    // linked into femm_headless, which has no QApplication to ask. $HOME is the
    // same answer QStandardPaths would give for AppDataLocation.
    const char *home = std::getenv("HOME");
    if (!home || !*home) return exeDir() + name;      // degenerate; keep working

    // "FEMM" matches setApplicationName("FEMM") in gui/main.cpp, so the data
    // directory sits beside the preferences Qt writes under the same name.
    const std::string dir = std::string(home) + "/Library/Application Support/FEMM/";
    const std::string userPath = dir + name;
    if (fileExists(userPath)) return userPath;        // already seeded; the common case

    // First use of this library: seed from the read-only original.
    //
    // The seed is a CHAIN, and the order is what lets this land before
    // macos_build.sh changes. Contents/Resources/ is where the seeds belong and
    // where P1 will put them; Contents/MacOS/ (i.e. exeDir()) is where today's
    // build script still puts them. Whichever is found, the path RETURNED is
    // always the Application Support one, so there is only ever one writable
    // copy and the bundle is never written to.
    std::error_code ec;
    const std::string seeds[] = { exeDir() + "../Resources/" + name,
                                  exeDir() + name };
    for (const auto &seed : seeds) {
        if (!fileExists(seed)) continue;
        fs::create_directories(femm_path(dir), ec);
        if (ec) return exeDir() + name;               // cannot make it; fall back
        fs::copy_file(femm_path(seed), femm_path(userPath),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return exeDir() + name;
        // Bundle contents are read-only, and copy_file carries the permissions
        // across. The whole point is that this copy is editable.
        fs::permissions(femm_path(userPath), fs::perms::owner_write,
                        fs::perm_options::add, ec);
        return userPath;
    }

    // No seed anywhere. Return the user path regardless: the caller opens it and
    // reports "couldn't open", which names the place the file is meant to be
    // rather than a bundle-internal path the user cannot act on.
    return userPath;
#else
    return exeDir() + name;
#endif
}
