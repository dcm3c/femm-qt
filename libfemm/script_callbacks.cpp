// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// script_callbacks.cpp - language-neutral scripting/GUI interface.
//
// Definitions for the callback storage, document registry, shared mesh
// creation, and analysis helpers declared in script_callbacks.h. These were
// originally in lua_state.cpp; they are scripting-engine-neutral and now live
// in their own TU so both the Lua and MicroPython backends can link against
// them without cross-dependency.
#include "script_callbacks.h"
#include "utf8_io.h"
#include "document.h"
#include "results_doc_base.h"
#include "tangle_mesh.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Callback storage
// ---------------------------------------------------------------------------
static DocOpenedCallback       s_docOpenedCb    = nullptr;
static ResultsOpenedCallback   s_resultsOpenedCb = nullptr;
static RunSolverCallback       s_runSolverCb    = nullptr;
static ExitHook                s_exitHook       = nullptr;
static MeshLoadedCallback      s_meshLoadedCb   = nullptr;
static ViewCommandCallback     s_viewCmdCb      = nullptr;

void set_doc_opened_callback(DocOpenedCallback cb)       { s_docOpenedCb = cb; }
DocOpenedCallback get_doc_opened_callback()              { return s_docOpenedCb; }
void set_results_opened_callback(ResultsOpenedCallback cb) { s_resultsOpenedCb = cb; }
ResultsOpenedCallback get_results_opened_callback()      { return s_resultsOpenedCb; }
void set_run_solver_callback(RunSolverCallback cb)       { s_runSolverCb = cb; }
RunSolverCallback get_run_solver_callback()              { return s_runSolverCb; }
void set_exit_hook(ExitHook cb)                          { s_exitHook = cb; }
ExitHook get_exit_hook()                                 { return s_exitHook; }
void set_mesh_loaded_callback(MeshLoadedCallback cb)     { s_meshLoadedCb = cb; }
MeshLoadedCallback get_mesh_loaded_callback()            { return s_meshLoadedCb; }
void set_view_command_callback(ViewCommandCallback cb)   { s_viewCmdCb = cb; }
ViewCommandCallback get_view_command_callback()          { return s_viewCmdCb; }

static MessageBoxCallback      s_messageBoxCb   = nullptr;
void set_message_box_callback(MessageBoxCallback cb)     { s_messageBoxCb = cb; }
MessageBoxCallback get_message_box_callback()            { return s_messageBoxCb; }

// Scripting-layer dispatch hooks for activate/deactivate
static ScriptDocActivateCallback   s_activateCb   = nullptr;
static ScriptDocDeactivateCallback s_deactivateCb  = nullptr;

static ScriptResultsActivateCallback s_resultsActivateCb = nullptr;
static ScriptResultsDeactivateCallback s_resultsDeactivateCb = nullptr;

void set_script_doc_activate_callback(ScriptDocActivateCallback cb)   { s_activateCb = cb; }
void set_script_doc_deactivate_callback(ScriptDocDeactivateCallback cb) { s_deactivateCb = cb; }
void set_script_results_activate_callback(ScriptResultsActivateCallback cb) { s_resultsActivateCb = cb; }
void set_script_results_deactivate_callback(ScriptResultsDeactivateCallback cb) { s_resultsDeactivateCb = cb; }

// ---------------------------------------------------------------------------
// Document registry
// ---------------------------------------------------------------------------
static std::vector<DocumentBase*>    s_inputDocs;
static std::vector<ResultsDocBase*>  s_resultsDocs;
static bool s_guiMode = false;

void set_gui_mode(bool gui) { s_guiMode = gui; }

void doc_registry_add(DocumentBase *doc) {
    if (!doc) return;
    for (auto *d : s_inputDocs) if (d == doc) return;
    s_inputDocs.push_back(doc);
}
void doc_registry_add(ResultsDocBase *doc) {
    if (!doc) return;
    for (auto *d : s_resultsDocs) if (d == doc) return;
    s_resultsDocs.push_back(doc);
}

void doc_registry_remove(DocumentBase *doc) {
    if (!doc) return;
    s_inputDocs.erase(std::remove(s_inputDocs.begin(), s_inputDocs.end(), doc),
                      s_inputDocs.end());
    if (s_deactivateCb) s_deactivateCb(doc);
}
void doc_registry_remove(ResultsDocBase *doc) {
    if (!doc) return;
    s_resultsDocs.erase(std::remove(s_resultsDocs.begin(), s_resultsDocs.end(), doc),
                        s_resultsDocs.end());
    // Symmetric with the input-doc path: clear any postproc binding still
    // pointing at this results doc, so a mo_/eo_/ho_/co_ command issued after
    // its window is closed (which deletes the doc) can't use freed memory.
    if (s_resultsDeactivateCb) s_resultsDeactivateCb(doc);
}

DocumentBase *doc_registry_find_input(const std::string &path) {
    if (path.empty()) return nullptr;
    for (auto *d : s_inputDocs)
        if (d->filePath == path) return d;
    return nullptr;
}
ResultsDocBase *doc_registry_find_results(const std::string &path) {
    if (path.empty()) return nullptr;
    for (auto *d : s_resultsDocs)
        if (d->filePath == path) return d;
    return nullptr;
}

bool doc_registry_contains(ResultsDocBase *doc) {
    if (!doc) return false;
    for (auto *d : s_resultsDocs)
        if (d == doc) return true;
    return false;
}

bool doc_registry_activate_input(const std::string &path) {
    DocumentBase *doc = doc_registry_find_input(path);
    if (!doc) return false;
    if (s_activateCb) s_activateCb(doc);
    return true;
}

bool doc_registry_activate_results(const std::string &path) {
    ResultsDocBase *doc = doc_registry_find_results(path);
    if (!doc) return false;
    if (s_resultsActivateCb) s_resultsActivateCb(doc);
    return true;
}

// Case-insensitive base-filename (the GUI tab-bar label). setfocus("Antunes.fem")
// or "antunes.fem" should match a doc opened as "c:/temp/Antunes.fem". (open()'s
// de-dup still uses exact full-path matching so same-named files stay distinct.)
static std::string baseLower(const std::string &path) {
    auto slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    for (auto &c : base) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    return base;
}

// Normalize a script-supplied setfocus name so a bare "temp" and the tab label
// "temp.fem" both resolve to the same document. OF's *_setfocus treats the
// extension as optional -- lua_switchfocus strips the physics extension from
// both the requested name and each document title before comparing. We instead
// APPEND the canonical extension when it is missing, which yields the same
// matches AND scopes the result to this physics: the input docs of all four
// physics share one registry, so mi_setfocus("temp") normalized to "temp.fem"
// cannot accidentally activate a same-named "temp.fee"/"temp.feh"/"temp.fec".
// A name that already ends in `ext` is returned unchanged (dir stripped,
// lowercased). "my.motor" (no canonical ext) -> "my.motor.fem", matching OF.
std::string canonicalDocName(const std::string &name, const std::string &ext) {
    std::string base = baseLower(name);
    std::string e = ext;
    for (auto &c : e) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    if (base.size() >= e.size() &&
        base.compare(base.size() - e.size(), e.size(), e) == 0)
        return base;
    return base + e;
}

bool doc_registry_activate_input_byname(const std::string &name) {
    std::string want = baseLower(name);
    for (auto *d : s_inputDocs)
        if (baseLower(d->filePath) == want) {
            if (s_activateCb) s_activateCb(d);
            return true;
        }
    return false;
}

bool doc_registry_activate_results_byname(const std::string &name) {
    std::string want = baseLower(name);
    for (auto *d : s_resultsDocs)
        if (baseLower(d->filePath) == want) {
            if (s_resultsActivateCb) s_resultsActivateCb(d);
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------
// Shared mesh creation
// ---------------------------------------------------------------------------

// Meshing and analysis both write the document out first, because the mesher
// and the solvers are separate processes that read it back from disk -- an edit
// still in memory would otherwise be left out of the run.
//
// The write can fail, and until 2026-08-22 both callers ignored that: the run
// went ahead against whatever happened to be on disk, which is the PREVIOUS
// version of the model, and the results looked perfectly ordinary. A read-only
// file, a full disk, a folder without write permission, or the file held open
// by another program all do it. On macOS it is also what happens to an example
// opened from inside the application bundle, which is read-only by design.
static bool saveBeforeRun(DocumentBase *doc, const char *what,
                          char *errmsg, int errmsg_size)
{
    if (doc->SaveDocument(doc->filePath)) return true;
    if (errmsg)
        snprintf(errmsg, errmsg_size,
                 "Could not save \"%s\" before %s -- the file may be read-only, "
                 "the disk full, or the folder not writable. Nothing was run: "
                 "the solver reads the saved file, so continuing would have "
                 "used the previous version of the model.",
                 doc->filePath.c_str(), what);
    return false;
}

int createMeshForDocument(DocumentBase *doc, char *errmsg, int errmsg_size)
{
    if (!doc || doc->filePath.empty()) {
        if (errmsg) snprintf(errmsg, errmsg_size, "File must be saved before creating mesh");
        return -1;
    }

    if (!saveBeforeRun(doc, "creating the mesh", errmsg, errmsg_size)) return -1;

    namespace fs = std::filesystem;
    // Pass the FULL filename (with its extension) so the mesher loads THIS
    // document's geometry. Stripping the extension made tangle_mesh_fem
    // re-guess and pick .fem first -- silently meshing a same-named .fem into a
    // .fee/.feh/.fec problem.
    fs::path p = fs::absolute(femm_path(doc->filePath));
    std::string full = femm_u8string(p);
    std::replace(full.begin(), full.end(), '\\', '/');

    Mesh tmesh;
    int ret = tangle_mesh_fem(full, tmesh);
    if (ret != 0) {
        if (errmsg) snprintf(errmsg, errmsg_size, "Mesher failed (error %d)", ret);
        return ret;
    }

    auto meshCb = get_mesh_loaded_callback();
    if (meshCb) {
        int nv = (int)tmesh.vertices.size();
        int nt = (int)tmesh.triangles.size();
        std::vector<double> nx(nv), ny(nv);
        std::vector<int> tn(nt * 3), tr(nt);
        for (int i = 0; i < nv; i++) {
            nx[i] = tmesh.vertices[i].x;
            ny[i] = tmesh.vertices[i].y;
        }
        // A region whose block label has no material ("<None>") should display as
        // undefined (grey), like a region with no label at all. tangle numbers
        // regions by block-label position over the file's block-label list, which
        // omits "<No Mesh>" holes -- rebuild that order and pass 0 for a <None>
        // region so the GUI's "region != 0" test greys it; material regions keep
        // their positive attribute.
        std::vector<bool> labelDefined;
        for (const auto &b : doc->blocklist)
            if (b.BlockType != "<No Mesh>")
                labelDefined.push_back(b.BlockType != "<None>" && !b.BlockType.empty());
        for (int i = 0; i < nt; i++) {
            tn[i*3+0] = tmesh.triangles[i].v[0];
            tn[i*3+1] = tmesh.triangles[i].v[1];
            tn[i*3+2] = tmesh.triangles[i].v[2];
            int ra = (int)tmesh.triangles[i].region_attrib;
            if (ra > 0 && ra <= (int)labelDefined.size() && !labelDefined[ra-1]) ra = 0;
            tr[i] = ra;
        }
        MeshDisplayData mdd = { nv, nx.data(), ny.data(), nt, tn.data(), tr.data() };
        meshCb(&mdd);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Shared analysis
// ---------------------------------------------------------------------------
std::string exeDir()
{
    static std::string dir;
    if (!dir.empty()) return dir;
#ifdef _WIN32
    wchar_t wbuf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, wbuf, MAX_PATH);
    if (n > 0) {
        dir = femm_u8string(std::filesystem::path(wbuf));
        std::replace(dir.begin(), dir.end(), '\\', '/');
        auto slash = dir.rfind('/');
        if (slash != std::string::npos)
            dir = dir.substr(0, slash + 1);
    }
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char real[4096];
        if (realpath(buf, real))
            dir = real;
        else
            dir = buf;
        auto slash = dir.rfind('/');
        if (slash != std::string::npos)
            dir = dir.substr(0, slash + 1);
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        dir = buf;
        auto slash = dir.rfind('/');
        if (slash != std::string::npos)
            dir = dir.substr(0, slash + 1);
    }
#endif
    return dir;
}

// Human-readable text for the exit codes the solvers (fkn/belasolv/hsolv/csolv)
// return. All four use the same codes for the same failures -- 1, 2, 4, 5, 6, 7
// and 9 through 13 appear in every one of their main.cpp files -- so one table
// serves all four physics. Two are narrower than the rest: 3 (previous solution)
// is only reachable from hsolv, since fkn reports a failed mesh reuse as 2, and
// 8 is no longer returned by anything (meshing failures became 13 when meshing
// moved in-process). Both are kept so an older solver binary still reads
// correctly. Returns nullptr for unknown codes so the caller falls back to the
// bare number.
static const char *solverErrorText(int code)
{
    switch (code) {
    case 1: return "invalid solver arguments";
    case 2: return "meshing or mesh loading failed";
    case 3: return "could not load the previous (bias) solution";
    case 4: return "out of memory (matrix allocation failed)";
    case 5: return "the solve failed -- out of memory, non-convergence, or an "
                   "ill-posed problem";
    case 6: return "failed to write the results file";
    case 7: return "could not load the problem file";
    case 8: return "meshing failed";
    case 9: return "axisymmetric geometry extends to r < 0 -- move it onto or "
                   "to the right of the r = 0 axis";
    case 10: return "a region has no material assigned -- run the mesh generator "
                    "to highlight the regions without a block label";
    case 11: return "the solver could not find the problem file";
    case 12: return "the problem file could not be parsed";
    case 13: return "meshing failed";
    default: return nullptr;
    }
}

int analyzeDocument(DocumentBase *doc, int hide, char *errmsg, int errmsg_size)
{
    if (!doc || doc->filePath.empty()) {
        if (errmsg) snprintf(errmsg, errmsg_size, "File must be saved before analysis");
        return -1;
    }

    // Reject a region whose block label has no material ("<None>") before we mesh.
    // It is written to the solver file as material index 0, which the mesher reads
    // as a hole -- silently voiding the region (and, if it's the only one, an empty
    // mesh that crashes downstream). Catch it here while the literal "<None>" is
    // still known (the saved file only keeps the flattened index), matching OF. A
    // default block label does NOT cover this: the default applies only to regions
    // with no label at all, not to a label left without a material. (The standalone
    // solver CLI, which bypasses this, guards the same case in each solver's main.)
    // "No material" is spelled "<None>" now, whether the label was just placed
    // or loaded with material index 0. The empty string means the same thing
    // and is still accepted here, as it is at the two display sites and in the
    // mesh shading above: it is what a default-constructed label carried before
    // FBlockLabel named the state, so it can still arrive from a caller built
    // against the old header.
    {
        bool hasNone = false;
        for (const auto &b : doc->blocklist)
            if (b.BlockType == "<None>" || b.BlockType.empty()) { hasNone = true; break; }
        if (hasNone) {
            if (errmsg) snprintf(errmsg, errmsg_size,
                "A block label has no material assigned. Assign a material to "
                "every region before analyzing.");
            return -1;
        }
    }

    // Axisymmetric problems live in r >= 0; geometry drawn to the left of the
    // r = 0 axis is invalid (it yields negative volumes/energies/forces, since
    // every element carries a 2*pi*r weight) yet otherwise meshes and "solves"
    // silently, so reject it up front. getBoundingBox samples arc segments at
    // discretization level, so an arc that bulges below the axis between r >= 0
    // endpoints is caught too. Tolerance scales with the model size so geometry
    // on the axis (r ~ 0 with FP noise) doesn't trip it.
    if (doc->ProblemType == 1) {
        double xmin, ymin, xmax, ymax;
        if (doc->getBoundingBox(xmin, ymin, xmax, ymax)) {
            double xscale = std::max(std::fabs(xmin), std::fabs(xmax));
            if (xmin < -1e-6 * (xscale > 0 ? xscale : 1.0)) {
                if (errmsg) snprintf(errmsg, errmsg_size,
                    "Axisymmetric problems require r >= 0, but the geometry extends "
                    "to r = %g. Move it onto or to the right of the r = 0 axis.", xmin);
                return -1;
            }
        }
    }

    if (!saveBeforeRun(doc, "analysis", errmsg, errmsg_size)) return -1;
    int ret;

    namespace fs = std::filesystem;
    fs::path p = fs::absolute(femm_path(doc->filePath));
    p.replace_extension();
    std::string base = femm_u8string(p);
    std::replace(base.begin(), base.end(), '\\', '/');

    std::string solver = exeDir() + doc->solverName();
#ifdef _WIN32
    solver += ".exe";
#endif

    auto cb = get_run_solver_callback();
    if (cb) {
        ret = cb(solver.c_str(), base.c_str(), hide);
    } else {
#ifdef _WIN32
        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::string cmdline = "\"" + solver + "\" \"" + base + "\"";
        std::wstring wcmd = femm_widen(cmdline);
        // DETACHED_PROCESS, not CREATE_NO_WINDOW: the latter still lets
        // the child inherit the parent's console attachment, so when
        // femm_headless is launched from a terminal every solver stdout
        // write hits conhost and adds visible per-iteration latency.
        BOOL ok = CreateProcessW(
            nullptr, &wcmd[0], nullptr, nullptr,
            FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
        if (!ok) {
            ret = -1;
        } else {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            ret = (int)exitCode;
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
#else
        std::string cmd = "\"" + solver + "\" \"" + base + "\" > /dev/null 2>&1";
        ret = system(cmd.c_str());
#endif
    }
    if (ret != 0 && errmsg) {
        const char *t = solverErrorText(ret);
        if (t) snprintf(errmsg, errmsg_size, "Solver failed: %s (exit code %d)", t, ret);
        else   snprintf(errmsg, errmsg_size, "Solver returned error code %d", ret);
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Registry cleanup (called by close_py / close_lua)
// ---------------------------------------------------------------------------
void doc_registry_cleanup()
{
    if (!s_guiMode) {
        for (auto *doc : s_inputDocs) delete doc;
        for (auto *doc : s_resultsDocs) delete doc;
    }
    s_inputDocs.clear();
    s_resultsDocs.clear();
}
