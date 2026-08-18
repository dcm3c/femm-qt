// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_port_hooks.c - port-level symbols referenced from inside the
// micropython_embed core (vm.c, gc.c). This file lives OUTSIDE the generated
// micropython_embed/ tree (regen-micropython must not wipe it) and is
// compiled into the micropython_embed library so every embedder — libfemm,
// fkn, the test harnesses — links it without extra wiring.
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "py/mpconfig.h"
#include "py/mphal.h"
#include "py/runtime.h"

// VM-loop callback (see MICROPY_VM_HOOK_LOOP in mpconfigport.h). The GUI
// console sets this to a rate-limited Qt event pump (py_state.cpp
// set_py_vm_hook); NULL when headless, so the hook is just a null check.
void (*femm_vm_hook_fn)(void) = NULL;

#if MICROPY_GC_SPLIT_HEAP_AUTO
// Cap on each additional heap segment gc_try_add_heap() may malloc. Growth
// doubles the total heap per split, so this bounds the step size, not the
// total: the total is bounded only by what the system malloc will grant,
// matching desktop interpreter semantics (CPython behaves the same way).
size_t gc_get_max_new_split(void) {
    return 64u * 1024 * 1024;
}
#endif

// ---------------------------------------------------------------------------
// Clock and delay, for the `time` module (MICROPY_PY_TIME). extmod/modtime.c
// calls these; no port file in the embed package provides them.
// ---------------------------------------------------------------------------
#if MICROPY_PY_TIME

#include <time.h>
#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#endif

// Nanoseconds since the 1970 epoch. Also backs time.time(), via femm_modtime.c.
uint64_t femm_hal_time_ns(void) {
    #ifdef _WIN32
    // GetSystemTimePreciseAsFileTime: 100 ns ticks since 1601-01-01.
    FILETIME ft;
    ULARGE_INTEGER t;
    GetSystemTimePreciseAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (t.QuadPart - 116444736000000000ULL) * 100ULL;
    #else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    #endif
}
uint64_t mp_hal_time_ns(void) {
    return femm_hal_time_ns();
}

// A monotonic microsecond counter for ticks_ms/ticks_us. Monotonic, not
// wall-clock: ticks_diff over a leap second or an NTP correction should still
// be a duration.
static uint64_t femm_monotonic_us(void) {
    #ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64_t)((now.QuadPart * 1000000ULL) / (uint64_t)freq.QuadPart);
    #else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    #endif
}

mp_uint_t mp_hal_ticks_ms(void) { return (mp_uint_t)(femm_monotonic_us() / 1000ULL); }
mp_uint_t mp_hal_ticks_us(void) { return (mp_uint_t)femm_monotonic_us(); }
mp_uint_t mp_hal_ticks_cpu(void) { return (mp_uint_t)femm_monotonic_us(); }

// time.sleep() must not freeze the GUI. femm_vm_hook_fn is the same rate-limited
// Qt event pump the VM loop uses, so sleeping keeps the window alive, repaints,
// and lets the Stop control through -- mp_handle_pending then raises
// KeyboardInterrupt out of the sleep, as Ctrl-C does in CPython. Headless leaves
// the hook null and this is a plain wait.
void mp_hal_delay_us(mp_uint_t us) {
    uint64_t deadline = femm_monotonic_us() + (uint64_t)us;
    for (;;) {
        uint64_t now = femm_monotonic_us();
        if (now >= deadline) return;
        uint64_t remaining = deadline - now;
        if (femm_vm_hook_fn) femm_vm_hook_fn();
        mp_handle_pending(true);
        #ifdef _WIN32
        Sleep(remaining > 5000 ? 5 : 0);      // 5 ms slices; spin for the tail
        #else
        usleep(remaining > 5000 ? 5000 : 0);
        #endif
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    mp_hal_delay_us(ms * 1000);
}

// Seed for the random module (MICROPY_PY_RANDOM_SEED_INIT_FUNC), so
// random.random() differs run to run as it does in CPython. Not cryptographic;
// nothing here needs it to be.
unsigned long femm_random_seed(void) {
    return (unsigned long)(femm_hal_time_ns() ^ (femm_hal_time_ns() >> 32));
}

#endif // MICROPY_PY_TIME

#ifdef _WIN32
// UTF-8 file open for the POSIX VFS (patch 05). MicroPython strings are UTF-8,
// but the CRT's narrow open() decodes a path as ANSI, so a script opening
// "C:/temp/Pruefung/x.txt" with an umlaut would reach a DIFFERENT file from the
// one mi_saveas() writes -- libfemm widens to UTF-16 at the OS boundary
// (common/utf8_io.h) and this is the same boundary. Same conversion, expressed
// in C because it is called from the vendored extmod tree.
//
// Lives here rather than in the patch so the patch stays a two-line diff, and
// here rather than in utf8_io.cpp because that is C++ and this is linked into
// the micropython_embed library.
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

static int femm_widen_path(const char *utf8Path, wchar_t *wbuf, int wbufLen) {
    // 0 on failure (invalid UTF-8, or longer than the buffer), in which case the
    // caller falls back to the narrow call rather than failing outright -- an
    // ASCII path then behaves exactly as it did before.
    return MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, wbuf, wbufLen);
}

int femm_wopen_utf8(const char *utf8Path, int flags, int binary) {
    wchar_t wbuf[1024];
    // State the translation mode instead of inheriting the CRT's global
    // default, which is text for every mode. Text mode on a BINARY file
    // rewrites \n as \r\n both ways: f.write(b"\n\n\n") reached disk as six
    // bytes, and on read a full buffer came back short, which
    // mp_reader_vfs_readbyte takes for EOF -- truncating any script-written
    // module past ~48 bytes into a SyntaxError on import.
    //
    // A text file keeps the translation, because that is what CPython does on
    // Windows (its text mode writes os.linesep and reads it back as \n), and a
    // script here is far more likely to be written by someone with CPython
    // habits than by someone relying on a bare-metal MicroPython port's lack
    // of translation.
    flags |= binary ? _O_BINARY : _O_TEXT;
    if (!femm_widen_path(utf8Path, wbuf, 1024))
        return _open(utf8Path, flags, 0644);
    return _wopen(wbuf, flags, 0644);
}

// Existence test for the import machinery: 0 absent, 1 regular file,
// 2 directory. A kind rather than a struct stat, because the caller
// (mp_vfs_posix_import_stat) wants only this and the two platforms disagree
// about which stat struct they use.
int femm_wstat_utf8_kind(const char *utf8Path) {
    wchar_t wbuf[1024];
    struct _stat64 st;
    int rc;
    if (femm_widen_path(utf8Path, wbuf, 1024))
        rc = _wstat64(wbuf, &st);
    else
        rc = _stat64(utf8Path, &st);
    if (rc != 0) return 0;
    if (st.st_mode & _S_IFDIR) return 2;
    if (st.st_mode & _S_IFREG) return 1;
    return 0;
}
#endif
