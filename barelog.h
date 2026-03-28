/*
 * barelog.h -- Structured logging, zero-alloc hot path
 * ======================================================
 *
 *  USAGE
 *    #define BARELOG_IMPLEMENTATION
 *    #include "barelog.h"
 *
 *  DEPENDS ON
 *    barestd.h
 *    baretime.h  (include first — provides timestamping)
 *    bareio.h    (optional — if included, log_set_sink_writer() available)
 *
 *  DESIGN
 *    Four levels: LOG_DEBUG < LOG_INFO < LOG_WARN < LOG_ERROR
 *    Messages below the active level are discarded with a single comparison.
 *    Hot path formats into a static 1 KB buffer — zero heap, zero arena.
 *    Thread-safe mode: wrap the write with a Mutex (opt-in via log_set_mutex).
 *
 *    SINKS
 *      log_set_sink_file(FILE*)     — write to any FILE* (default: stderr)
 *      log_set_sink_writer(Writer*) — write to any bareio Writer (requires
 * bareio.h) log_set_sink_fn(fn)          — custom callback
 *
 *    STRUCTURED FIELDS
 *      log_with(key, val)  — attach a Str key-value pair to the NEXT log call
 *      Fields appear as  key=val  after the message.
 *      Up to LOG_MAX_FIELDS fields buffered per call (default: 8).
 *
 *  FORMAT
 *    2025-03-21 14:32:00 INFO  [tag] message key=val key2=val2
 *
 *  EXAMPLE
 *    log_set_level(LOG_INFO);
 *    log_info("startup", "listening on :%d", port);
 *    log_with("user",  str_lit("alice"));
 *    log_with("reqid", str_lit("abc123"));
 *    log_info("http", "GET /api/users");   // user=alice reqid=abc123
 *    log_error("db", "query failed: %s", err);
 */
#ifndef BARELOG_H
#define BARELOG_H

#include <stdarg.h>
#include <stdio.h>

#include "barestd.h"

/* baretime.h must come first */
#ifndef BARETIME_H
#        error "barelog.h requires baretime.h to be included before it"
#endif

/* ================================================================
 *  Level
 * ================================================================ */
typedef enum {
        LOG_DEBUG = 0,
        LOG_INFO  = 1,
        LOG_WARN  = 2,
        LOG_ERROR = 3,
        LOG_NONE  = 4,
} LogLevel;

/* ================================================================
 *  Structured field (key=value pair attached to next call)
 * ================================================================ */
#ifndef LOG_MAX_FIELDS
#        define LOG_MAX_FIELDS 8
#endif

/* ================================================================
 *  Sink callback type
 * ================================================================ */
typedef void (*LogSinkFn)(LogLevel    lvl,
                          const char* tag,
                          const char* msg,
                          size_t      msg_len);

/* ================================================================
 *  Configuration
 * ================================================================ */
void log_set_level(LogLevel lvl);
void log_set_sink_file(FILE* f);
void log_set_sink_fn(LogSinkFn fn);

#ifdef BAREIO_H
void log_set_sink_writer(Writer* w);
#endif

#ifdef BARESYNC_H
/* Wrap log writes with this mutex for thread-safe logging. */
void log_set_mutex(Mutex* m);
#endif

/* ================================================================
 *  Structured fields — attached to the NEXT log_* call, then cleared
 * ================================================================ */
void log_with(const char* key, Str value);
void log_with_int(const char* key, int64_t value);

/* ================================================================
 *  Logging macros
 * ================================================================ */
#define log_debug(tag, ...) _log_write(LOG_DEBUG, (tag), __VA_ARGS__)
#define log_info(tag, ...)  _log_write(LOG_INFO, (tag), __VA_ARGS__)
#define log_warn(tag, ...)  _log_write(LOG_WARN, (tag), __VA_ARGS__)
#define log_error(tag, ...) _log_write(LOG_ERROR, (tag), __VA_ARGS__)

/* Internal */
void _log_write(LogLevel lvl, const char* tag, const char* fmt, ...);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARELOG_IMPLEMENTATION
#        include <stdio.h>
#        include <string.h>

static LogLevel  _log_level   = LOG_INFO;
static FILE*     _log_file    = NULL; /* NULL = stderr */
static LogSinkFn _log_sink_fn = NULL;

#        ifdef BAREIO_H
static Writer* _log_writer = NULL;
#        endif

#        ifdef BARESYNC_H
static Mutex* _log_mutex = NULL;
void          log_set_mutex(Mutex* m) {
        _log_mutex = m;
}
#        endif

void log_set_level(LogLevel lvl) {
        _log_level = lvl;
}
void log_set_sink_file(FILE* f) {
        _log_file    = f;
        _log_sink_fn = NULL;
#        ifdef BAREIO_H
        _log_writer = NULL;
#        endif
}
void log_set_sink_fn(LogSinkFn fn) {
        _log_sink_fn = fn;
        _log_file    = NULL;
#        ifdef BAREIO_H
        _log_writer = NULL;
#        endif
}
#        ifdef BAREIO_H
void log_set_sink_writer(Writer* w) {
        _log_writer  = w;
        _log_file    = NULL;
        _log_sink_fn = NULL;
}
#        endif

/* Structured fields */
typedef struct {
        const char* key;
        Str         val;
} _LogField;
static _LogField _log_fields[LOG_MAX_FIELDS];
static int       _log_nfields = 0;

void log_with(const char* key, Str value) {
        if (_log_nfields < LOG_MAX_FIELDS) {
                _log_fields[_log_nfields].key = key;
                _log_fields[_log_nfields].val = value;
                _log_nfields++;
        }
}

void log_with_int(const char* key, int64_t value) {
        /* Format int into a static buffer per field slot */
        static char _int_bufs[LOG_MAX_FIELDS][24];
        if (_log_nfields < LOG_MAX_FIELDS) {
                snprintf(_int_bufs[_log_nfields],
                         sizeof _int_bufs[0],
                         "%lld",
                         (long long)value);
                _log_fields[_log_nfields].key = key;
                _log_fields[_log_nfields].val =
                    str_from_c(_int_bufs[_log_nfields]);
                _log_nfields++;
        }
}

static const char* _log_level_str(LogLevel lvl) {
        switch (lvl) {
                case LOG_DEBUG:
                        return "DEBUG";
                case LOG_INFO:
                        return "INFO ";
                case LOG_WARN:
                        return "WARN ";
                case LOG_ERROR:
                        return "ERROR";
                default:
                        return "?    ";
        }
}

/* Static 1 KB format buffer — zero heap on hot path */
static char _log_buf[1024];

void _log_write(LogLevel lvl, const char* tag, const char* fmt, ...) {
        if (lvl < _log_level) {
                _log_nfields = 0;
                return;
        }

        /* Timestamp */
        Instant  now = instant_now();
        DateTime dt  = datetime_utc(now);

        int pos = snprintf(_log_buf,
                           sizeof _log_buf,
                           "%04d-%02d-%02d %02d:%02d:%02d %s [%s] ",
                           (int)dt.year,
                           (int)dt.month,
                           (int)dt.day,
                           (int)dt.hour,
                           (int)dt.min,
                           (int)dt.sec,
                           _log_level_str(lvl),
                           tag ? tag : "");
        if (pos < 0) pos = 0;

        /* Message */
        if ((size_t)pos < sizeof _log_buf - 2) {
                va_list ap;
                va_start(ap, fmt);
                int msg = vsnprintf(
                    _log_buf + pos, sizeof _log_buf - (size_t)pos - 1, fmt, ap);
                va_end(ap);
                if (msg > 0) pos += msg;
                if ((size_t)pos >= sizeof _log_buf - 1)
                        pos = (int)(sizeof _log_buf - 2);
        }

        /* Structured fields: key=val */
        int i;
        for (i = 0; i < _log_nfields && (size_t)pos < sizeof _log_buf - 4;
             i++) {
                int wrote = snprintf(_log_buf + pos,
                                     sizeof _log_buf - (size_t)pos - 1,
                                     " %s=" StrFmt,
                                     _log_fields[i].key,
                                     (int)_log_fields[i].val.len,
                                     _log_fields[i].val.ptr);
                if (wrote > 0) pos += wrote;
                if ((size_t)pos >= sizeof _log_buf - 1)
                        pos = (int)(sizeof _log_buf - 2);
        }
        _log_nfields = 0; /* clear fields after use */

        _log_buf[pos]     = '\n';
        _log_buf[pos + 1] = '\0';
        pos++;

#        ifdef BARESYNC_H
        if (_log_mutex) mutex_lock(_log_mutex);
#        endif

        if (_log_sink_fn) {
                _log_sink_fn(lvl, tag, _log_buf, (size_t)pos);
        }
#        ifdef BAREIO_H
        else if (_log_writer) {
                io_write(_log_writer, _log_buf, (size_t)pos);
                if (lvl >= LOG_ERROR) {
                        /* attempt flush if it's a BufWriter */
                }
        }
#        endif
        else {
                FILE* out = _log_file ? _log_file : stderr;
                fwrite(_log_buf, 1, (size_t)pos, out);
                if (lvl >= LOG_ERROR) fflush(out);
        }

#        ifdef BARESYNC_H
        if (_log_mutex) mutex_unlock(_log_mutex);
#        endif
}

#endif /* BARELOG_IMPLEMENTATION */
#endif /* BARELOG_H */
