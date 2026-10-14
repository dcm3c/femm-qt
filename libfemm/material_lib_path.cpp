// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// material_lib_path.cpp - see material_lib_path.h.
#include "material_lib_path.h"
#include "femm_resource.h"     // femmResource()
#include "script_callbacks.h"   // exeDir()
#include "user_paths.h"         // userDataDir()
#include "utf8_io.h"            // femm_path / femm_fopen: UTF-8 aware

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

// Whether the seed was written by an ANSWER to the update question (Yes or
// No) rather than by a fork or by the no-seed adoption. Only an answered seed
// may silence the content check below: "Choosing No ... stops this being
// asked again for this version" is a promise the dialog makes. The mark is a
// second word on the seed's line, so a reader of the old one-number format
// (readSeed's fscanf) still gets the hash.
static bool seedAnswered(const char *name)
{
    const std::string p = seedPath(name);
    if (p.empty()) return false;
    FILE *fp = femm_fopen(p, "rt");
    if (!fp) return false;
    unsigned long long v = 0;
    char word[32] = {0};
    const int got = fscanf(fp, "%llu %31s", &v, word);
    fclose(fp);
    return got == 2 && std::strcmp(word, "answered") == 0;
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

// ---- Content-level comparison of material libraries ------------------------
//
// Inserted into material_lib_path.cpp (see the patch). Answers the question the
// seed cannot: is the user's copy an UNEDITED library -- merely an older
// shipped one -- so that offering to replace it loses nothing?
//
// Why this exists. The first macOS builds copied every library into the user's
// directory eagerly, with no .seed, so materialLibUpdateAvailable() has no
// fork mark to compare against and, by design, stays silent: every Mac user
// from that era keeps the old electrostatics, heat-flow and current-flow
// libraries through every revision and is never asked. Worse, the no-seed
// branch then writes the CURRENT shipped hash as the mark, after which the
// copy looks "forked from this version" and stays silent until the shipped
// library changes again. Both observed on 2026-10-05.
//
// Why content, not bytes. The dialog re-saves the library on OK whether or not
// anything changed, so a copy that was byte-identical to an old shipped file
// stops being so the first time its dialog is opened. Comparing parsed blocks
// -- name, properties as numbers where they are numbers, data rows as numbers
// -- survives that, and survives the writer's number formatting.
//
// Two tests, either of which means "pristine":
//   1. subset  -- every block in the user copy exists in the shipped library
//                 with the same name and equal values. The common case: an
//                 older library whose materials were kept and added to.
//   2. history -- the canonical content hash of the user copy is one of the
//                 libraries shipped before (kKnownShippedLibraries below).
//                 Covers reorganisations that renamed or revised entries, where
//                 the subset test cannot tell a renamed stock material from a
//                 user's own. The table is maintained by hand; the test in
//                 testing/MaterialLibHistoryTest.py fails when the CURRENT
//                 shipped library's hash is missing from it, so a revision
//                 cannot ship without recording itself.
//
// Numeric equality is relative 1e-9 -- enough to absorb "%.17g" against the
// writer's output, far below anything a user would edit by hand.

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace {

struct LibBlock {
    std::string name;
    std::map<std::string, std::string> props;   // <Key> = value   (value text, trimmed)
    std::vector<std::string> rows;              // lines inside a block with no '=' (B-H points etc.)
};

std::string trimCopy(const std::string &s)
{
    const char *ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

// Folders are presentation; blocks are content. Everything else is ignored.
std::vector<LibBlock> parseLibrary(const std::string &utf8Path)
{
    std::vector<LibBlock> out;
    FILE *fp = femm_fopen(utf8Path, "rt");
    if (!fp) return out;
    char buf[4096];
    bool inBlock = false;
    LibBlock cur;
    while (fgets(buf, sizeof buf, fp)) {
        std::string line = trimCopy(buf);
        if (line.empty()) continue;
        if (line == "<BeginBlock>") { inBlock = true; cur = LibBlock(); continue; }
        if (line == "<EndBlock>") { if (inBlock) out.push_back(cur); inBlock = false; continue; }
        if (!inBlock) continue;
        const size_t eq = line.find('=');
        if (line[0] == '<' && eq != std::string::npos) {
            const size_t close = line.find('>');
            if (close == std::string::npos || close > eq) { cur.rows.push_back(line); continue; }
            std::string key = line.substr(1, close - 1);
            std::string val = trimCopy(line.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            if (key == "BlockName") cur.name = val;
            // A "<...Points> = 0" with no table is the writer spelling out an
            // absence the shipped files leave implicit (heatlib: 56 blocks
            // gain "<TKPoints> = 0" on the first save). Same meaning, so same
            // content: drop it.
            else if (key.size() > 6 && key.compare(key.size() - 6, 6, "Points") == 0 &&
                     trimCopy(val) == "0") { /* implicit */ }
            else cur.props[key] = val;
        } else {
            cur.rows.push_back(line);
        }
    }
    fclose(fp);
    return out;
}

bool parseDouble(const std::string &s, double &d)
{
    if (s.empty()) return false;
    char *end = nullptr;
    d = strtod(s.c_str(), &end);
    return end && *end == '\0';
}

bool sameValue(const std::string &a, const std::string &b)
{
    if (a == b) return true;
    double da, db;
    if (parseDouble(a, da) && parseDouble(b, db))
        return std::fabs(da - db) <= 1e-9 * std::max(1.0, std::fabs(db));
    return false;
}

// Rows compare token by token, numerically where both sides are numbers.
bool sameRow(const std::string &a, const std::string &b)
{
    if (a == b) return true;
    std::vector<std::string> ta, tb;
    for (const std::string *src : {&a, &b}) {
        std::vector<std::string> &dst = (src == &a) ? ta : tb;
        size_t i = 0;
        while (i < src->size()) {
            while (i < src->size() && isspace((unsigned char)(*src)[i])) i++;
            size_t j = i;
            while (j < src->size() && !isspace((unsigned char)(*src)[j])) j++;
            if (j > i) dst.push_back(src->substr(i, j - i));
            i = j;
        }
    }
    if (ta.size() != tb.size()) return false;
    for (size_t i = 0; i < ta.size(); i++)
        if (!sameValue(ta[i], tb[i])) return false;
    return true;
}

bool sameBlock(const LibBlock &a, const LibBlock &b)
{
    if (a.name != b.name || a.props.size() != b.props.size() || a.rows.size() != b.rows.size())
        return false;
    for (const auto &kv : a.props) {
        auto it = b.props.find(kv.first);
        if (it == b.props.end() || !sameValue(kv.second, it->second)) return false;
    }
    for (size_t i = 0; i < a.rows.size(); i++)
        if (!sameRow(a.rows[i], b.rows[i])) return false;
    return true;
}

// Canonical text: blocks sorted by name; properties sorted by key; every
// number rendered with "%.12g"; one token per line. Hashed with the same
// FNV-1a as hashFile(). tools/matlib_canon_hash.py produces the identical
// value from Python and is how kKnownShippedLibraries is extended.
std::string canonToken(const std::string &s)
{
    double d;
    if (parseDouble(s, d)) {
        char buf[64];
        snprintf(buf, sizeof buf, "%.12g", d);
        return buf;
    }
    return s;
}

uint64_t canonicalHash(std::vector<LibBlock> blocks)
{
    std::sort(blocks.begin(), blocks.end(),
              [](const LibBlock &x, const LibBlock &y) { return x.name < y.name; });
    uint64_t h = 1469598103934665603ULL;
    auto feed = [&h](const std::string &s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        h ^= '\n'; h *= 1099511628211ULL;
    };
    for (const LibBlock &b : blocks) {
        feed("B " + b.name);
        for (const auto &kv : b.props) feed(kv.first + "=" + canonToken(kv.second));
        for (const std::string &r : b.rows) {
            std::string canon;
            size_t i = 0;
            while (i < r.size()) {
                while (i < r.size() && isspace((unsigned char)r[i])) i++;
                size_t j = i;
                while (j < r.size() && !isspace((unsigned char)r[j])) j++;
                if (j > i) { if (!canon.empty()) canon += ' '; canon += canonToken(r.substr(i, j - i)); }
                i = j;
            }
            feed("R " + canon);
        }
    }
    return h ? h : 1;
}

// Canonical content hashes of every library ever shipped, all four files in
// one list (a hash identifies its contents; which file it came from does not
// matter). Append a line per revision; MaterialLibHistoryTest.py enforces
// that the current shipped libraries are present.
const uint64_t kKnownShippedLibraries[] = {
    // Recomputed from git on Windows 2026-10-06 with tools/matlib_canon_hash.py
    // (after its UTF-8 fix); every entry is a committed version of the file.
    // magnetics -- matlib.dat
    5655204628670885294ULL,    // 69068ac, 246 blocks: the original library, in every test build to 26Sep
    986457713768693601ULL,     // a5dedfc, 274 blocks: provenance rework (never in a release build)
    14689669006117839692ULL,   // fb2330d, 282 blocks: second review (never in a release build)
    17037349332438090264ULL,   // 280ab27, 282 blocks: current
    // electrostatics -- statlib.dat
    12032107445838854548ULL,   // 69068ac, 26 blocks
    1637964825958766937ULL,    // a5dedfc, 95 blocks: current
    // heat flow -- heatlib.dat
    16085430070600801723ULL,   // 69068ac, 136 blocks (verified from git; the Mac's recompute note is settled)
    7245644529617138792ULL,    // a5dedfc, 136 blocks: current
    // current flow -- condlib.dat
    15824074916941881472ULL,   // 69068ac, 2 blocks
    16540722511046625811ULL    // a5dedfc, 258 blocks: current
};

} // namespace

bool materialLibCopyIsPristine(const char *name)
{
    const std::string user = userPath(name);
    if (!fileExists(user)) return false;
    const std::vector<LibBlock> mine = parseLibrary(user);
    if (mine.empty()) return false;                     // unreadable or empty: say nothing
    const std::vector<LibBlock> shipped = parseLibrary(materialLibShippedPath(name));

    // 1. subset of the current shipped library
    bool subset = !shipped.empty();
    for (const LibBlock &b : mine) {
        if (!subset) break;
        bool found = false;
        for (const LibBlock &s : shipped)
            if (sameBlock(b, s)) { found = true; break; }
        subset = found;
    }
    if (subset) return true;

    // 2. one of the libraries shipped before
    const uint64_t h = canonicalHash(mine);
    for (uint64_t k : kKnownShippedLibraries)
        if (k == h) return true;
    return false;
}

bool materialLibCopyMatchesShipped(const char *name)
{
    const std::vector<LibBlock> mine = parseLibrary(userPath(name));
    const std::vector<LibBlock> shipped = parseLibrary(materialLibShippedPath(name));
    if (mine.empty() || mine.size() != shipped.size()) return false;
    return canonicalHash(mine) == canonicalHash(shipped);
}

uint64_t materialLibCanonicalHash(const char *utf8Path)
{
    return canonicalHash(parseLibrary(utf8Path));
}

bool materialLibUpdateAvailable(const char *name)
{
    if (!fileExists(userPath(name))) return false;   // reading shipped already

    const uint64_t shipped = hashFile(materialLibShippedPath(name));
    if (shipped == 0) return false;                  // no original to compare

    const uint64_t seed = readSeed(name);
    if (seed != 0 && shipped != seed) return true;   // forked from an older shipped library: offer
    // No seed (an install that predates the mechanism, or the eager-copy macOS
    // behaviour before it), or a seed equal to the shipped hash -- which is
    // also what the no-seed branch used to write, leaving the copy looking
    // current. The seed cannot decide; the CONTENT can. If the copy is an
    // unedited library that merely differs from the shipped one, offering the
    // replacement loses nothing, so offer. If it was edited, stay silent:
    // prompting on no evidence is worse than keeping quiet.
    if (seed == 0) writeSeed(name, shipped);
    else if (seedAnswered(name)) return false;   // the user already said Yes or No for THIS version
    if (materialLibCopyMatchesShipped(name)) return false;   // already current, re-saved or not
    return materialLibCopyIsPristine(name);
}

void materialLibMarkSeen(const char *name)
{
    const std::string p = seedPath(name);
    const uint64_t h = hashFile(materialLibShippedPath(name));
    if (p.empty() || h == 0) return;
    FILE *fp = femm_fopen(p, "wt");
    if (!fp) return;
    fprintf(fp, "%llu answered\n", (unsigned long long)h);
    fclose(fp);
}
