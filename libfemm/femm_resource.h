// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// femm_resource.h - find a READ-ONLY asset that ships with the program.
#pragma once

#include "script_callbacks.h"   // exeDir()
#include "utf8_io.h"            // femm_path

#include <filesystem>
#include <string>
#include <system_error>

// Everything femm-qt ships and never writes -- the manual, python/, the
// PyBridge worker, the material-library originals -- is found relative to the
// running executable. That works as long as the program directory is ONE place,
// which is true for the Windows install, the Unix install and the macOS bundle,
// and false for a distro package that splits /usr/bin from /usr/share.
//
// So: a search, not a single path. `relative` is a path fragment such as
// "manual.pdf", "python" or "pybridge-worker/femmqt_pybridge_worker.py".
// Returns the first candidate that exists; when none do, returns the FIRST
// candidate, so an error message names the place the file is normally kept
// rather than the last place that was tried.
//
// Order matters, and exeDir() must stay first: that alone is what happens
// today, so every existing layout resolves exactly as it did and the new cases
// are purely additive.
inline std::string femmResource(const std::string &relative)
{
    const std::string first = exeDir() + relative;
    const std::string candidates[] = {
        first,
        // macOS bundle: a document in Contents/MacOS/ is sealed as nested code
        // by codesign, so shipped documents live in Contents/Resources/.
        exeDir() + "../Resources/" + relative,
        // FHS, found RELATIVELY: /usr/bin/femm_gui -> /usr/share/femm/...
        // No compiled-in prefix needed, and it serves /usr, /usr/local and
        // ~/.local alike, as long as bin/ and share/ share a parent.
        exeDir() + "../share/femm/" + relative,
#ifdef FEMM_INSTALL_PREFIX
        // Last resort for a layout where even the relative form fails. Nothing
        // defines this yet -- it becomes useful when the tree grows install()
        // rules, and is inert until then.
        std::string(FEMM_INSTALL_PREFIX) + "/share/femm/" + relative,
#endif
    };
    std::error_code ec;
    for (const auto &c : candidates)
        if (std::filesystem::exists(femm_path(c), ec)) return c;
    return first;
}
