/*
 * ds3231_time_bridge.c
 *
 * Notes:
 * - This file defines time(), gettimeofday(), and settimeofday() so that
 *   standard library time calls use the DS3231.
 * - We assume RTC stores UTC. If you want local time, convert externally.
 * - Microseconds are returned as 0 (DS3231 has 1s granularity).
 *
 * All comments are in English.
 */

#include "ds3231_time_bridge.h"
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>

#ifndef WEAK
#if defined(__GNUC__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif
#endif

/* Keep a pointer to the attached DS3231 instance. */
static DS3231 *s_rtc = NULL;

/* ===== Date helpers (UTC, no timezones) ===== */
static bool is_leap(int y) {
    /* y is full year, e.g., 2000, 2024 */
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int days_in_month(int y, int m0) {
    /* m0: 0..11 */
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m0 == 1) return dim[1] + (is_leap(y) ? 1 : 0);
    return dim[m0];
}

/* Convert broken-down UTC (struct tm) to UNIX epoch seconds. */
static int tm_to_epoch_utc(const struct tm *t, time_t *out) {
    if (!t || !out) return -1;
    int y = t->tm_year + 1900;
    int m = t->tm_mon; /* 0..11 */
    int d = t->tm_mday; /* 1..31 */
    int hh = t->tm_hour, mm = t->tm_min, ss = t->tm_sec;

    if (y < 1970 || y > 2099) return -2;
    if (m < 0 || m > 11) return -3;
    if (d < 1 || d > 31) return -4;

    long long days = 0;
    /* Years */
    for (int year = 1970; year < y; ++year) {
        days += is_leap(year) ? 366 : 365;
    }
    /* Months */
    for (int month = 0; month < m; ++month) {
        days += days_in_month(y, month);
    }
    /* Days */
    days += (d - 1);

    long long secs = days * 86400LL + (long long)hh * 3600 + (long long)mm * 60 + (long long)ss;
    *out = (time_t)secs;
    return 0;
}

/* Convert UNIX epoch seconds to UTC broken-down time (struct tm). */
static int epoch_to_tm_utc(time_t epoch, struct tm *out) {
    if (!out) return -1;
    long long secs = (long long)epoch;
    if (secs < 0) return -2;

    long long days = secs / 86400LL;
    int rem = (int)(secs % 86400LL);
    int hh = rem / 3600; rem %= 3600;
    int mm = rem / 60;
    int ss = rem % 60;

    int y = 1970;
    while (1) {
        int diy = is_leap(y) ? 366 : 365;
        if (days >= diy) { days -= diy; ++y; }
        else break;
        if (y > 2099) return -3;
    }

    int m = 0;
    while (m < 12) {
        int dim = days_in_month(y, m);
        if (days >= dim) { days -= dim; ++m; }
        else break;
    }

    int d = (int)days + 1;

    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon  = m;
    out->tm_mday = d;
    out->tm_hour = hh;
    out->tm_min  = mm;
    out->tm_sec  = ss;

    /* Compute weekday (0=Sun) */
    /* 1970-01-01 was a Thursday (4) */
    long long total_days = (long long)(epoch / 86400LL);
    int wday = (int)((total_days + 4) % 7);
    if (wday < 0) wday += 7;
    out->tm_wday = wday;

    return 0;
}

/* ===== Public attach + manual accessors ===== */

void ds3231_time_bridge_attach(DS3231 *rtc) {
    s_rtc = rtc;
}

int ds3231_time_get_epoch(time_t *out_epoch) {
    if (!s_rtc || !out_epoch) return -1;
    struct tm t;
    int r = ds3231_get_time(s_rtc, &t);
    if (r != 0) return r;
    return tm_to_epoch_utc(&t, out_epoch);
}

int ds3231_time_set_epoch(time_t epoch) {
    if (!s_rtc) return -1;
    struct tm t;
    int r = epoch_to_tm_utc(epoch, &t);
    if (r != 0) return r;
    return ds3231_set_time(s_rtc, &t);
}

/* ===== Overrides ===== */

/* Provide time(). Many embedded libcs call time() inside mktime, so keep it simple. */
WEAK time_t time(time_t *tloc) {
    time_t now = 0;
    if (s_rtc) {
        if (ds3231_time_get_epoch(&now) != 0) {
            now = (time_t)0;
        }
    } else {
        /* If not attached, return 0 to indicate unknown time. */
        now = (time_t)0;
    }
    if (tloc) *tloc = now;
    return now;
}

/* gettimeofday() override (UTC). Returns microseconds=0. */
WEAK int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) return -1;
    time_t now = time(NULL);
    tv->tv_sec = now;
    tv->tv_usec = 0;
    return 0;
}

/* settimeofday() -> write back to DS3231 (UTC). */
WEAK int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    (void)tz;
    if (!s_rtc || !tv) return -1;
    return ds3231_time_set_epoch(tv->tv_sec);
}

/* ===== Optional sync helpers ===== */

int ds3231_sync_system_from_rtc(void) {
    if (!s_rtc) return -1;
    time_t now = 0;
    int r = ds3231_time_get_epoch(&now);
    if (r != 0) return r;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    return settimeofday(&tv, NULL);
}

int ds3231_sync_rtc_from_system(void) {
    if (!s_rtc) return -1;
    time_t now = time(NULL);
    return ds3231_time_set_epoch(now);
}
