// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// material_lib_path.cpp - see material_lib_path.h.
#include "material_lib_path.h"
#include "femm_resource.h"     // femmResource()
#include "script_callbacks.h"   // exeDir()
#include "user_paths.h"         // userDataDir()
#include "utf8_io.h"            // femm_path / femm_fopen: UTF-8 aware

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static bool fileExists(const std::string &utf8Path)
{
    if (utf8Path.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(femm_path(utf8Path), ec);
}

// Where the read-only original lives. femmResource covers all three layouts:
// beside the executable, Contents/Resources/ in a macOS bundle (the seeds moved
// there when macos_build.sh changed, and both places are still tried), and
// ../share/femm on a split install.
std::string materialLibShippedPath(const char *name)
{
    return femmResource(name);
}

static std::string userPath(const char *name)
{
    const std::string dir = userDataDir();
    if (dir.empty()) return std::string();
    return dir + name;
}

// FNV-1a, 64-bit. Deliberately NOT a cryptographic hash: the question is "did
// this file change", not "did someone forge it", and pulling in a crypto
// dependency to answer it would be a poor trade. Collisions would mean failing
// to offer an update, not corrupting anything.
//
// Returns 0 for a file that cannot be read, which callers treat as "unknown"
// rather than as a value to compare.
static uint64_t hashFile(const std::string &utf8Path)
{
    if (utf8Path.empty()) return 0;
    FILE *fp = femm_fopen(utf8Path, "rb");
    if (!fp) return 0;
    uint64_t h = 1469598103934665603ULL;          // FNV offset basis
    unsigned char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;                // FNV prime
        }
    fclose(fp);
    return h ? h : 1;                             // never collide with "unknown"
}

static std::string seedPath(const char *name)
{
    const std::string u = userPath(name);
    return u.empty() ? u : u + ".seed";
}

static uint64_t readSeed(const char *name)
{
    const std::string p = seedPath(name);
    if (p.empty()) return 0;
    FILE *fp = femm_fopen(p, "rt");
    if (!fp) return 0;
    unsigned long long v = 0;
    const int got = fscanf(fp, "%llu", &v);
    fclose(fp);
    return got == 1 ? (uint64_t)v : 0;
}

static void writeSeed(const char *name, uint64_t h)
{
    const std::string p = seedPath(name);
    if (p.empty() || h == 0) return;
    FILE *fp = femm_fopen(p, "wt");
    if (!fp) return;
    fprintf(fp, "%llu\n", (unsigned long long)h);
    fclose(fp);
}

std::string materialLibPath(const char *name)
{
    const std::string u = userPath(name);
    if (fileExists(u)) return u;        // the user has their own; it wins
    return materialLibShippedPath(name);           // otherwise track the shipped library
}

std::string materialLibWritePath(const char *name)
{
    const std::string u = userPath(name);
    if (u.empty()) return exeDir() + name;   // no home directory; keep working

    std::error_code ec;
    fs::create_directories(femm_path(userDataDir()), ec);
    if (ec && !fs::is_directory(femm_path(userDataDir()), ec))
        return std::string();                // caller reports; do not write blind

    // The user's copy is about to be created: record what the shipped original
    // looks like NOW, because that is what this copy forks from. Everything
    // after is a comparison against that mark.
    //
    // Keyed on the copy being absent, NOT on the seed being absent. Deleting
    // matlib.dat is the documented way to go back to the shipped materials, and
    // it leaves the .seed behind; re-forking against that stale mark would
    // offer an "update" relative to a fork that no longer exists.
    if (!fileExists(u))
        writeSeed(name, hashFile(materialLibShippedPath(name)));
    return u;
}

bool materialLibUpdateAvailable(const char *name)
{
    if (!fileExists(userPath(name))) return false;   // reading shipped already

    const uint64_t shipped = hashFile(materialLibShippedPath(name));
    if (shipped == 0) return false;                  // no original to compare

    const uint64_t seed = readSeed(name);
    if (seed == 0) {
        // A user copy with no seed: either an install that predates this
        // mechanism, or the eager-copy macOS behaviour that came before it. We
        // cannot tell whether they edited anything or how old their copy is, so
        // adopt the current shipped library as the mark and say nothing.
        // Prompting "replace your materials?" on no evidence is worse than
        // staying quiet, and every later change is detected normally.
        writeSeed(name, shipped);
        return false;
    }
    return shipped != seed;
}

void materialLibMarkSeen(const char *name)
{
    writeSeed(name, hashFile(materialLibShippedPath(name)));
}
