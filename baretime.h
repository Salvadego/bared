/*
 * baretime.h - Monotonic Instant, calendar DateTime, Duration
 * ==========================================================
 *
 *  USAGE
 *    #define BARETIME_IMPLEMENTATION
 *    #include "baretime.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena, Str)
 *
 *  DESIGN
 *    Instant  - opaque monotonic timestamp. Only meaningful as a
 *               difference. Cannot be printed directly - convert
 *               to DateTime first if you need wall-clock display.
 *
 *    DateTime - calendar representation: year/month/day/hour/...
 *               Carries a UTC offset so it can represent any
 *               timezone.  Not suitable for arithmetic - convert
 *               to Instant first.
 *
 *    Duration - int64_t nanoseconds. Use the NS/US/MS/SEC/MIN/HOUR
 *               constants to build values.
 *
 *  PLATFORM
 *    POSIX (Linux / macOS) - clock_gettime(CLOCK_MONOTONIC)
 *    Windows               - QueryPerformanceCounter
 *    Fallback (C99 only)   - clock()  (CPU time, not wall)
 *
 *  NOTE
 *    Include baretime.h before any header that pulls in <stdlib.h>
 *    so that _POSIX_C_SOURCE is set before glibc locks features.
 *    Easiest pattern: define all _IMPLEMENTATION macros first,
 *    then include baretime.h as your first include.
 *
 *  EXAMPLE
 *
 *    Instant  t0      = instant_now();
 *    ... work ...
 *    Duration elapsed = instant_since(t0);
 *    Str      s       = duration_fmt(arena, elapsed);
 *    printf("took " StrFmt "\n", StrArgs(s));
 *
 *    DateTime now = datetime_local(instant_now());
 *    printf("%04d-%02d-%02d\n", now.year, now.month, now.day);
 *
 *    time_sleep(500 * MS);
 */

/* Feature test macro must fire before ANY system header. */
/* _POSIX_C_SOURCE 200809L: clock_gettime, nanosleep, gmtime_r, localtime_r,
   timegm, and struct tm.tm_gmtoff (via _DEFAULT_SOURCE on glibc).
   Must be defined before any system header is pulled in. */
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__) || \
    defined(__MACH__)
#        if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#                undef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200809L
#        endif
/* _DEFAULT_SOURCE enables timegm() and tm_gmtoff on glibc (Linux).
   On macOS these are always available via BSD heritage. */
#        if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#                define _DEFAULT_SOURCE 1
#        endif
#endif

#ifndef BARETIME_H
#        define BARETIME_H

#        include <stdint.h>

#        include "barestd.h"

/* ================================================================
 *  Duration - signed nanoseconds
 * ================================================================ */

typedef int64_t Duration;

#        define NS   ((Duration)1)
#        define US   ((Duration)1000LL)
#        define MS   ((Duration)1000000LL)
#        define SEC  ((Duration)1000000000LL)
#        define MIN  ((Duration)(60LL * 1000000000LL))
#        define HOUR ((Duration)(3600LL * 1000000000LL))

/* ================================================================
 *  Instant - opaque monotonic point in time
 *  Only use for elapsed-time arithmetic, not calendar display.
 * ================================================================ */

typedef struct {
        int64_t _ns; /* nanoseconds from an arbitrary epoch - treat as opaque */
} Instant;

/* ================================================================
 *  DateTime - calendar representation
 *  Convert from Instant; do not do arithmetic directly on fields.
 * ================================================================ */

typedef struct {
        int32_t  year;           /* e.g. 2025                       */
        uint8_t  month;          /* 1-12                            */
        uint8_t  day;            /* 1-31                            */
        uint8_t  hour;           /* 0-23                            */
        uint8_t  min;            /* 0-59                            */
        uint8_t  sec;            /* 0-60  (leap second)             */
        uint8_t  weekday;        /* 0=Sunday ... 6=Saturday         */
        uint16_t yearday;        /* 1-366                           */
        uint32_t nsec;           /* sub-second nanoseconds          */
        int32_t  utc_offset_sec; /* seconds east of UTC; 0 = UTC    */
} DateTime;

/* ================================================================
 *  Instant API
 * ================================================================ */

/* Current monotonic time. */
Instant instant_now(void);

/* Elapsed time since t. Equivalent to instant_diff(t, instant_now()). */
Duration instant_since(Instant t);

/* Duration between two Instants (b - a). */
Duration instant_diff(Instant a, Instant b);

/* Add a duration to an instant. */
Instant instant_add(Instant t, Duration d);

/* Block calling thread for d nanoseconds. */
void time_sleep(Duration d);

/* ================================================================
 *  DateTime API
 * ================================================================ */

/* Convert Instant to local-timezone DateTime.
   Uses the system's local timezone (same as localtime_r on POSIX). */
DateTime datetime_local(Instant t);

/* Convert Instant to UTC DateTime. */
DateTime datetime_utc(Instant t);

/* Convert DateTime back to an Instant.
   utc_offset_sec in the DateTime is used for the conversion. */
Instant instant_from_datetime(DateTime dt);

/* ================================================================
 *  Formatting
 * ================================================================ */

/* "1h30m0.500s"  "250ms"  "42us"  "7ns"  "-1.500s" */
Str duration_fmt(Arena* a, Duration d);

/* "2025-03-21 14:32:00.000000000 +0000" */
Str datetime_fmt(Arena* a, DateTime dt);

/* "2025-03-21" */
Str datetime_fmt_date(Arena* a, DateTime dt);

/* "14:32:00" */
Str datetime_fmt_time(Arena* a, DateTime dt);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BARETIME_IMPLEMENTATION

#                include <stdio.h>
#                include <string.h>

/* ----------------------------------------------------------------
 *  Platform: clock
 * ---------------------------------------------------------------- */

#                if defined(_WIN32) || defined(_WIN64)

#                        include <windows.h>

static int64_t _instant_ns(void) {
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);
        /* scale ticks -> nanoseconds */
        return (int64_t)((double)counter.QuadPart / (double)freq.QuadPart *
                         1e9);
}

void time_sleep(Duration d) {
        if (d <= 0) return;
        Sleep((DWORD)(d / MS));
}

#                elif defined(__unix__) || defined(__linux__) || \
                    defined(__APPLE__) || defined(__MACH__)

#                        include <time.h>

static int64_t _instant_ns(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * SEC + (int64_t)ts.tv_nsec;
}

void time_sleep(Duration d) {
        if (d <= 0) return;
        struct timespec ts;
        ts.tv_sec  = (time_t)(d / SEC);
        ts.tv_nsec = (long)(d % SEC);
        nanosleep(&ts, NULL);
}

#                else /* C99 fallback */

#                        include <time.h>

static int64_t _instant_ns(void) {
        return (int64_t)((double)clock() / (double)CLOCKS_PER_SEC * 1e9);
}

void time_sleep(Duration d) {
        if (d <= 0) return;
        Instant start = instant_now();
        while (instant_since(start) < d) { /* busy wait */
        }
}

#                endif

/* ----------------------------------------------------------------
 *  Instant
 * ---------------------------------------------------------------- */

Instant instant_now(void) {
        Instant t;
        t._ns = _instant_ns();
        return t;
}

Duration instant_diff(Instant a, Instant b) {
        return b._ns - a._ns;
}

Duration instant_since(Instant t) {
        return instant_diff(t, instant_now());
}

Instant instant_add(Instant t, Duration d) {
        Instant out;
        out._ns = t._ns + d;
        return out;
}

/* ----------------------------------------------------------------
 *  DateTime conversion
 *  We use time_t / gmtime_r / localtime_r on POSIX and Windows
 *  equivalents.  Instant is monotonic and has no fixed epoch, so
 *  we anchor it to real wall time by combining gettimeofday/
 *  GetSystemTime with the monotonic instant delta.
 * ---------------------------------------------------------------- */

#                if defined(_WIN32) || defined(_WIN64)

#                        include <windows.h>

static time_t _wall_time_t(void) {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER ull;
        ull.LowPart  = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;
        /* Windows epoch: Jan 1 1601; Unix epoch: Jan 1 1970 */
        return (time_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

#                else

#                        include <sys/time.h>
#                        include <time.h>

static time_t _wall_time_t(void) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (time_t)tv.tv_sec;
}

#                endif

static DateTime _tm_to_datetime(struct tm* t, uint32_t nsec, int32_t utc_off) {
        DateTime dt;
        memset(&dt, 0, sizeof dt);
        dt.year           = (int32_t)(t->tm_year + 1900);
        dt.month          = (uint8_t)(t->tm_mon + 1);
        dt.day            = (uint8_t)t->tm_mday;
        dt.hour           = (uint8_t)t->tm_hour;
        dt.min            = (uint8_t)t->tm_min;
        dt.sec            = (uint8_t)t->tm_sec;
        dt.weekday        = (uint8_t)t->tm_wday;
        dt.yearday        = (uint16_t)(t->tm_yday + 1);
        dt.nsec           = nsec;
        dt.utc_offset_sec = utc_off;
        return dt;
}

DateTime datetime_utc(Instant t) {
        /* Anchor: current wall clock minus elapsed since then */
        Instant  now_mono = instant_now();
        int64_t  delta_ns = t._ns - now_mono._ns;
        time_t   wall     = _wall_time_t();
        time_t   target   = wall + (time_t)(delta_ns / SEC);
        uint32_t nsec = (delta_ns % SEC < 0) ? 0 : (uint32_t)(delta_ns % SEC);

        struct tm out;
#                if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&out, &target);
#                else
        gmtime_r(&target, &out);
#                endif
        return _tm_to_datetime(&out, nsec, 0);
}

DateTime datetime_local(Instant t) {
        Instant  now_mono = instant_now();
        int64_t  delta_ns = t._ns - now_mono._ns;
        time_t   wall     = _wall_time_t();
        time_t   target   = wall + (time_t)(delta_ns / SEC);
        uint32_t nsec = (delta_ns % SEC < 0) ? 0 : (uint32_t)(delta_ns % SEC);

        struct tm out;
#                if defined(_WIN32) || defined(_WIN64)
        localtime_s(&out, &target);
        int32_t utc_off = 0; /* TODO: _get_timezone on MSVC */
#                else
        localtime_r(&target, &out);
        /* tm_gmtoff is a BSD/GNU extension (available via _DEFAULT_SOURCE on
           Linux, always on macOS).  Fall back to 0 if not present. */
#                        if defined(__USE_MISC) || defined(__APPLE__) || \
                            defined(__MACH__)
        int32_t utc_off = (int32_t)out.tm_gmtoff;
#                        else
        int32_t utc_off = 0;
#                        endif
#                endif
        return _tm_to_datetime(&out, nsec, utc_off);
}

Instant instant_from_datetime(DateTime dt) {
        struct tm t;
        memset(&t, 0, sizeof t);
        t.tm_year = (int)(dt.year - 1900);
        t.tm_mon  = (int)(dt.month - 1);
        t.tm_mday = (int)dt.day;
        t.tm_hour = (int)dt.hour;
        t.tm_min  = (int)dt.min;
        t.tm_sec  = (int)dt.sec;

#                if defined(_WIN32) || defined(_WIN64)
        time_t wall = _mkgmtime(&t) - (time_t)dt.utc_offset_sec;
#                else
        /* timegm() converts UTC struct tm -> time_t.
           Available via _DEFAULT_SOURCE on Linux, always on macOS/BSD. */
#                        if defined(__USE_MISC) || defined(__APPLE__) || \
                            defined(__MACH__)
        time_t wall = timegm(&t) - (time_t)dt.utc_offset_sec;
#                        else
        /* Fallback: use mktime (local) then adjust by local UTC offset */
        time_t wall = mktime(&t) - (time_t)dt.utc_offset_sec;
#                        endif
#                endif

        Instant now_mono = instant_now();
        time_t  now_wall = _wall_time_t();
        int64_t delta    = (int64_t)(wall - now_wall) * SEC + (int64_t)dt.nsec;

        Instant out;
        out._ns = now_mono._ns + delta;
        return out;
}

/* ----------------------------------------------------------------
 *  Formatting
 * ---------------------------------------------------------------- */

Str duration_fmt(Arena* a, Duration d) {
        char buf[64];
        int  n   = 0;
        bool neg = (d < 0);
        if (neg) d = -d;
        if (neg) buf[n++] = '-';

        if (d >= HOUR) {
                n += snprintf(buf + n,
                              sizeof(buf) - (size_t)n,
                              "%lldh",
                              (long long)(d / HOUR));
                d %= HOUR;
        }
        if (d >= MIN) {
                n += snprintf(buf + n,
                              sizeof(buf) - (size_t)n,
                              "%lldm",
                              (long long)(d / MIN));
                d %= MIN;
        }
        if (d >= SEC) {
                n += snprintf(buf + n,
                              sizeof(buf) - (size_t)n,
                              "%lld.%03llds",
                              (long long)(d / SEC),
                              (long long)((d % SEC) / MS));
        } else if (d >= MS) {
                n += snprintf(buf + n,
                              sizeof(buf) - (size_t)n,
                              "%lldms",
                              (long long)(d / MS));
        } else if (d >= US) {
                n += snprintf(buf + n,
                              sizeof(buf) - (size_t)n,
                              "%lldus",
                              (long long)(d / US));
        } else {
                n += snprintf(
                    buf + n, sizeof(buf) - (size_t)n, "%lldns", (long long)d);
        }

        char* out = arena_push_array(a, char, (size_t)n + 1);
        memcpy(out, buf, (size_t)n + 1);
        return str_buf(out, (size_t)n);
}

Str datetime_fmt(Arena* a, DateTime dt) {
        char  buf[64];
        int   off_h = (int)(dt.utc_offset_sec / 3600);
        int   off_m = (int)((dt.utc_offset_sec % 3600) / 60);
        int   n     = snprintf(buf,
                               sizeof buf,
                               "%04d-%02d-%02d %02d:%02d:%02d.%09u %+03d%02d",
                               (int)dt.year,
                               (int)dt.month,
                               (int)dt.day,
                               (int)dt.hour,
                               (int)dt.min,
                               (int)dt.sec,
                               (unsigned)dt.nsec,
                               off_h,
                               (off_m < 0 ? -off_m : off_m));
        char* out   = arena_push_array(a, char, (size_t)n + 1);
        memcpy(out, buf, (size_t)n + 1);
        return str_buf(out, (size_t)n);
}

Str datetime_fmt_date(Arena* a, DateTime dt) {
        char  buf[16];
        int   n   = snprintf(buf,
                             sizeof buf,
                             "%04d-%02d-%02d",
                             (int)dt.year,
                             (int)dt.month,
                             (int)dt.day);
        char* out = arena_push_array(a, char, (size_t)n + 1);
        memcpy(out, buf, (size_t)n + 1);
        return str_buf(out, (size_t)n);
}

Str datetime_fmt_time(Arena* a, DateTime dt) {
        char  buf[16];
        int   n   = snprintf(buf,
                             sizeof buf,
                             "%02d:%02d:%02d",
                             (int)dt.hour,
                             (int)dt.min,
                             (int)dt.sec);
        char* out = arena_push_array(a, char, (size_t)n + 1);
        memcpy(out, buf, (size_t)n + 1);
        return str_buf(out, (size_t)n);
}

#        endif /* BARETIME_IMPLEMENTATION */
#endif         /* BARETIME_H */
