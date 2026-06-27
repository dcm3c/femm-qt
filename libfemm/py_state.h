// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_state.h - MicroPython interpreter lifecycle and the shared global
// namespace.
//
// The script console, the TCP server exec path and the line-edit evaluator
// all share the one global namespace created here (invariant #6 in the plan).
//
// NOTE (phasing): this Phase-2 version brings up the interpreter and the core
// numeric globals (I, pi/PI, Complex/Re/Im/conj/arg). The FEMM document API
// (newdocument, mi_*, mo_*, ...) is registered by the per-physics binding
// files in later phases; the frozen init script (complex-aware sqrt/sin/...) is
// added in Phase 6. Those will extend init_py(); nothing here is stubbed.
#ifndef PY_STATE_H
#define PY_STATE_H

#include <string>

// The wire-formatted return value(s) of the most recent exec_python() call
// (tab-separated for multiple/tuple results, empty for statements or a None
// result). The TCP server sends this on the wire. Valid until the next
// exec_python(). Complex values use I* notation (the IPC contract).
const std::string &py_last_result();

// Same return value(s), but formatted for human display: complex numbers in
// native-Python parenthesized "j" notation (e.g. "(1+2j)", real-valued
// collapsed to a bare real). The GUI console echoes this. Valid until the next
// exec_python().
const std::string &py_last_display();

// Write directly to the wire result buffer, bypassing the MicroPython GC heap.
// Used by bulk data retrieval functions (mo_getallelements etc.) whose result
// strings can exceed the GC heap capacity for large meshes.
void py_set_result(std::string &&result);

// Output sink for everything MicroPython writes to stdout: print(), exception
// tracebacks, sys.stdout.write, and REPL output. The GUI console sets this to
// route output into its window; when unset, output goes to the process stdout
// (headless/TCP mode). Set to nullptr to restore the default.
typedef void (*PyOutputSink)(const char *str, size_t len);
void set_py_output_sink(PyOutputSink sink);

// Periodic hook called from the VM inner loop while a script executes (every
// 1024 bytecode branches). The GUI registers a rate-limited Qt event pump so
// the UI stays responsive during long scripts and a Stop control stays
// clickable. Leave unset in headless mode. Set to nullptr to remove.
typedef void (*PyVmHook)();
void set_py_vm_hook(PyVmHook hook);

// Hook fired once after each *top-level* exec_python() / exec_python_file()
// returns — i.e. once per interactive line and once per script file, but NOT
// on event-pump re-entry (a nested exec_python while a script is already
// running). The GUI installs a throttled MDI repaint here so geometry edited
// by any script path — TCP line, console line, dofile, Run Script File —
// refreshes consistently, instead of only the TCP transport doing so. Leave
// unset in headless mode; set to nullptr to remove.
typedef void (*PyPostExecHook)();
void set_py_post_exec_hook(PyPostExecHook hook);

// Ask the running script to stop: KeyboardInterrupt is raised at the next VM
// branch (the standard MicroPython pending-exception mechanism). A request
// made while no script is running is dropped at the next exec_python().
void py_request_interrupt();

// True while exec_python() is on the stack. The GUI console uses this as a
// re-entrancy guard: event pumping means the user can type while a script
// runs.
bool py_is_running();

// Initialise the embedded MicroPython runtime and the shared global namespace.
// Must be called once before exec_python().
void init_py();

// Execute a chunk of Python source in the shared global namespace.
// Tries it as a single expression first (REPL-style: a non-None result is
// printed through the wire formatter); falls back to statement execution.
// Returns 0 on success. On a Python exception, returns nonzero and writes the
// formatted message into errmsg (truncated to errmsg_size).
int exec_python(const char *cmd, char *errmsg, int errmsg_size);

// Expression-only evaluation for property-dialog line edits.
// These try MP_PARSE_EVAL_INPUT only (no statement fallback), matching
// Python has expression-only eval (MP_PARSE_EVAL_INPUT), avoiding the side-effect of
// accidentally executing statements. Silent failure (returns false)
// matches sol::safe_script(pass_on_error).
bool py_eval_double(const char *expr, double *out);
bool py_eval_complex(const char *expr, double *re, double *im);
bool py_eval_string(const char *expr, std::string *out);

// Register global FEMM document functions (newdocument, open, quit, chdir,
// main_*) and the scripting-layer dispatch hooks for the doc registry.
// Defined in py_state_globals.cpp (needs per-physics document types); the
// test harness provides a no-op stub.
void register_global_femm_funcs();
void cleanup_global_femm_state();

// Tear down the runtime (symmetric to init_py()).
void close_py();

// Execute a Python script file. Returns 0 on success.
int exec_python_file(const char *path, char *errmsg = nullptr, int errmsg_size = 0);

#endif  // PY_STATE_H
