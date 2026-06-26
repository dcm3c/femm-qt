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
static MeshLoadedCallback      s_meshLoadedCb   = nullptr;
static ViewCommandCallback     s_viewCmdCb      = nullptr;

void set_doc_opened_callback(DocOpenedCallback cb)       { s_docOpenedCb = cb; }
DocOpenedCallback get_doc_opened_callback()              { return s_docOpenedCb; }
void set_results_opened_callback(ResultsOpenedCallback cb) { s_resultsOpenedCb = cb; }
ResultsOpenedCallback get_results_opened_callback()      { return s_resultsOpenedCb; }
void set_run_solver_callback(RunSolverCallback cb)       { s_runSolverCb = cb; }
RunSolverCallback get_run_solver_callback()              { return s_runSolverCb; }
void set_mesh_loaded_callback(MeshLoadedCallback cb)     { s_meshLoadedCb = cb; }
MeshLoadedCallback get_mesh_loaded_callback()            { return s_meshLoadedCb; }
void set_view_command_callback(ViewCommandCallback cb)   { s_viewCmdCb = cb; }
ViewCommandCallback get_view_command_callback()          { return s_viewCmdCb; }

// Scripting-layer dispatch hooks for activate/deactivate
static ScriptDocActivateCallback   s_activateCb   = nullptr;
static ScriptDocDeactivateCallback s_deactivateCb  = nullptr;

static ScriptResultsActivateCallback s_resultsActivateCb = nullptr;

void set_script_doc_activate_callback(ScriptDocActivateCallback cb)   { s_activateCb = cb; }
void set_script_doc_deactivate_callback(ScriptDocDeactivateCallback cb) { s_deactivateCb = cb; }
void set_script_results_activate_callback(ScriptResultsActivateCallback cb) { s_resultsActivateCb = cb; }

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
int createMeshForDocument(DocumentBase *doc, char *errmsg, int errmsg_size)
{
    if (!doc || doc->filePath.empty()) {
        if (errmsg) snprintf(errmsg, errmsg_size, "File must be saved before creating mesh");
        return -1;
    }

    doc->SaveDocument(doc->filePath);

    namespace fs = std::filesystem;
    // Pass the FULL filename (with its extension) so the mesher loads THIS
    // document's geometry. Stripping the extension made tangle_mesh_fem
    // re-guess and pick .fem first -- silently meshing a same-named .fem into a
    // .fee/.feh/.fec problem.
    fs::path p = fs::absolute(doc->filePath);
    std::string full = p.string();
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
        for (int i = 0; i < nt; i++) {
            tn[i*3+0] = tmesh.triangles[i].v[0];
            tn[i*3+1] = tmesh.triangles[i].v[1];
            tn[i*3+2] = tmesh.triangles[i].v[2];
            tr[i] = (int)tmesh.triangles[i].region_attrib;
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
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n > 0) {
        dir = buf;
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

int analyzeDocument(DocumentBase *doc, int hide, char *errmsg, int errmsg_size)
{
    if (!doc || doc->filePath.empty()) {
        if (errmsg) snprintf(errmsg, errmsg_size, "File must be saved before analysis");
        return -1;
    }

    doc->SaveDocument(doc->filePath);
    int ret;

    namespace fs = std::filesystem;
    fs::path p = fs::absolute(doc->filePath);
    p.replace_extension();
    std::string base = p.string();
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
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::string cmdline = "\"" + solver + "\" \"" + base + "\"";
        // DETACHED_PROCESS, not CREATE_NO_WINDOW: the latter still lets
        // the child inherit the parent's console attachment, so when
        // femm_headless is launched from a terminal every solver stdout
        // write hits conhost and adds visible per-iteration latency.
        BOOL ok = CreateProcessA(
            nullptr, &cmdline[0], nullptr, nullptr,
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
    if (ret != 0 && errmsg)
        snprintf(errmsg, errmsg_size, "Solver returned error code %d", ret);
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
