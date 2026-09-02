// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// user_paths.h - the per-user directory femm-qt may write to and read from.
#pragma once

#include <cstdlib>
#include <string>

// Absolute path, with a trailing '/', to this user's femm-qt data directory --
// or "" when it cannot be determined, in which case the caller should simply
// skip whatever it was going to do there.
//
// The directory is NOT created here. Callers that only read (sys.path) want a
// path whether or not it exists; a caller that writes creates it when it
// writes. Nothing should be making directories during startup.
//
// The location on each platform is the one Qt already uses for this
// application's own data: gui/main.cpp sets both setApplicationName("FEMM") and
// setOrganizationName("FEMM"), so these sit beside the preferences Qt writes
// rather than inventing a second convention.
//
// Header-only on purpose. py_state.cpp is also compiled by
// micropython/build_test.sh, which links a curated list of objects rather than
// the CMake target, so a new .cpp here would have to be added to two build
// systems to avoid an link error in the one nobody runs by default.
//
// Qt is deliberately not used, for the same reason materialLibPath() avoids it:
// libfemm sits below the GUI and is linked into femm_headless, which has no
// QApplication to ask.
inline std::string userDataDir()
{
#if defined(_WIN32)
    // %APPDATA% comes back with backslashes; exeDir() and everything else in
    // the tree use forward slashes, and both work on Windows. Normalise so a
    // user reading sys.path does not see one path in two styles.
    auto fwd = [](std::string s) {
        for (char &c : s) if (c == '\\') c = '/';
        return s;
    };
    const char *appdata = std::getenv("APPDATA");
    if (appdata && *appdata) return fwd(appdata) + "/FEMM/";
    const char *profile = std::getenv("USERPROFILE");
    if (profile && *profile) return fwd(profile) + "/AppData/Roaming/FEMM/";
    return std::string();
#else
    const char *home = std::getenv("HOME");
#if defined(__APPLE__)
    if (home && *home)
        return std::string(home) + "/Library/Application Support/FEMM/";
    return std::string();
#else
    // XDG: $XDG_DATA_HOME, defaulting to ~/.local/share. The spec says to
    // ignore the variable when it is empty or not absolute.
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && *xdg == '/') return std::string(xdg) + "/FEMM/";
    if (home && *home) return std::string(home) + "/.local/share/FEMM/";
    return std::string();
#endif
#endif
}
