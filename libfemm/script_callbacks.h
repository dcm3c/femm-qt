// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// script_callbacks.h - scripting/GUI interface.
//
// Callbacks, document registry, shared mesh/analysis helpers, and utility
// functions used by the MicroPython scripting layer (py_*) and the GUI.
// Definitions are in script_callbacks.cpp (neutral TU, no scripting-engine
// dependency).
#ifndef SCRIPT_CALLBACKS_H
#define SCRIPT_CALLBACKS_H

#include <string>

class DocumentBase;
class ResultsDocBase;

// Document opened (newdocument/open) -> GUI creates an editor tab.
typedef void (*DocOpenedCallback)(DocumentBase *doc, const char *title);
void set_doc_opened_callback(DocOpenedCallback cb);
DocOpenedCallback get_doc_opened_callback();

// Results file opened (mi_loadsolution).
typedef void (*ResultsOpenedCallback)(const char *ansPath, void *doc);
void set_results_opened_callback(ResultsOpenedCallback cb);
ResultsOpenedCallback get_results_opened_callback();

// Mesh data handed to the GUI after createmesh (avoids .node/.ele round-trip).
struct MeshDisplayData {
    int numNodes;
    const double *nodeX;
    const double *nodeY;
    int numTris;
    const int *triNodes;   // [n0,n1,n2] per triangle (3*numTris)
    const int *triRegion;  // region attribute per triangle
};
typedef void (*MeshLoadedCallback)(const MeshDisplayData *data);
void set_mesh_loaded_callback(MeshLoadedCallback cb);
MeshLoadedCallback get_mesh_loaded_callback();

// Run an external solver (GUI uses QProcess). Returns process exit code.
typedef int (*RunSolverCallback)(const char *exePath, const char *basePath, int hide);
void set_run_solver_callback(RunSolverCallback cb);
RunSolverCallback get_run_solver_callback();

// GUI view commands (zoom, grid, save, window management).
// Returns a string result (gettitle etc.) or NULL.
typedef const char *(*ViewCommandCallback)(const char *cmd, int nargs, double *args);
void set_view_command_callback(ViewCommandCallback cb);
ViewCommandCallback get_view_command_callback();

// Script messagebox(): show a modal message dialog in the GUI. NULL in headless
// (the binding then falls back to stderr).
typedef void (*MessageBoxCallback)(const char *msg);
void set_message_box_callback(MessageBoxCallback cb);
MessageBoxCallback get_message_box_callback();

// Shared mesh creation for all physics types. 0 on success.
int createMeshForDocument(DocumentBase *doc, char *errmsg = nullptr, int errmsg_size = 0);

// Shared analysis for all physics types. 0 on success.
int analyzeDocument(DocumentBase *doc, int hide = 0, char *errmsg = nullptr, int errmsg_size = 0);

// Directory of the running executable, with trailing slash.
std::string exeDir();

// Document registry (shared GUI/headless tracking).
void set_gui_mode(bool gui);
void doc_registry_add(DocumentBase *doc);
void doc_registry_add(ResultsDocBase *doc);
void doc_registry_remove(DocumentBase *doc);
void doc_registry_remove(ResultsDocBase *doc);
DocumentBase *doc_registry_find_input(const std::string &path);
ResultsDocBase *doc_registry_find_results(const std::string &path);
bool doc_registry_contains(ResultsDocBase *doc);
bool doc_registry_activate_input(const std::string &path);
bool doc_registry_activate_results(const std::string &path);
// Match by base filename, case-insensitively (the GUI tab-bar label), for setfocus.
bool doc_registry_activate_input_byname(const std::string &name);
bool doc_registry_activate_results_byname(const std::string &name);

// Normalize a script-supplied setfocus name to carry this physics' canonical
// extension (".fem"/".ans"/etc.), so a bare "temp" and the GUI tab label
// "temp.fem" both resolve to the same document. See the definition for why we
// append (rather than strip) the extension.
std::string canonicalDocName(const std::string &name, const std::string &ext);

// Clean up the document registry (called by close_py / close_lua).
// In headless mode, deletes owned documents; always clears the vectors.
void doc_registry_cleanup();

// Scripting-layer dispatch hooks. The active scripting engine (Lua or Python)
// sets these so the neutral registry can activate/deactivate editor bindings
// without depending on either engine's headers.
typedef void (*ScriptDocActivateCallback)(DocumentBase *doc);
typedef void (*ScriptDocDeactivateCallback)(DocumentBase *doc);
typedef void (*ScriptResultsActivateCallback)(ResultsDocBase *doc);
typedef void (*ScriptResultsDeactivateCallback)(ResultsDocBase *doc);
void set_script_doc_activate_callback(ScriptDocActivateCallback cb);
void set_script_doc_deactivate_callback(ScriptDocDeactivateCallback cb);
void set_script_results_activate_callback(ScriptResultsActivateCallback cb);
void set_script_results_deactivate_callback(ScriptResultsDeactivateCallback cb);

// Normalize a file path: unescape C escape sequences that clients may embed
// (pyfemm sends raw backslash paths like "C:\foo\bar"), convert backslashes
// to forward slashes. Defined here so both the editor templates and the
// global open/setcurrentdirectory commands can use it without pulling in
// the full py_bind.h / sol3 headers.
inline std::string fixpath(const std::string &p) {
    std::string s;
    s.reserve(p.size());
    for (char c : p) {
        switch (c) {
            case '\a': s += "/a"; break;
            case '\b': s += "/b"; break;
            case '\f': s += "/f"; break;
            case '\n': s += "/n"; break;
            case '\r': s += "/r"; break;
            case '\t': s += "/t"; break;
            case '\v': s += "/v"; break;
            case '\\': s += '/'; break;
            default:   s += c; break;
        }
    }
    return s;
}

#endif  // SCRIPT_CALLBACKS_H
