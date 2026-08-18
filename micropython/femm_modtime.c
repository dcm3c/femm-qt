// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker
//
// femm_modtime.c - the port half of the `time` module.
//
// extmod/modtime.c pulls this in through MICROPY_PY_TIME_INCLUDEFILE, which is
// how every port supplies the calls that need a real clock. It is #included
// into that file, not compiled on its own, so everything here is static and the
// file is deliberately outside micropython_embed/ (which regen wipes).
//
// The clock is the local one and the epoch is 1970, so time.time() and
// time.localtime() read the way they do in desktop Python rather than the way
// they do on a microcontroller with a 2000 epoch.
//
// The timezone half of that is not free. Upstream aliases gmtime to localtime
// and converts a given timestamp with timeutils_seconds_since_epoch_to_struct_
// time(), which is pure UTC arithmetic -- exactly right on a microcontroller,
// which has no timezone, and wrong on a desktop, where it made
// time.localtime(t) return UTC and time.gmtime() return local, each the other's
// answer. Anything built on those inherited the error: datetime.now() reported
// UTC wearing a local label, four hours off here. So this file also supplies
// the three host-timezone conversions modtime.c asks for under
// MICROPY_PY_TIME_HOST_TIMEZONE (patches/06-modtime-host-timezone.patch).

#include <string.h>
#include <time.h>

#include "shared/timeutils/timeutils.h"

uint64_t femm_hal_time_ns(void);   // femm_port_hooks.c

// A C broken-out time to MicroPython's. The two disagree about origins in
// three places, which is most of why this helper exists rather than being
// written out at each call site.
static void femm_from_c_tm(const struct tm *lt, timeutils_struct_time_t *tm) {
    tm->tm_year = (uint16_t)(lt->tm_year + 1900);
    tm->tm_mon = (uint8_t)(lt->tm_mon + 1);
    tm->tm_mday = (uint8_t)lt->tm_mday;
    tm->tm_hour = (uint8_t)lt->tm_hour;
    tm->tm_min = (uint8_t)lt->tm_min;
    tm->tm_sec = (uint8_t)lt->tm_sec;
    // C counts weekdays from Sunday, MicroPython from Monday.
    tm->tm_wday = (uint8_t)((lt->tm_wday + 6) % 7);
    tm->tm_yday = (uint16_t)(lt->tm_yday + 1);
}

static void femm_localtime_r(time_t t, struct tm *out) {
    #ifdef _WIN32
    localtime_s(out, &t);
    #else
    localtime_r(&t, out);
    #endif
}

// Current local time, broken out. localtime() (not gmtime) so a script that
// prints a timestamp beside a solve gets the wall clock on the wall.
static void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    struct tm lt;
    femm_localtime_r(time(NULL), &lt);
    femm_from_c_tm(&lt, tm);
}

// The local UTC offset in seconds, measured at the epoch. Only needed for
// timestamps the CRT refuses to convert -- Windows' localtime_s rejects
// everything before 1970 -- where it is the difference between reporting a
// pre-1970 time in local terms and reporting it in UTC while calling it local.
// A single offset is all that is available anyway: the CRT applies today's
// daylight-saving rules to all history, and does not model 1969's.
static long femm_epoch_utc_offset(void) {
    static long off;
    static int known = 0;
    if (!known) {
        struct tm lt;
        memset(&lt, 0, sizeof(lt));
        femm_localtime_r((time_t)0, &lt);
        off = (long)lt.tm_hour * 3600 + (long)lt.tm_min * 60 + lt.tm_sec;
        // West of Greenwich the epoch lands on 1969-12-31, so the hours read
        // as ~19 rather than ~-5.
        if (lt.tm_year == 69) {
            off -= 86400;
        }
        known = 1;
    }
    return off;
}

// Local broken-out time for an arbitrary epoch value -- what time.localtime(t)
// and therefore datetime.fromtimestamp(t) need.
static void mp_time_localtime_epoch(mp_timestamp_t secs, timeutils_struct_time_t *tm) {
    time_t t = (time_t)secs;
    struct tm lt;
    memset(&lt, 0, sizeof(lt));
    #ifdef _WIN32
    if (localtime_s(&lt, &t) != 0) {
    #else
    if (localtime_r(&t, &lt) == NULL) {
    #endif
        // Out of the CRT's range. Shift into local time by hand rather than
        // hand back UTC under a local name: datetime.fromtimestamp(0) probes
        // one day BEFORE the timestamp it was given to detect DST, so the
        // first day of the epoch reaches this path on Windows.
        timeutils_seconds_since_epoch_to_struct_time(secs + femm_epoch_utc_offset(), tm);
        return;
    }
    femm_from_c_tm(&lt, tm);
}

// Whole seconds since the epoch, for gmtime()'s no-argument form.
static mp_timestamp_t mp_time_now_seconds(void) {
    return (mp_timestamp_t)time(NULL);
}

// The inverse of localtime(): a LOCAL broken-out time back to epoch seconds,
// which is what CPython's mktime() means. tm_isdst = -1 leaves it to the CRT to
// work out whether daylight saving was in force, so a local time either side of
// a transition round-trips.
static mp_timestamp_t mp_time_mktime_local(mp_int_t year, mp_int_t mon, mp_int_t mday,
    mp_int_t hour, mp_int_t min, mp_int_t sec) {
    struct tm lt;
    memset(&lt, 0, sizeof(lt));
    lt.tm_year = (int)year - 1900;
    lt.tm_mon = (int)mon - 1;
    lt.tm_mday = (int)mday;
    lt.tm_hour = (int)hour;
    lt.tm_min = (int)min;
    lt.tm_sec = (int)sec;
    lt.tm_isdst = -1;
    time_t t = mktime(&lt);
    if (t == (time_t)-1) {
        // Outside what the CRT can express (pre-1970 on Windows, mostly).
        // timeutils' UTC inverse is the honest fallback -- wrong by the local
        // offset, but monotonic and in range, where -1 is neither.
        return (mp_timestamp_t)timeutils_mktime(year, mon, mday, hour, min, sec);
    }
    return (mp_timestamp_t)t;
}

// Seconds since the epoch, as a float. Bare-metal ports return an integer
// because they have no sub-second clock to offer; this one does, and a script
// timing an analysis wants the fraction. Matches CPython's time.time().
static mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_float((mp_float_t)femm_hal_time_ns() / 1e9);
}
