/*
 * ds3231_time_bridge.c
 *
 * Notes:
 * - This module assumes DS3231 keeps UTC.
 * - DS3231 has 1-second granularity, sub-second parts are ignored.
 */

#include "ds3231_time_bridge.h"
#include <stdbool.h>
#include <string.h>

#if DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS
#include <sys/time.h>
#endif

/* Keep a pointer to the attached DS3231 instance. */
static DS3231_t *s_rtc = NULL;

/* ===== Date helpers (UTC, no timezones) ===== */

static bool ds3231_time_is_leap(int y)
{
    return ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
}

static int ds3231_time_days_in_month(int y, int m0)
{
    /* m0: 0..11 */
    static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (m0 == 1)
    {
        return dim[1] + (ds3231_time_is_leap(y) ? 1 : 0);
    }

    return dim[m0];
}

static int ds3231_time_tm_to_epoch_utc(const struct tm *t, time_t *out)
{
    if (t == NULL || out == NULL)
    {
        return -1;
    }

    int y = t->tm_year + 1900;
    int m = t->tm_mon;   /* 0..11 */
    int d = t->tm_mday;  /* 1..31 */
    int hh = t->tm_hour;
    int mm = t->tm_min;
    int ss = t->tm_sec;

    if (y < 1970 || y > 2099)
    {
        return -2;
    }
    if (m < 0 || m > 11)
    {
        return -3;
    }
    if (d < 1 || d > 31)
    {
        return -4;
    }
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
    {
        return -5;
    }

    int dim = ds3231_time_days_in_month(y, m);
    if (d > dim)
    {
        return -6;
    }

    long long days = 0;

    for (int year = 1970; year < y; ++year)
    {
        days += ds3231_time_is_leap(year) ? 366 : 365;
    }

    for (int month = 0; month < m; ++month)
    {
        days += ds3231_time_days_in_month(y, month);
    }

    days += (long long)(d - 1);

    long long secs = days * 86400LL + (long long)hh * 3600LL + (long long)mm * 60LL + (long long)ss;

    /* Ensure the value fits time_t on this toolchain. */
    time_t tt = (time_t)secs;
    if ((long long)tt != secs)
    {
        return -7;
    }

    *out = tt;
    return 0;
}

static int ds3231_time_epoch_to_tm_utc(time_t epoch, struct tm *out)
{
    if (out == NULL)
    {
        return -1;
    }

    long long secs = (long long)epoch;
    if (secs < 0)
    {
        return -2;
    }

    long long days = secs / 86400LL;
    int rem = (int)(secs % 86400LL);

    int hh = rem / 3600;
    rem %= 3600;
    int mm = rem / 60;
    int ss = rem % 60;

    int y = 1970;
    while (1)
    {
        int diy = ds3231_time_is_leap(y) ? 366 : 365;
        if (days >= diy)
        {
            days -= diy;
            ++y;
        }
        else
        {
            break;
        }
        if (y > 2099)
        {
            return -3;
        }
    }

    int m = 0;
    while (m < 12)
    {
        int dim = ds3231_time_days_in_month(y, m);
        if (days >= dim)
        {
            days -= dim;
            ++m;
        }
        else
        {
            break;
        }
    }

    int d = (int)days + 1;

    memset(out, 0, sizeof(*out));
    out->tm_year = y - 1900;
    out->tm_mon = m;
    out->tm_mday = d;
    out->tm_hour = hh;
    out->tm_min = mm;
    out->tm_sec = ss;

    /* Compute weekday (0=Sunday). 1970-01-01 was a Thursday (4). */
    long long total_days = secs / 86400LL;
    int wday = (int)((total_days + 4) % 7);
    if (wday < 0)
    {
        wday += 7;
    }
    out->tm_wday = wday;

    return 0;
}

/* ===== Public API ===== */

void ds3231_time_bridge_attach(DS3231_t *rtc)
{
    s_rtc = rtc;
}

int ds3231_time_get_epoch(time_t *out_epoch)
{
    if (s_rtc == NULL || out_epoch == NULL)
    {
        return -1;
    }

    struct tm t;
    int r = ds3231_get_time(s_rtc, &t);
    if (r != 0)
    {
        return r;
    }

    return ds3231_time_tm_to_epoch_utc(&t, out_epoch);
}

int ds3231_time_set_epoch(time_t epoch)
{
    if (s_rtc == NULL)
    {
        return -1;
    }

    struct tm t;
    int r = ds3231_time_epoch_to_tm_utc(epoch, &t);
    if (r != 0)
    {
        return r;
    }

    /* DS3231 calendar starts at year 2000. */
    if (t.tm_year < 100 || t.tm_year > 199)
    {
        return -4;
    }

    return ds3231_set_time(s_rtc, &t);
}

#if DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS

#if !defined(DS3231_TIME_BRIDGE_SYSCALL_ATTR)
#if defined(__GNUC__)
#define DS3231_TIME_BRIDGE_SYSCALL_ATTR __attribute__((weak))
#else
#define DS3231_TIME_BRIDGE_SYSCALL_ATTR
#endif
#endif

#if defined(__has_include)
#  if __has_include(<sys/reent.h>)
#    include <sys/reent.h>
#  elif __has_include(<reent.h>)
#    include <reent.h>
#  else
     struct _reent;
#  endif
#else
struct _reent;
#endif

/* newlib syscall backend for gettimeofday(). */
DS3231_TIME_BRIDGE_SYSCALL_ATTR int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;

    if (tv == NULL)
    {
        return -1;
    }

    time_t now = 0;
    if (ds3231_time_get_epoch(&now) != 0)
    {
        return -1;
    }

    tv->tv_sec = now;
    tv->tv_usec = 0;
    return 0;
}

/* newlib reentrant syscall backend for gettimeofday(). */
DS3231_TIME_BRIDGE_SYSCALL_ATTR int _gettimeofday_r(struct _reent *r, struct timeval *tv, void *tz)
{
    (void)r;
    return _gettimeofday(tv, tz);
}

/* newlib syscall backend for settimeofday(). */
DS3231_TIME_BRIDGE_SYSCALL_ATTR int _settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tz;

    if (tv == NULL)
    {
        return -1;
    }

    return ds3231_time_set_epoch(tv->tv_sec);
}

/* newlib reentrant syscall backend for settimeofday(). */
DS3231_TIME_BRIDGE_SYSCALL_ATTR int _settimeofday_r(struct _reent *r, const struct timeval *tv, const struct timezone *tz)
{
    (void)r;
    return _settimeofday(tv, tz);
}

#endif /* DS3231_TIME_BRIDGE_PROVIDE_SYSCALLS */
