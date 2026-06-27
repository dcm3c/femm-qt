// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_state.cpp - embedded MicroPython lifecycle + shared global namespace.
// See py_state.h for interface notes.
#include "py_state.h"
#include "py_bind.h"
#include "py_clinalg.h"
#include "py_init_script.h"
#include "script_callbacks.h"

#include <cstdio>

extern "C" {
#include "py/compile.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mpprint.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
}

#include "femm_stack_guard.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>

// Initial GC heap segment. MICROPY_GC_SPLIT_HEAP_AUTO grows the heap on
// demand by malloc'ing further segments (capped per split by
// gc_get_max_new_split() in femm_port_hooks.c) and returns empty segments
// during sweep, so this only needs to cover a steady-state console session.
static char s_heap[2 * 1024 * 1024];

// ---------------------------------------------------------------------------
// Stdout routing. We provide mp_hal_stdout_tx_strn[_cooked] ourselves (the
// embed port's port/mphalport.c is excluded from the build) so that print(),
// tracebacks and sys.stdout can be redirected to the GUI console. When no sink
// is set, output goes to the process stdout (headless/TCP mode).
// ---------------------------------------------------------------------------
static PyOutputSink s_output_sink = nullptr;

void set_py_output_sink(PyOutputSink sink) { s_output_sink = sink; }

// ---------------------------------------------------------------------------
// Script interruption + VM hook (GUI event pumping).
// femm_vm_hook_fn (femm_port_hooks.c) is called from the VM inner loop every
// 1024 branches; the GUI points it at a Qt event pump so the UI stays live
// and a Stop control works. py_request_interrupt() raises KeyboardInterrupt
// at the next VM branch via the pending-exception mechanism.
// ---------------------------------------------------------------------------
static int s_running_depth = 0;  // >0 while exec_python is on the stack

void set_py_vm_hook(PyVmHook hook) { femm_vm_hook_fn = hook; }

// Fired once when the outermost exec_python() unwinds (s_running_depth -> 0).
// See py_state.h. Headless/TCP leave this null and pay nothing.
static PyPostExecHook s_post_exec_hook = nullptr;

void set_py_post_exec_hook(PyPostExecHook hook) { s_post_exec_hook = hook; }

bool py_is_running() { return s_running_depth > 0; }

void py_request_interrupt() { mp_sched_keyboard_interrupt(); }

extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    if (s_output_sink) s_output_sink(str, len);
    else std::fwrite(str, 1, len, stdout);
}
extern "C" mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    if (s_output_sink) s_output_sink(str, len);
    else std::fwrite(str, 1, len, stdout);
    return len;
}

// ---------------------------------------------------------------------------
// Wire-compatible result formatting.
//
// Wire-format result capture. exec_python formats the return value into
// s_last_result. Byte-for-byte formatting rules (%.17g precision):
//   complex -> CComplex::ToString format (%.17g, with im==0/+-1 / re==0 cases)
//   float   -> "%.17g"
//   bool    -> "true"/"false"  (wire compat, NOT Python "True"/"False")
//   int     -> integer digits        (MicroPython repr; handles big ints)
//   str     -> the string, unquoted
//   tuple   -> elements tab-separated
// print() output and tracebacks are NOT part of this (they go to stdout, like
// the output sink); only return values reach the wire.
// ---------------------------------------------------------------------------
static std::string s_last_result;   // wire format (I* notation) — for IPC
static std::string s_last_display;  // human display (j notation) — for the console
static std::string s_accum;

const std::string &py_last_result() { return s_last_result; }
const std::string &py_last_display() { return s_last_display; }
void py_set_result(std::string &&result) { s_last_result = std::move(result); }

// Replicates CComplex::ToString(s, n) exactly.
static void format_complex(std::string &out, double re, double im) {
    char b[96];
    if (im == 0)
        snprintf(b, sizeof b, "%.17g", re);
    else if (im == 1)
        re == 0 ? snprintf(b, sizeof b, "I") : snprintf(b, sizeof b, "%.17g+I", re);
    else if (im == -1)
        re == 0 ? snprintf(b, sizeof b, "-I") : snprintf(b, sizeof b, "%.17g-I", re);
    else if (im < 0)
        re != 0 ? snprintf(b, sizeof b, "%.17g-I*%.17g", re, fabs(im))
                : snprintf(b, sizeof b, "-I*%.17g", fabs(im));
    else  // im > 0
        re != 0 ? snprintf(b, sizeof b, "%.17g+I*%.17g", re, im)
                : snprintf(b, sizeof b, "I*%.17g", im);
    out += b;
}

// Human-facing complex rendering for the console and line-edit: Python "j"
// notation (e.g. "1.5+2j", no parentheses), with a real-valued complex (im==0)
// collapsed to a bare real so genuinely-real results read cleanly. Full %.17g
// precision (shows the exact value). The TCP wire uses format_complex() above
// (I* notation) instead — that format is the IPC contract and must not change.
static void format_complex_display(std::string &out, double re, double im) {
    char b[96];
    if (im == 0)        // collapse: a real-valued result reads as a real
        snprintf(b, sizeof b, "%.17g", re);
    else if (re == 0)   // pure imaginary: "0.5j", "-3j", "1j"
        snprintf(b, sizeof b, "%.17gj", im);
    else if (im < 0)
        snprintf(b, sizeof b, "%.17g-%.17gj", re, -im);
    else
        snprintf(b, sizeof b, "%.17g+%.17gj", re, im);
    out += b;
}

// Buffer-backed mp_print_t (for int/str rendering and exception messages).
struct buf_sink {
    char *buf;
    size_t size;
    size_t pos;
};
static void buf_sink_strn(void *data, const char *str, size_t len) {
    buf_sink *b = (buf_sink *)data;
    for (size_t i = 0; i < len && b->pos + 1 < b->size; i++)
        b->buf[b->pos++] = str[i];
    if (b->size) b->buf[b->pos] = '\0';
}

// Append one value to out, using str()-style rendering via mp_obj_print_helper.
static void append_via_print(std::string &out, mp_obj_t o) {
    char tmp[256];
    buf_sink bs{tmp, sizeof tmp, 0};
    mp_print_t pr{&bs, buf_sink_strn};
    mp_obj_print_helper(&pr, o, PRINT_STR);
    out.append(tmp, bs.pos);
}

// Render one value. `display` selects the human (j-notation) complex format for
// the console; otherwise the wire (I*-notation) format for IPC. Every other
// type is rendered identically on both sides.
static void format_value(std::string &out, mp_obj_t o, bool display) {
    if (mp_obj_is_type(o, &mp_type_complex)) {
        mp_float_t re = 0, im = 0;
        mp_obj_complex_get(o, &re, &im);
        if (display) format_complex_display(out, (double)re, (double)im);
        else         format_complex(out, (double)re, (double)im);
    } else if (o == mp_const_true) {
        out += "true";
    } else if (o == mp_const_false) {
        out += "false";
    } else if (mp_obj_is_float(o)) {
        char b[40];
        snprintf(b, sizeof b, "%.17g", (double)mp_obj_get_float(o));
        out += b;
    } else if (mp_obj_is_str(o)) {
        size_t n = 0;
        const char *s = mp_obj_str_get_data(o, &n);
        out.append(s, n);
    } else {
        // int (incl. big ints), and any other object -> str() rendering.
        append_via_print(out, o);
    }
}

// Capture a command's return value into both s_last_result (wire, for IPC) and
// s_last_display (human, for the console), tab-joining a tuple.
static void capture_result(mp_obj_t res) {
    if (mp_obj_is_type(res, &mp_type_tuple)) {
        size_t n = 0;
        mp_obj_t *items = nullptr;
        mp_obj_get_array(res, &n, &items);
        for (size_t k = 0; k < n; k++) {
            if (k) { s_last_result += '\t'; s_last_display += '\t'; }
            format_value(s_last_result, items[k], false);
            format_value(s_last_display, items[k], true);
        }
    } else {
        format_value(s_last_result, res, false);
        format_value(s_last_display, res, true);
    }
}

// Wire error: the exception's message (str(exc)), single value. RuntimeError
// raised from a bound function carries e.what(), so FEMM error strings match.
static int format_exception(mp_obj_t exc, char *errmsg, int errmsg_size) {
    if (errmsg && errmsg_size > 0) {
        buf_sink bs{errmsg, (size_t)errmsg_size, 0};
        mp_print_t pr{&bs, buf_sink_strn};
        mp_obj_print_helper(&pr, exc, PRINT_STR);
        // Exceptions with no args (KeyboardInterrupt from the Stop control)
        // str() to "" — fall back to the type name so the console says
        // something useful.
        if (bs.pos == 0)
            snprintf(errmsg, errmsg_size, "%s", mp_obj_get_type_str(exc));
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
static void store_global(const char *name, mp_obj_t v) {
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_globals_get()),
                      MP_OBJ_NEW_QSTR(qstr_from_strn(name, std::strlen(name))), v);
}

void init_py() {
    // MICROPY_STACK_CHECK: deep script recursion raises RecursionError instead
    // of overflowing the C stack and killing the process. Anchors stack_top at
    // the OS stack base so the limit is valid from every later exec entry point
    // regardless of call depth (see femm_stack_guard.h). Must run on the thread
    // that will execute scripts — the GUI/main thread.
    femm_init_stack_guard();
    gc_init(s_heap, s_heap + sizeof(s_heap));
    mp_init();

#if MICROPY_VFS_POSIX
    // Mount the host filesystem at "/" so Python open()/read()/write() work
    // (used by user scripts writing analysis results to files).
    {
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        // Make the just-mounted root the current VFS so relative paths resolve
        // against the process working directory (per the unix port).
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
        while (MP_STATE_VM(vfs_cur)->next != NULL)
            MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_cur)->next;
    }
#endif

    // All three entry points (console, TCP exec, line-edit) share this dict.
    femm_py::femm_module.globals = mp_globals_get();

    // Set up sys.path so user scripts can `import` from a packages directory
    // next to the executable and from the current working directory.
    // "" = CWD (so imports work relative to the user's script location).
    // exeDir()+"python" = a packages dir co-located with the FEMM binaries.
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("", 0));
    {
        std::string pkgdir = exeDir() + "python";
        mp_obj_list_append(mp_sys_path,
            mp_obj_new_str(pkgdir.c_str(), pkgdir.size()));
    }

    // Wire invariant: I is the imaginary unit on both sides of the wire.
    store_global("I", mp_obj_new_complex(0, 1));
    const double kPi = 3.14159265358979323846;
    store_global("pi", mp_obj_new_float((mp_float_t)kPi));
    store_global("PI", mp_obj_new_float((mp_float_t)kPi));

    // Complex helpers, bound at the C level. The
    // complex-aware math overloads (abs/sqrt/sin/...) come with the Phase 6
    // init script.
    femm_py::femm_module.def("Complex", [](double r, std::optional<double> i) {
        return CComplex(r, i.value_or(0.0));
    });
    femm_py::femm_module.def("Re",   [](CComplex z) { return z.re; });
    femm_py::femm_module.def("Im",   [](CComplex z) { return z.im; });
    femm_py::femm_module.def("re",   [](CComplex z) { return z.re; });
    femm_py::femm_module.def("im",   [](CComplex z) { return z.im; });
    femm_py::femm_module.def("conj", [](CComplex z) { return CComplex(z.re, -z.im); });
    femm_py::femm_module.def("arg",  [](CComplex z) { return std::atan2(z.im, z.re); });

    // Complex transcendentals backed by CComplex -- the same implementations
    // (and branch cuts) original FEMM used, and more complete than MicroPython's
    // cmath (which lacks asin/acos/atan/tan/sinh/cosh/tanh). Bound under private
    // names; the init script wraps them with a real fast-path (math.* for
    // in-domain reals, which stay float and flow into double-typed bound calls)
    // and routes complex args / out-of-domain reals here, so sqrt(-1)==1j etc.
    // CComplex also gives cleaner roots than cmath (sqrt(-1) is exactly 1j).
    femm_py::femm_module.def("_csqrt", [](CComplex z) { return sqrt(z); });
    femm_py::femm_module.def("_clog",  [](CComplex z) { return log(z);  });
    femm_py::femm_module.def("_cexp",  [](CComplex z) { return exp(z);  });
    femm_py::femm_module.def("_csin",  [](CComplex z) { return sin(z);  });
    femm_py::femm_module.def("_ccos",  [](CComplex z) { return cos(z);  });
    femm_py::femm_module.def("_ctan",  [](CComplex z) { return tan(z);  });
    femm_py::femm_module.def("_casin", [](CComplex z) { return asin(z); });
    femm_py::femm_module.def("_cacos", [](CComplex z) { return acos(z); });
    femm_py::femm_module.def("_catan", [](CComplex z) { return atan(z); });
    femm_py::femm_module.def("_csinh", [](CComplex z) { return sinh(z); });
    femm_py::femm_module.def("_ccosh", [](CComplex z) { return cosh(z); });
    femm_py::femm_module.def("_ctanh", [](CComplex z) { return tanh(z); });

    // String accumulator for bulk data retrieval from script.
    // Builds a C++ string on the native heap (no GC pressure), then moves
    // it into the wire result buffer. Usage from script:
    //   straccum(estimated_bytes)
    //   for k in range(n): strappend(v1, "\t", v2, "\t", ...)
    //   strfinish()
    femm_py::femm_module.def("straccum", [](std::optional<int> hint) {
        s_accum.clear();
        if (hint) s_accum.reserve(*hint);
    });
    femm_py::femm_module.def("strfinish", []() {
        if (!s_accum.empty() && s_accum.back() == '\t')
            s_accum.pop_back();
        py_set_result(std::move(s_accum));
    });

    // strappend: raw MicroPython function — accepts any number of args,
    // formats each via format_value (same as the wire formatter).
    {
        static mp_obj_fun_builtin_var_t strappend_obj = {
            .base = { &mp_type_fun_builtin_var },
            .sig = MP_OBJ_FUN_MAKE_SIG(0, MP_OBJ_FUN_ARGS_MAX, false),
            .fun = { .var = [](size_t n, const mp_obj_t *args) -> mp_obj_t {
                for (size_t i = 0; i < n; i++) {
                    if (i) s_accum += '\t';
                    format_value(s_accum, args[i], false);  // wire format
                }
                s_accum += '\t';
                return mp_const_none;
            }}
        };
        store_global("strappend", MP_OBJ_FROM_PTR(&strappend_obj));
    }

    // Console-control globals. These route through the view-command callback so
    // they are harmless no-ops in headless mode; the GUI console (py_console.cpp)
    // overrides them with direct widget control when present. (Mirrors the
    // the view-command callback dispatches to the GUI widget when present.)
    auto viewcb = [](const char *cmd) {
        auto cb = get_view_command_callback();
        if (cb) { double a[4] = {}; cb(cmd, 0, a); }
    };
    femm_py::femm_module.def("showconsole",  [viewcb]() { viewcb("showconsole"); });
    femm_py::femm_module.def("hideconsole",  [viewcb]() { viewcb("hideconsole"); });
    femm_py::femm_module.def("clearconsole", [viewcb]() { viewcb("clearconsole"); });
    femm_py::femm_module.alias("show_console",  "showconsole");
    femm_py::femm_module.alias("hide_console",  "hideconsole");
    femm_py::femm_module.alias("clear_console", "clearconsole");
    femm_py::femm_module.def("messagebox", [](const std::string &msg) {
        std::fprintf(stderr, "%s\n", msg.c_str());
    });

    // Global FEMM document functions (newdocument, open, quit, chdir, main_*)
    // and the scripting-layer dispatch hooks for the document registry. These
    // depend on the per-physics document types and editor registration functions,
    // so they live in a separate TU (py_state_globals.cpp) to keep py_state.cpp
    // linkable in the lightweight test harness.
    register_global_femm_funcs();

    // Complex linear algebra (_clinalg_eig/_qr) backed by common/fullmatrix.cpp,
    // filling ulab's complex-linalg gaps. Must precede the init script, which
    // grafts these onto numpy.linalg / a `clinalg` namespace.
    register_clinalg();

    // Run the embedded init script: complex-aware math (sqrt/sin/...), unit
    // constants, AWG, the asymptotic-BC permeability tables, and the
    // per-physics draw/makeABC helpers. (The FEMM document API it references
    // is resolved at call time, so it need not be registered yet.)
    char initerr[512] = {};
    if (exec_python(PY_INIT_SCRIPT, initerr, sizeof initerr) != 0)
        std::fprintf(stderr, "py init script error: %s\n", initerr);
}

int exec_python(const char *cmd, char *errmsg, int errmsg_size) {
    if (errmsg && errmsg_size > 0) errmsg[0] = '\0';
    s_last_result.clear();
    s_last_display.clear();

    // Track nesting (event pumping can re-enter via toolbar actions) and
    // drop any interrupt requested after the previous script already ended,
    // so a stale Stop click can't kill the next script at its first branch.
    // NB: the nlr longjmps below land in this function's own nlr bufs, never
    // past this frame, so the RAII guard's destructor always runs.
    struct RunningGuard {
        RunningGuard() {
            if (s_running_depth++ == 0)
                MP_STATE_THREAD(mp_pending_exception) = MP_OBJ_NULL;
        }
        ~RunningGuard() {
            // Fire only when the outermost script finishes, so a batch file
            // (one exec_python call for the whole file) repaints once at the
            // end while interactive lines repaint per line. Re-entrant execs
            // via event pumping exit at depth>0 and stay silent. s_last_result
            // is already captured by the time we unwind, so the GUI's hook can
            // apply its mutator/getter gate. The nlr longjmps land in this
            // frame's own bufs (see above), so this destructor always runs.
            if (--s_running_depth == 0 && s_post_exec_hook)
                s_post_exec_hook();
        }
    } running_guard;

    nlr_buf_t nlr;
    mp_obj_t module_fun = MP_OBJ_NULL;
    mp_parse_input_kind_t kind = MP_PARSE_EVAL_INPUT;

    // Attempt 1: compile as a single expression.
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, cmd,
                                                    strlen(cmd), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_EVAL_INPUT);
        module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        nlr_pop();
    } else {
        module_fun = MP_OBJ_NULL;  // not a valid expression; fall through
    }

    // Attempt 2: compile as statements.
    if (module_fun == MP_OBJ_NULL) {
        kind = MP_PARSE_FILE_INPUT;
        if (nlr_push(&nlr) == 0) {
            mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, cmd,
                                                        strlen(cmd), 0);
            mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
            module_fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
            nlr_pop();
        } else {
            return format_exception((mp_obj_t)nlr.ret_val, errmsg, errmsg_size);
        }
    }

    // Execute.
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_0(module_fun);
        nlr_pop();
        if (kind == MP_PARSE_EVAL_INPUT && res != mp_const_none)
            capture_result(res);
        return 0;
    } else {
        return format_exception((mp_obj_t)nlr.ret_val, errmsg, errmsg_size);
    }
}

// ---------------------------------------------------------------------------
// Expression-only evaluator for line-edit widgets.
// Compiles + executes as MP_PARSE_EVAL_INPUT only. Returns MP_OBJ_NULL on any
// compile or runtime error (silent, matching sol::safe_script(pass_on_error)).
// ---------------------------------------------------------------------------
static mp_obj_t eval_expr(const char *expr) {
    nlr_buf_t nlr;
    mp_obj_t fun = MP_OBJ_NULL;

    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, expr, std::strlen(expr), 0);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_EVAL_INPUT);
        fun = mp_compile(&pt, MP_QSTR__lt_stdin_gt_, false);
        nlr_pop();
    } else {
        return MP_OBJ_NULL;
    }

    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_0(fun);
        nlr_pop();
        return res;
    } else {
        return MP_OBJ_NULL;
    }
}

bool py_eval_double(const char *expr, double *out) {
    mp_obj_t res = eval_expr(expr);
    if (res == MP_OBJ_NULL) return false;
    if (mp_obj_is_type(res, &mp_type_complex)) {
        mp_float_t re = 0, im = 0;
        mp_obj_complex_get(res, &re, &im);
        *out = (double)re;
        return true;
    }
    if (mp_obj_is_float(res) || mp_obj_is_int(res)) {
        *out = (double)mp_obj_get_float(res);
        return true;
    }
    return false;
}

bool py_eval_complex(const char *expr, double *re, double *im) {
    mp_obj_t res = eval_expr(expr);
    if (res == MP_OBJ_NULL) return false;
    if (mp_obj_is_type(res, &mp_type_complex)) {
        mp_float_t r = 0, i = 0;
        mp_obj_complex_get(res, &r, &i);
        *re = (double)r;
        *im = (double)i;
        return true;
    }
    if (mp_obj_is_float(res) || mp_obj_is_int(res)) {
        *re = (double)mp_obj_get_float(res);
        *im = 0.0;
        return true;
    }
    return false;
}

bool py_eval_string(const char *expr, std::string *out) {
    mp_obj_t res = eval_expr(expr);
    if (res == MP_OBJ_NULL || res == mp_const_none) return false;
    out->clear();
    format_value(*out, res, true);  // display format (j notation) for line-edit
    return true;
}

void close_py() {
    cleanup_global_femm_state();
    gc_sweep_all();
    mp_deinit();
#if MICROPY_GC_SPLIT_HEAP_AUTO
    // Free any heap segments gc_try_add_heap() malloc'd; mp_deinit doesn't.
    // The first area is the static s_heap and stays. Matters if the
    // interpreter is ever cycled (close_py/init_py) within one process.
    for (mp_state_mem_area_t *a = MP_STATE_MEM(area).next; a != NULL;) {
        mp_state_mem_area_t *next = a->next;
        free(a);
        a = next;
    }
    MP_STATE_MEM(area).next = NULL;
#endif
}

int exec_python_file(const char *path, char *errmsg, int errmsg_size) {
    FILE *f = std::fopen(path, "r");
    if (!f) {
        if (errmsg) std::snprintf(errmsg, errmsg_size, "Cannot open %s", path);
        return -1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string buf(sz, '\0');
    std::fread(&buf[0], 1, sz, f);
    std::fclose(f);
    return exec_python(buf.c_str(), errmsg, errmsg_size);
}
