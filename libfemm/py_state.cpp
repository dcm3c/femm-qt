// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors
//
// py_state.cpp - embedded MicroPython lifecycle + shared global namespace.
// See py_state.h for interface notes.
#include "py_state.h"
#include "utf8_io.h"
#include "py_bind.h"
#include "py_clinalg.h"
#include "py_init_script.h"
#include "py_os.h"
#include "py_pybridge.h"
#include "pybridge_host.h"
#include "femm_resource.h"
#include "script_callbacks.h"
#include "user_paths.h"

#include <cstdio>

extern "C" {
#include "py/compile.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mpprint.h"
#include "py/repl.h"
#include "py/stream.h"   // MP_STREAM_POLL_WR, for mp_hal_stdio_poll below
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "ndarray.h"   // ulab — header is not extern-"C" guarded, so wrap it
}

#include "femm_stack_guard.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>

#if MICROPY_GC_SPLIT_HEAP
// Fallback collector only (-DFEMM_GC_BDWGC=OFF). Initial GC heap segment:
// MICROPY_GC_SPLIT_HEAP_AUTO grows the heap on demand by malloc'ing further
// segments (capped per split by gc_get_max_new_split() in femm_port_hooks.c)
// and returns empty segments during sweep, so this only needs to cover a
// steady-state console session.
//
// Under bdwgc there is no fixed heap at all -- see micropython/femm_gc.c and
// docs/bdwgc-handoff-plan.md. That is what lifts the ceiling on array sizes:
// np.zeros(30_000_000) used to be a MemoryError.
static char s_heap[2 * 1024 * 1024];
#endif

// ---------------------------------------------------------------------------
// Stdout routing. We provide mp_hal_stdout_tx_strn[_cooked] ourselves (the
// embed port's port/mphalport.c is excluded from the build) so that print(),
// tracebacks and sys.stdout can be redirected to the GUI console. When no sink
// is set, output goes to the process stdout (headless/TCP mode).
//
// This is the ONLY route out. A GUI-subsystem process has no valid fd 1, so
// anything reaching for one raises EBADF instead of printing -- which is what
// happened when MICROPY_PY_SYS_STDFILES first went on and the vfs_posix
// definitions of sys.stdout won the link. See the rename in CMakeLists.txt.
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
PyVmHook py_vm_hook() { return femm_vm_hook_fn; }

// Fired once when the outermost exec_python() unwinds (s_running_depth -> 0).
// See py_state.h. Headless/TCP leave this null and pay nothing.
static PyPostExecHook s_post_exec_hook = nullptr;

void set_py_post_exec_hook(PyPostExecHook hook) { s_post_exec_hook = hook; }

bool py_is_running() { return s_running_depth > 0; }

void py_request_interrupt() { mp_sched_keyboard_interrupt(); }

bool py_interrupt_pending() {
    return MP_STATE_THREAD(mp_pending_exception) != MP_OBJ_NULL;
}

extern "C" void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    if (s_output_sink) s_output_sink(str, len);
    else std::fwrite(str, 1, len, stdout);
}
extern "C" mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    if (s_output_sink) s_output_sink(str, len);
    else std::fwrite(str, 1, len, stdout);
    return len;
}

// The other two halves of the mp_hal stdio interface. Nothing called them until
// sys_stdio_mphal.c started linking (see the vfs_posix_file.c symbol rename in
// the top-level CMakeLists.txt); its stdio_read/stdio_ioctl reference them, so
// they have to exist or the link fails.
//
// stdin is read straight from the process, which is right when there is one
// (headless in a terminal) and must not hang when there is not. A GUI-subsystem
// process has no console at all, and scripts run on the GUI thread, so a
// blocking read there would wedge the whole program. getchar() returns EOF
// immediately on an invalid descriptor, and EOF is reported as Ctrl-D (4),
// which is what MicroPython's own ports return -- verified not to hang in a
// console-less GUI. builtins.input() does not exist in this port at all, so
// sys.stdin.read() is the only way a script can get here.
extern "C" int mp_hal_stdin_rx_chr(void) {
    int c = std::getchar();
    return (c == EOF) ? 4 : c;
}

// Always writable, never readable -- consistent with the above.
extern "C" uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    return poll_flags & MP_STREAM_POLL_WR;
}

// ---------------------------------------------------------------------------
// Wire-compatible result formatting.
//
// Wire-format result capture. exec_python formats the return value into
// s_last_result. Byte-for-byte formatting rules:
//   complex -> CComplex::ToString STRUCTURE (im==0/+-1 / re==0 cases), with
//              shortest round-trip digits rather than %.17g
//   float   -> shortest round-trip digits (std::to_chars)
//   bool    -> "true"/"false"  (wire compat, NOT Python "True"/"False")
//   int     -> integer digits        (MicroPython repr; handles big ints)
//   str     -> the string, unquoted
//   tuple   -> elements tab-separated
// print() output and tracebacks are NOT part of this (they go to stdout, like
// the output sink); only return values reach the wire.
// ---------------------------------------------------------------------------
static std::string s_last_result;   // wire format (I* notation) — for IPC
static std::string s_last_display;  // human display (j notation) — for the console

const std::string &py_last_result() { return s_last_result; }
const std::string &py_last_display() { return s_last_display; }
void py_set_result(std::string &&result) { s_last_result = std::move(result); }

// Append one double as the SHORTEST digit string that strtod reads back to the
// identical value (std::to_chars). This is common/fastio.h's technique, applied
// here for the same reason it was applied to the .ans hot loops: snprintf
// re-parses its format string and goes through the locale machinery on every
// call. Measured on a bulk mesh readout -- 126k values, the mix
// mo_getallelements emits -- 26.8 ms for snprintf("%.17g") against 2.6 ms here,
// with zero round-trip mismatches.
//
// The BYTES therefore differ from %.17g: "0.1" rather than "0.10000000000000001".
// Measured 2-4% shorter on real solver output -- do NOT expect fastio.h's
// 30-40%, which is for .ans files, mostly round INPUT values; computed field
// values carry full mantissas and barely shrink. Speed is the win here, not
// size. Every value still round-trips bit-exactly, and
// every binding parses with strtod / str2num / float(), so this is invisible to
// them -- what it moves is any baseline that compares wire text literally.
// inf, nan, -0 and the exponent form ("6.02214076e+23") are unchanged.
static void append_g(std::string &out, double v) {
    char b[40];   // longest shortest-form double is 24 chars
    auto r = std::to_chars(b, b + sizeof b, v);
    out.append(b, (size_t)(r.ptr - b));
}

// Replicates CComplex::ToString(s, n)'s STRUCTURE -- the im==0, im==+-1 and
// re==0 special cases -- with append_g digits in place of its %.17g. Note that
// common/complex.cpp still writes %.17g, so the two no longer agree byte for
// byte; they agree on every value, which is what the wire contract needs.
static void format_complex(std::string &out, double re, double im) {
    if (im == 0) {
        append_g(out, re);
    } else if (im == 1) {
        if (re == 0) out += "I";
        else { append_g(out, re); out += "+I"; }
    } else if (im == -1) {
        if (re == 0) out += "-I";
        else { append_g(out, re); out += "-I"; }
    } else if (im < 0) {
        if (re != 0) append_g(out, re);
        out += "-I*";
        append_g(out, fabs(im));
    } else {  // im > 0
        if (re != 0) { append_g(out, re); out += "+I*"; }
        else           out += "I*";
        append_g(out, im);
    }
}

// Human-facing complex rendering for the console and line-edit: Python "j"
// notation (e.g. "1.5+2j", no parentheses), with a real-valued complex (im==0)
// collapsed to a bare real so genuinely-real results read cleanly. Shortest
// round-trip precision: the exact value, in the fewest digits that mean it.
// The TCP wire uses format_complex() above (I* notation) instead. The I*
// NOTATION is the IPC contract and must not change; the digit count is not,
// and became shortest-round-trip in 2026-08 (see append_g).
static void format_complex_display(std::string &out, double re, double im) {
    // Complex stays complex, matching _femm_fmt in the init script -- one rule,
    // two places it has to be written. The zero-imaginary collapse that used to
    // live here existed because the DC getters returned complex-typed reals;
    // they return real numbers now, so a complex result means a phasor and its
    // zero imaginary part is information.
    if (re == 0 && im != 0) {         // pure imaginary: "0.5j", "-3j", "1j"
        append_g(out, im); out += 'j';
    } else if (im < 0) {
        append_g(out, re); out += '-'; append_g(out, -im); out += 'j';
    } else {
        append_g(out, re); out += '+'; append_g(out, im); out += 'j';
    }
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

// ---------------------------------------------------------------------------
// ndarray -> wire text
// ---------------------------------------------------------------------------
// A bulk getter returns typed arrays; the wire wants the same tab-separated text
// it has always sent. Flattening here is what lets ONE implementation serve
// both: the in-process caller gets arrays, and the IPC caller gets bytes
// identical to what the hand-written string builders used to produce.
//
// Every value goes through the SAME per-value formatter as every scalar result,
// which is what makes that byte-identity hold rather than merely look likely.
// Note in particular that append_g emits "1" for 1.0, exactly as the old
// appendInt did for an integer field -- so a float64 array carrying node indices
// reproduces the old text without needing an integer dtype.
//
// The dtype is dispatched ONCE, outside the loop. Doing it per value (the shape
// format_value uses for scalars) costs more than the formatting does.
//
// Walks with strides rather than assuming dense, so a view or a slice of a
// larger array formats correctly. ULAB_MAX_DIMS is 2 here; the #error fires
// rather than silently mis-walking if that is ever raised.
#if ULAB_MAX_DIMS > 2
#error "femm-qt: extend append_ndarray() before raising ULAB_MAX_DIMS"
#endif

template <class Emit>
static void walk_ndarray(ndarray_obj_t *a, Emit emit) {
    // A 1-D array leaves shape[MAX-2] at zero, so the row count has to come from
    // ndim, not from the shape slot -- reading it would run the loop zero times.
    size_t rows = (a->ndim >= 2) ? a->shape[ULAB_MAX_DIMS - 2] : 1;
    size_t cols = a->shape[ULAB_MAX_DIMS - 1];
    int32_t rstride = a->strides[ULAB_MAX_DIMS - 2];
    int32_t cstride = a->strides[ULAB_MAX_DIMS - 1];
    uint8_t *row = (uint8_t *)a->array;
    for (size_t r = 0; r < rows; r++) {
        uint8_t *p = row;
        for (size_t c = 0; c < cols; c++) { emit(p); p += cstride; }
        row += rstride;
    }
}

// Row/column geometry, treating a 1-D array as a single column so it can sit
// beside 2-D ones in a table (see append_ndarray_table).
static size_t nd_rows(ndarray_obj_t *a) {
    return (a->ndim >= 2) ? a->shape[ULAB_MAX_DIMS - 2] : a->shape[ULAB_MAX_DIMS - 1];
}
static size_t nd_cols(ndarray_obj_t *a) {
    return (a->ndim >= 2) ? a->shape[ULAB_MAX_DIMS - 1] : 1;
}
static int32_t nd_rstride(ndarray_obj_t *a) {
    return a->strides[(a->ndim >= 2) ? ULAB_MAX_DIMS - 2 : ULAB_MAX_DIMS - 1];
}

static void append_cell(std::string &out, uint8_t dtype, const uint8_t *p) {
    switch (dtype) {
        case NDARRAY_FLOAT:   append_g(out, (double)*(const mp_float_t *)p); break;
        case NDARRAY_COMPLEX: {
            const mp_float_t *z = (const mp_float_t *)p;
            format_complex(out, (double)z[0], (double)z[1]);
            break;
        }
        case NDARRAY_INT32:   out += std::to_string(*(const int32_t *)p); break;
        case NDARRAY_INT16:   out += std::to_string(*(const int16_t *)p); break;
        case NDARRAY_UINT16:  out += std::to_string(*(const uint16_t *)p); break;
        case NDARRAY_INT8:    out += std::to_string((int)*(const int8_t *)p); break;
        case NDARRAY_UINT8:   out += std::to_string((unsigned)*p); break;
        default:              out += "<unknown dtype>"; break;
    }
}

static void append_ndarray(std::string &out, ndarray_obj_t *a) {
    bool first = out.empty();
    auto sep = [&out, &first]() { if (!first) out += '\t'; first = false; };
    switch (a->dtype) {
        case NDARRAY_FLOAT:
            walk_ndarray(a, [&](uint8_t *p) { sep(); append_g(out, (double)*(mp_float_t *)p); });
            break;
        case NDARRAY_COMPLEX:
            walk_ndarray(a, [&](uint8_t *p) {
                sep();
                mp_float_t *z = (mp_float_t *)p;
                format_complex(out, (double)z[0], (double)z[1]);
            });
            break;
        case NDARRAY_INT32:
            walk_ndarray(a, [&](uint8_t *p) { sep(); out += std::to_string(*(int32_t *)p); });
            break;
        case NDARRAY_INT16:
            walk_ndarray(a, [&](uint8_t *p) { sep(); out += std::to_string(*(int16_t *)p); });
            break;
        case NDARRAY_UINT16:
            walk_ndarray(a, [&](uint8_t *p) { sep(); out += std::to_string(*(uint16_t *)p); });
            break;
        case NDARRAY_INT8:
            walk_ndarray(a, [&](uint8_t *p) { sep(); out += std::to_string((int)*(int8_t *)p); });
            break;
        case NDARRAY_UINT8:   // also the storage for a boolean array
            walk_ndarray(a, [&](uint8_t *p) { sep(); out += std::to_string((unsigned)*p); });
            break;
        default:
            // Unreachable: dtype is validated at construction (ndarray.c). Say so
            // rather than emit nothing, which would look like an empty result.
            out += "<unknown dtype>";
            break;
    }
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
        append_g(out, (double)mp_obj_get_float(o));
    } else if (mp_obj_is_str(o)) {
        size_t n = 0;
        const char *s = mp_obj_str_get_data(o, &n);
        out.append(s, n);
    } else if (!display && mp_obj_is_type(o, &ulab_ndarray_type)) {
        // Wire: the full array, flattened. Display: deliberately NOT flattened --
        // it falls through to append_via_print below and gets ulab's own repr,
        // which abbreviates to a few elements. Building the console copy of a
        // bulk result would double both the time and the memory of every such
        // call (74 MB of text at a million elements) to produce something no
        // console can show.
        append_ndarray(out, (ndarray_obj_t *)MP_OBJ_TO_PTR(o));
    } else {
        // int (incl. big ints), and any other object -> str() rendering.
        append_via_print(out, o);
    }
}

// A tuple whose items are ALL ndarrays sharing a leading dimension is a TABLE:
// the wire gets it ROW-MAJOR, each row contributing every array's row-k slice in
// tuple order. That is what lets mo_getallelements return int32 connectivity and
// float64 centroids as separate typed columns while still producing the
// record-ordered text the wire has always carried.
//
// Flattening the arrays one after another instead would emit
// [all connectivity][all centroids][all areas][all groups] -- column-block
// order, which parses without complaint into a completely wrong mesh.
//
// A 1-D array counts as one column, so it can sit beside 2-D ones.
static bool is_ndarray_table(mp_obj_t res, size_t &n, mp_obj_t *&items) {
    if (!mp_obj_is_type(res, &mp_type_tuple)) return false;
    mp_obj_get_array(res, &n, &items);
    if (n == 0) return false;
    size_t rows = 0;
    for (size_t k = 0; k < n; k++) {
        if (!mp_obj_is_type(items[k], &ulab_ndarray_type)) return false;
        size_t r = nd_rows((ndarray_obj_t *)MP_OBJ_TO_PTR(items[k]));
        if (k == 0) rows = r;
        else if (r != rows) return false;
    }
    return true;
}

static void append_ndarray_table(std::string &out, size_t n, mp_obj_t *items) {
    ndarray_obj_t *a[8];
    size_t cols[8], count = (n < 8) ? n : 8;
    for (size_t k = 0; k < count; k++) {
        a[k] = (ndarray_obj_t *)MP_OBJ_TO_PTR(items[k]);
        cols[k] = nd_cols(a[k]);
    }
    size_t rows = nd_rows(a[0]);
    bool first = true;
    for (size_t r = 0; r < rows; r++) {
        for (size_t k = 0; k < count; k++) {
            const uint8_t *p = (const uint8_t *)a[k]->array + (size_t)nd_rstride(a[k]) * r;
            int32_t cs = a[k]->strides[ULAB_MAX_DIMS - 1];
            for (size_t c = 0; c < cols[k]; c++) {
                if (!first) out += '\t';
                first = false;
                append_cell(out, a[k]->dtype, p + (size_t)cs * c);
            }
        }
    }
}

// Capture a command's return value into both s_last_result (wire, for IPC) and
// s_last_display (human, for the console), tab-joining a tuple.
static void capture_result(mp_obj_t res) {
    size_t tn = 0;
    mp_obj_t *titems = nullptr;
    if (is_ndarray_table(res, tn, titems)) {
        append_ndarray_table(s_last_result, tn, titems);
        // Console: the abbreviated repr of each column, not the whole table --
        // same reasoning as the single-array case in format_value.
        for (size_t k = 0; k < tn; k++) {
            if (k) s_last_display += '\t';
            append_via_print(s_last_display, titems[k]);
        }
        return;
    }
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
// Where an mp_obj_t may live, and why it is load-bearing under bdwgc.
//
// The collector finds roots by scanning the C stack, registers and STATIC data
// segments. It does NOT scan memory obtained from C++ new/malloc, so an
// mp_obj_t held in a std::vector, a std::string, a Qt container or a
// heap-allocated context object is invisible and its object can be collected
// while still referenced. Keep such references in the Python globals dict (via
// this function), or in static storage.
//
// This is now doubly load-bearing: the root policy in micropython/femm_gc.c
// deliberately does NOT call GC_set_no_dls or GC_exclude_static_roots, on the
// grounds that the static data segments must stay scanned precisely because we
// keep mp_obj_t there. A refactor that moves interpreter state out of statics
// and into a heap-allocated struct would silently break that bargain -- clean
// compile, and a use-after-free under memory pressure. Carry this comment along
// if that ever happens.
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
#if MICROPY_GC_SPLIT_HEAP
    gc_init(s_heap, s_heap + sizeof(s_heap));
#else
    gc_init(nullptr, nullptr);   // bdwgc: no fixed heap, see micropython/femm_gc.c
#endif
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

    // `os` and `os.path`. After the mount, so open() and os.* agree about the
    // working directory from the first line of a script.
    register_os_py();

    // `_pybridge`: transport only, and it spawns nothing until a script asks.
    register_pybridge_py();

    // All three entry points (console, TCP exec, line-edit) share this dict.
    femm_py::femm_module.globals = mp_globals_get();

    // Set up sys.path. MicroPython has already put "" (the current directory,
    // so imports work relative to the user's script) at the front in
    // runtime.c:145 -- appending our own "" as well only made every failed
    // import stat the working directory twice.
    //
    // Order after that is deliberate:
    //   exeDir()+"python"   the vendored stdlib that ships with the program
    //   userDataDir()+python  the user's own modules, LAST
    //
    // Last, so the shipped modules always win. This directory is the user's
    // equivalent of site-packages, but the directory above it is the STDLIB --
    // someone dropping a csv.py or json.py in here and silently shadowing ours
    // would produce failures that look like interpreter bugs.
    //
    // The user directory is the only place they CAN write: exeDir()+"python" is
    // replaced wholesale by every installer, and on an FHS install it is
    // root-owned. It is appended whether or not it exists -- a missing sys.path
    // entry is skipped, and creating directories at startup is not this
    // function's business.
    {
        // femmResource, not exeDir() alone: on a split (FHS) install the
        // stdlib is in ../share/femm/python while the executable is in bin/.
        std::string pkgdir = femmResource("python");
        mp_obj_list_append(mp_sys_path,
            mp_obj_new_str(pkgdir.c_str(), pkgdir.size()));
    }
    {
        std::string userdir = userDataDir();
        if (!userdir.empty()) {
            userdir += "python";
            mp_obj_list_append(mp_sys_path,
                mp_obj_new_str(userdir.c_str(), userdir.size()));
        }
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

    // (straccum/strappend/strfinish lived here: a native-heap string builder a
    // binding could drive to assemble a bulk result client-side. The bulk
    // getters made it pointless -- and it was wire-only, since strfinish
    // pushed into the result buffer via py_set_result and returned None, so an
    // in-process caller got nothing back. That is exactly the local-vs-wire
    // split the bulk getters removed. See git history for the technique.)

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
        // Show a modal dialog in the GUI; fall back to stderr in headless.
        auto cb = get_message_box_callback();
        if (cb) cb(msg.c_str());
        else std::fprintf(stderr, "%s\n", msg.c_str());
    });
    femm_py::femm_module.alias("message_box", "messagebox");

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

    // Skip a leading UTF-8 BOM (EF BB BF). Windows editors (Notepad, etc.)
    // often save a .py "as UTF-8" with a BOM; without this the first token
    // parses as a stray identifier -- e.g. "name '﻿import' isn't defined".
    // CPython's tokenizer strips it likewise. This is the one chokepoint every
    // entry path funnels through (files via exec_python_file, the console, TCP,
    // the init script), so one strip here covers them all. The short-circuit
    // makes it safe on any NUL-terminated string, including "" and a lone BOM.
    if ((unsigned char)cmd[0] == 0xEF &&
        (unsigned char)cmd[1] == 0xBB &&
        (unsigned char)cmd[2] == 0xBF)
        cmd += 3;

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

bool py_repl_continue(const char *src) {
    return mp_repl_continue_with_input(src);
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
    // The worker is a child process, not a heap object: nothing below would
    // reap it. close_py() is only ever called at process exit today, so this
    // is "femm-qt is quitting" rather than "the interpreter is being reset" --
    // when a reset command does exist it should send `clear` instead, keeping
    // the worker and its figure windows alive.
    femm_pybridge_shutdown();
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

void py_set_argv(const std::vector<std::string> &argv) {
    // sys.argv is a VM root pointer (MP_REGISTER_ROOT_POINTER in modsys.c), so
    // the list and the strings appended to it are reachable by the collector --
    // no rooting is needed here beyond building it in place.
    // MP_OBJ_TO_PTR is a C-style void* cast; C++ needs the target type named.
    mp_obj_list_init(static_cast<mp_obj_list_t *>(MP_OBJ_TO_PTR(mp_sys_argv)), 0);
    for (const std::string &a : argv)
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(a.c_str(), a.size()));
}

int exec_python_file(const char *path, char *errmsg, int errmsg_size) {
    // Binary, and sized by what was actually READ -- both halves matter, and
    // getting either wrong is invisible until a script happens to be the wrong
    // length. Opened "r" (text) on Windows, ftell reports the on-disk byte
    // count while fread returns CHARACTERS, fewer by one per CRLF; the two
    // disagree, and a CRLF script of certain sizes reached the lexer damaged
    // and failed with a bare "invalid syntax" naming no line. A 400-statement
    // file failed where the identical file with LF endings, and the same file
    // at 410 statements, both ran.
    //
    // MicroPython's lexer handles CRLF itself, so there is nothing to gain
    // from letting the CRT translate.
    FILE *f = femm_fopen(path, "rb");
    if (!f) {
        if (errmsg) std::snprintf(errmsg, errmsg_size, "Cannot open %s", path);
        return -1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    std::string buf(static_cast<size_t>(sz), '\0');
    size_t got = std::fread(&buf[0], 1, static_cast<size_t>(sz), f);
    const bool shortRead = std::ferror(f) != 0;
    std::fclose(f);
    if (shortRead) {
        if (errmsg) std::snprintf(errmsg, errmsg_size, "Cannot read %s", path);
        return -1;
    }
    buf.resize(got);
    // A NUL anywhere in the source would silently truncate the script at the
    // lexer, so say so instead. (Reaching here means a binary file was handed
    // to -run, not that anything above went wrong.)
    if (buf.find('\0') != std::string::npos) {
        if (errmsg)
            std::snprintf(errmsg, errmsg_size,
                          "%s is not a text file (contains a NUL byte)", path);
        return -1;
    }
    const int status = exec_python(buf.c_str(), errmsg, errmsg_size);

    // Collect once the script is done, if this was the OUTERMOST one (a script
    // that dofile()s another must not collect halfway through the caller).
    //
    // Two things happen here that cannot happen anywhere else. bdwgc only
    // queues a finaliser when a collection finds the object unreachable, and
    // with free_space_divisor(2) a script that never exhausts memory may
    // trigger no collection at all -- so a file dropped without close() would
    // otherwise stay open until something else forced one. And this is the
    // moment when the C stack is shallowest and the registers least likely to
    // still hold a dead pointer, so a block that is genuinely garbage has its
    // best chance of being seen as garbage. A conservative collector cannot
    // guarantee that; it can only be asked at the most favourable moment.
    //
    // Deliberately here and NOT in exec_python(): that runs once per command
    // over the TCP interface, where round trips are 0.05-0.5 ms and a
    // collection is 0.4-1.5 ms. Per command it would be a throughput
    // regression of 2-30x. Per script it is unmeasurable.
    // ONE collection, for a semantic contract and not for a memory number:
    // finalisers get a predictable home. Under bdwgc a collection only happens
    // when the allocator needs one, so a script that never exhausts memory
    // could otherwise finish with a dropped file still open. Now __del__ runs
    // when the script ends. gc_collect_end() drains the finaliser queue
    // immediately after the collection (micropython/femm_gc.c), so the two are
    // atomic and a __del__ that frees something else has it collected on the
    // next collection, exactly as py/gc.c behaved.
    //
    // It is deliberately NOT tuned to release memory promptly. Repeating the
    // collection does release more -- 240 MB dropped by a script measured
    // 238 MB retained after one pass and 9 MB after three -- but the count that
    // achieves that is a property of one script on one platform, and the pin
    // counts behind it varied 0 to 4 across eight platform runs for no reason
    // more principled than what happened to be in a register. A constant fitted
    // to that would be re-tuned by the next machine. Retention is bounded and
    // non-cumulative (docs/bdwgc-handoff-plan.md 11.4c) -- the block is reused
    // by the next script rather than accumulating -- and that is the property
    // worth guaranteeing.
    //
    // Deliberately here and NOT in exec_python(): that runs once per command
    // over the TCP interface, where round trips are 0.05-0.5 ms and a
    // collection is 0.4-1.5 ms. Per command it would be a throughput
    // regression of 2-30x, and it would put finaliser runs back at arbitrary
    // mid-protocol moments. Per script it is unmeasurable.
    //
    // Guarded on depth so a script that dofile()s another does not collect
    // halfway through the caller.
    if (s_running_depth == 0) gc_collect();
    return status;
}
