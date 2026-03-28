/*
 * barehttp.h -- Go-inspired HTTP/1.1 server
 * ==========================================
 *
 *  USAGE
 *    #define BAREHTTP_IMPLEMENTATION
 *    #include "barehttp.h"
 *
 *  DEPENDS ON
 *    barestd.h, barenet.h, barethreads.h, baresync.h, baretime.h
 *
 *  DESIGN
 *    Modelled closely on Go's net/http package.
 *
 *    Two handler styles are supported:
 *
 *      1. Go-style:
 *           void my_handler(HttpResponseWriter *w, HttpRequest *req)
 *
 *      2. Context style (arena + user data):
 *           void my_handler(HttpResponseWriter *w, HttpRequest *req, HttpCtx
 * *ctx)
 *
 *    Routing is a flat slice of (pattern, handler) pairs matched in
 *    insertion order. Exact matches beat prefix matches.  "/" is the
 *    catch-all fallback, exactly like Go's default mux.
 *
 *    Threading model: thread-per-connection (like Go's goroutine-per-conn).
 *    Each accepted connection gets its own OS thread.  The thread uses a
 *    fresh scratch arena (HTTP_CONN_ARENA_SIZE bytes) freed on disconnect.
 *
 *    A companion ThreadPool variant (http_serve_pool) accepts connections
 *    on a shared queue serviced by N worker threads, useful when you want
 *    to cap parallelism.
 *
 *  EXAMPLE -- Go style
 *
 *    void hello(HttpResponseWriter *w, HttpRequest *req) {
 *        http_set_header(w, str_lit("Content-Type"), str_lit("text/plain"));
 *        http_write_str(w, str_lit("Hello, World!\n"));
 *    }
 *
 *    int main(void) {
 *        Arena *a = arena_new(MB(4));
 *        HttpMux *mux = http_mux_new(a);
 *        http_handle(mux, str_lit("/"), hello);
 *        http_handle(mux, str_lit("/hello"), hello);
 *        http_serve(mux, str_lit("0.0.0.0"), 8080, a);
 *    }
 *
 *  EXAMPLE -- Context style
 *
 *    typedef struct { DB *db; Logger *log; } AppCtx;
 *
 *    void get_user(HttpResponseWriter *w, HttpRequest *req, HttpCtx *ctx) {
 *        AppCtx *app = (AppCtx *)ctx->user;
 *        Str id = http_query(req, str_lit("id"));
 *        http_write_fmt(w, ctx->arena, "{\"id\": \"" StrFmt "\"}",
 * StrArgs(id));
 *    }
 *
 *    int main(void) {
 *        Arena *a = arena_new(MB(4));
 *        AppCtx app = { .db = db_open(), .log = logger_new() };
 *        HttpMux *mux = http_mux_new(a);
 *        http_handle_ctx(mux, str_lit("/user"), get_user, &app);
 *        http_serve(mux, str_lit("0.0.0.0"), 8080, a);
 *    }
 *
 *  THREAD POOL VARIANT
 *
 *    http_serve_pool(mux, str_lit("0.0.0.0"), 8080, 8, a);
 *    //                                              ^ 8 worker threads
 */

/* Feature test macros must appear before any system header. */
#if !defined(_WIN32) && !defined(_WIN64)
#        if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#                undef _POSIX_C_SOURCE
#                define _POSIX_C_SOURCE 200809L
#        endif
#        if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#                define _DEFAULT_SOURCE 1
#        endif
#endif

#ifndef BAREHTTP_H
#        define BAREHTTP_H

#        include <stdarg.h>
#        include <stdbool.h>
#        include <stddef.h>
#        include <stdint.h>

#        include "barenet.h"
#        include "barestd.h"
#        include "baresync.h"
#        include "barethreads.h"

/* ================================================================
 *  Configuration
 * ================================================================ */

#        ifndef HTTP_CONN_ARENA_SIZE
#                define HTTP_CONN_ARENA_SIZE \
                        (KB(256)) /* per-connection scratch arena    */
#        endif
#        ifndef HTTP_MAX_HEADERS
#                define HTTP_MAX_HEADERS \
                        64 /* request headers cap             */
#        endif
#        ifndef HTTP_MAX_ROUTES
#                define HTTP_MAX_ROUTES \
                        256 /* mux route table cap             */
#        endif
#        ifndef HTTP_READ_BUF
#                define HTTP_READ_BUF \
                        (KB(8)) /* request read buffer             */
#        endif
#        ifndef HTTP_WRITE_BUF
#                define HTTP_WRITE_BUF \
                        (KB(16)) /* response write buffer           */
#        endif
#        ifndef HTTP_POOL_QUEUE_CAP
#                define HTTP_POOL_QUEUE_CAP \
                        1024 /* thread pool connection queue    */
#        endif

/* ================================================================
 *  HTTP status codes (most common subset)
 * ================================================================ */

#        define HTTP_200 200
#        define HTTP_201 201
#        define HTTP_204 204
#        define HTTP_301 301
#        define HTTP_302 302
#        define HTTP_304 304
#        define HTTP_400 400
#        define HTTP_401 401
#        define HTTP_403 403
#        define HTTP_404 404
#        define HTTP_405 405
#        define HTTP_408 408
#        define HTTP_409 409
#        define HTTP_422 422
#        define HTTP_429 429
#        define HTTP_500 500
#        define HTTP_501 501
#        define HTTP_503 503

/* ================================================================
 *  HttpHeader -- single key/value pair
 * ================================================================ */

typedef struct {
        Str key;
        Str value;
} HttpHeader;

/* ================================================================
 *  HttpRequest -- parsed incoming request (arena-owned strings)
 * ================================================================ */

typedef struct {
        Str        method;                    /* "GET", "POST", ...           */
        Str        path;                      /* "/users/42"                  */
        Str        query_string;              /* "id=42&fmt=json"             */
        Str        version;                   /* "HTTP/1.1"                   */
        Str        body;                      /* request body (may be empty)  */
        HttpHeader headers[HTTP_MAX_HEADERS]; /* parsed headers               */
        size_t     num_headers;
        bool       keep_alive;
        Str _matched_pattern; /* populated by mux before calling handler */
} HttpRequest;

/* ================================================================
 *  HttpResponseWriter -- write interface for handlers
 *  Similar to Go's http.ResponseWriter.
 * ================================================================ */

typedef struct {
        Sock       sock;
        int        status; /* default 200                  */
        HttpHeader headers[HTTP_MAX_HEADERS];
        size_t     num_headers;
        bool       header_sent;
        uint8_t    wbuf[HTTP_WRITE_BUF]; /* write buffer                 */
        size_t     wpos;
} HttpResponseWriter;

/* ================================================================
 *  HttpCtx -- per-request context (context handler style)
 * ================================================================ */

typedef struct {
        Arena* arena; /* per-connection scratch arena -- freed after handler */
        void*  user;  /* caller-supplied application data                    */
} HttpCtx;

/* ================================================================
 *  Handler function types
 * ================================================================ */

/* Go style: void handler(ResponseWriter, Request) */
typedef void (*HttpHandlerFn)(HttpResponseWriter* w, HttpRequest* req);

/* Context style: void handler(ResponseWriter, Request, Ctx) */
typedef void (*HttpHandlerCtxFn)(HttpResponseWriter* w,
                                 HttpRequest*        req,
                                 HttpCtx*            ctx);

/* ================================================================
 *  HttpMux -- request multiplexer / router
 * ================================================================ */

typedef enum {
        _HTTP_HANDLER_PLAIN = 0,
        _HTTP_HANDLER_CTX,
        _HTTP_HANDLER_METHOD, /* future: method-restricted */
} _HttpHandlerKind;

typedef struct {
        Str              pattern;
        _HttpHandlerKind kind;
        HttpHandlerFn    fn;
        HttpHandlerCtxFn fn_ctx;
        void*            user;   /* for ctx handlers          */
        Str              method; /* "" = any, else "GET" etc  */
} _HttpRoute;

typedef struct {
        _HttpRoute routes[HTTP_MAX_ROUTES];
        size_t     num_routes;
        void*      global_user; /* default ctx.user for plain handlers   */
        Arena*     arena;
} HttpMux;

/* ================================================================
 *  Mux API
 * ================================================================ */

/* Create a new mux backed by arena a. */
HttpMux* http_mux_new(Arena* a);

/* Register a Go-style handler for pattern. */
void http_handle(HttpMux* mux, Str pattern, HttpHandlerFn fn);

/* Register a Go-style handler restricted to one HTTP method. */
void http_handle_method(HttpMux*      mux,
                        Str           pattern,
                        Str           method,
                        HttpHandlerFn fn);

/* Register a context handler (ctx.user = user_data). */
void http_handle_ctx(HttpMux*         mux,
                     Str              pattern,
                     HttpHandlerCtxFn fn,
                     void*            user_data);

/* Register a context handler restricted to one method. */
void http_handle_ctx_method(HttpMux*         mux,
                            Str              pattern,
                            Str              method,
                            HttpHandlerCtxFn fn,
                            void*            user_data);

/* Set a default user pointer for plain (non-ctx) handlers' HttpCtx. */
void http_mux_set_user(HttpMux* mux, void* user);

/* ================================================================
 *  ResponseWriter API
 * ================================================================ */

/* Set response status code (must call before WriteHeader or first Write). */
void http_set_status(HttpResponseWriter* w, int status);

/* Set a response header. */
void http_set_header(HttpResponseWriter* w, Str key, Str value);

/* Flush response headers to the wire (called automatically on first Write). */
void http_write_header(HttpResponseWriter* w);

/* Write raw bytes. */
void http_write(HttpResponseWriter* w, const void* buf, size_t n);

/* Write a Str. */
void http_write_str(HttpResponseWriter* w, Str s);

/* Write a printf-formatted string (arena used for temporary buffer). */
void http_write_fmt(HttpResponseWriter* w, Arena* a, const char* fmt, ...);

/* Flush the write buffer. */
void http_flush(HttpResponseWriter* w);

/* Convenience: send a complete JSON response. */
void http_json(HttpResponseWriter* w, int status, Str body);
void http_text(HttpResponseWriter* w, int status, Str body);
void http_html(HttpResponseWriter* w, int status, Str body);

/* Convenience: redirect. */
void http_redirect(HttpResponseWriter* w, HttpRequest* req, Str url, int code);

/* Convenience: 404 Not Found text response. */
void http_not_found(HttpResponseWriter* w);

/* Convenience: 405 Method Not Allowed. */
void http_method_not_allowed(HttpResponseWriter* w);

/* Convenience: 500 Internal Server Error. */
void http_error(HttpResponseWriter* w, Str message, int code);

/* ================================================================
 *  Request helpers
 * ================================================================ */

/* Get a request header value (str_null if absent). */
Str http_header(const HttpRequest* req, Str key);

/* Get a query string parameter (str_null if absent). */
Str http_query(const HttpRequest* req, Str key);

/* Get a path segment by index after split on '/'. */
Str http_path_seg(const HttpRequest* req, size_t index);
Str http_path_param(const HttpRequest* req, const char* name);

/* ================================================================
 *  Server entry points
 * ================================================================ */

/*
 * Thread-per-connection server (like Go's http.ListenAndServe).
 * Blocks forever; returns -1 on listen failure.
 */
int http_serve(HttpMux* mux, Str host, uint16_t port, Arena* a);

/*
 * Thread-pool server: N worker threads share the accept queue.
 * Blocks forever; returns -1 on listen failure.
 */
int http_serve_pool(
    HttpMux* mux, Str host, uint16_t port, int num_workers, Arena* a);

/* ================================================================
 *  Status text helper
 * ================================================================ */
const char* http_status_text(int status);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#        ifdef BAREHTTP_IMPLEMENTATION

#                include <ctype.h>
#                include <stdio.h>
#                include <stdlib.h>
#                include <string.h>

/* ----------------------------------------------------------------
 *  Internal helpers
 * ---------------------------------------------------------------- */

static Str _http_trim(Str s) {
        while (s.len && (unsigned char)s.ptr[0] <= ' ') {
                s.ptr++;
                s.len--;
        }
        while (s.len && (unsigned char)s.ptr[s.len - 1] <= ' ') s.len--;
        return s;
}

static bool _str_eq_nocase(Str a, Str b) {
        if (a.len != b.len) return false;
        size_t i;
        for (i = 0; i < a.len; i++)
                if (tolower((unsigned char)a.ptr[i]) !=
                    tolower((unsigned char)b.ptr[i]))
                        return false;
        return true;
}

static Str _http_url_decode(Arena* a, Str s) __attribute__((unused));
static Str _http_url_decode(Arena* a, Str s) {
        char*  out = arena_push_array(a, char, s.len + 1);
        size_t n   = 0;
        size_t i   = 0;
        while (i < s.len) {
                if (s.ptr[i] == '%' && i + 2 < s.len) {
                        char hi = s.ptr[i + 1], lo = s.ptr[i + 2];
                        int  h   = (hi >= 'a')   ? hi - 'a' + 10
                                   : (hi >= 'A') ? hi - 'A' + 10
                                                 : hi - '0';
                        int  l   = (lo >= 'a')   ? lo - 'a' + 10
                                   : (lo >= 'A') ? lo - 'A' + 10
                                                 : lo - '0';
                        out[n++] = (char)((h << 4) | l);
                        i += 3;
                } else if (s.ptr[i] == '+') {
                        out[n++] = ' ';
                        i++;
                } else {
                        out[n++] = s.ptr[i++];
                }
        }
        out[n] = '\0';
        return str_buf(out, n);
}

/* ----------------------------------------------------------------
 *  Status text
 * ---------------------------------------------------------------- */

const char* http_status_text(int status) {
        switch (status) {
                case 200:
                        return "OK";
                case 201:
                        return "Created";
                case 204:
                        return "No Content";
                case 301:
                        return "Moved Permanently";
                case 302:
                        return "Found";
                case 304:
                        return "Not Modified";
                case 400:
                        return "Bad Request";
                case 401:
                        return "Unauthorized";
                case 403:
                        return "Forbidden";
                case 404:
                        return "Not Found";
                case 405:
                        return "Method Not Allowed";
                case 408:
                        return "Request Timeout";
                case 409:
                        return "Conflict";
                case 422:
                        return "Unprocessable Entity";
                case 429:
                        return "Too Many Requests";
                case 500:
                        return "Internal Server Error";
                case 501:
                        return "Not Implemented";
                case 503:
                        return "Service Unavailable";
                default:
                        return "Unknown";
        }
}

/* ----------------------------------------------------------------
 *  Mux
 * ---------------------------------------------------------------- */

HttpMux* http_mux_new(Arena* a) {
        HttpMux* m = arena_push_type(a, HttpMux);
        memset(m, 0, sizeof *m);
        m->arena = a;
        return m;
}

static void _http_add_route(HttpMux*         mux,
                            Str              pattern,
                            _HttpHandlerKind kind,
                            HttpHandlerFn    fn,
                            HttpHandlerCtxFn fn_ctx,
                            Str              method,
                            void*            user) {
        assert(mux->num_routes < HTTP_MAX_ROUTES && "http: too many routes");
        _HttpRoute* r = &mux->routes[mux->num_routes++];
        r->pattern    = pattern;
        r->kind       = kind;
        r->fn         = fn;
        r->fn_ctx     = fn_ctx;
        r->method     = method;
        r->user       = user;
}

void http_handle(HttpMux* mux, Str pattern, HttpHandlerFn fn) {
        _http_add_route(mux,
                        pattern,
                        _HTTP_HANDLER_PLAIN,
                        fn,
                        NULL,
                        str_lit(""),
                        mux->global_user);
}

void http_handle_method(HttpMux*      mux,
                        Str           pattern,
                        Str           method,
                        HttpHandlerFn fn) {
        _http_add_route(mux,
                        pattern,
                        _HTTP_HANDLER_PLAIN,
                        fn,
                        NULL,
                        method,
                        mux->global_user);
}

void http_handle_ctx(HttpMux*         mux,
                     Str              pattern,
                     HttpHandlerCtxFn fn,
                     void*            user_data) {
        _http_add_route(
            mux, pattern, _HTTP_HANDLER_CTX, NULL, fn, str_lit(""), user_data);
}

void http_handle_ctx_method(HttpMux*         mux,
                            Str              pattern,
                            Str              method,
                            HttpHandlerCtxFn fn,
                            void*            user_data) {
        _http_add_route(
            mux, pattern, _HTTP_HANDLER_CTX, NULL, fn, method, user_data);
}

void http_mux_set_user(HttpMux* mux, void* user) {
        mux->global_user = user;
        /* Backfill existing plain handlers */
        size_t i;
        for (i = 0; i < mux->num_routes; i++)
                if (mux->routes[i].kind == _HTTP_HANDLER_PLAIN)
                        mux->routes[i].user = user;
}

/* Route matching: exact first, then longest-prefix. */
static _HttpRoute* _http_match(HttpMux* mux, Str path, Str method) {
        _HttpRoute* best     = NULL;
        size_t      best_len = 0;
        size_t      i;

        for (i = 0; i < mux->num_routes; i++) {
                _HttpRoute* r = &mux->routes[i];

                /* Method filter */
                if (r->method.len > 0 && !_str_eq_nocase(r->method, method))
                        continue;

                /* Exact match wins immediately */
                if (str_eq(r->pattern, path)) return r;

                /* Prefix match (pattern ends with '/') */
                if (r->pattern.len > 0 &&
                    r->pattern.ptr[r->pattern.len - 1] == '/' &&
                    r->pattern.len >= best_len) {
                        size_t plen = r->pattern.len;
                        if (path.len >= plen &&
                            memcmp(path.ptr, r->pattern.ptr, plen) == 0) {
                                best     = r;
                                best_len = plen;
                        }
                }
        }

        /* "/" catch-all fallback */
        if (!best) {
                for (i = 0; i < mux->num_routes; i++) {
                        if (str_eq(mux->routes[i].pattern, str_lit("/")))
                                return &mux->routes[i];
                }
        }

        return best;
}

/* ----------------------------------------------------------------
 *  Request parsing
 * ---------------------------------------------------------------- */

static bool _http_parse_request(Arena*       a,
                                const char*  raw,
                                size_t       raw_len,
                                HttpRequest* req) {
        memset(req, 0, sizeof *req);
        Str s = str_buf(raw, raw_len);

        /* Request line */
        size_t eol = 0;
        while (eol < s.len && s.ptr[eol] != '\r' && s.ptr[eol] != '\n') eol++;
        Str line = str_buf(s.ptr, eol);

        /* Method */
        size_t sp = 0;
        while (sp < line.len && line.ptr[sp] != ' ') sp++;
        req->method = str_buf(line.ptr, sp);
        if (sp >= line.len) return false;
        sp++;

        /* Path + query */
        size_t sp2 = sp;
        while (sp2 < line.len && line.ptr[sp2] != ' ') sp2++;
        Str full_path = str_buf(line.ptr + sp, sp2 - sp);
        sp2++;

        /* Split path from query string */
        size_t qi = 0;
        while (qi < full_path.len && full_path.ptr[qi] != '?') qi++;
        req->path = str_buf(full_path.ptr, qi);
        if (qi < full_path.len) {
                req->query_string =
                    str_buf(full_path.ptr + qi + 1, full_path.len - qi - 1);
        }

        /* Version */
        req->version =
            str_buf(line.ptr + sp2, line.len > sp2 ? line.len - sp2 : 0);

        /* Skip past first line */
        size_t pos = eol;
        if (pos < s.len && s.ptr[pos] == '\r') pos++;
        if (pos < s.len && s.ptr[pos] == '\n') pos++;

        /* Headers */
        while (pos < s.len) {
                if (s.ptr[pos] == '\r' || s.ptr[pos] == '\n') break;
                size_t start = pos;
                while (pos < s.len && s.ptr[pos] != '\r' && s.ptr[pos] != '\n')
                        pos++;
                Str hline = str_buf(s.ptr + start, pos - start);
                if (pos < s.len && s.ptr[pos] == '\r') pos++;
                if (pos < s.len && s.ptr[pos] == '\n') pos++;

                /* Find ':' */
                size_t ci = 0;
                while (ci < hline.len && hline.ptr[ci] != ':') ci++;
                if (ci >= hline.len) continue;

                if (req->num_headers < HTTP_MAX_HEADERS) {
                        HttpHeader* h = &req->headers[req->num_headers++];
                        h->key        = _http_trim(str_buf(hline.ptr, ci));
                        h->value      = _http_trim(
                            str_buf(hline.ptr + ci + 1, hline.len - ci - 1));
                }
        }
        /* Skip blank line separating headers from body */
        if (pos < s.len && s.ptr[pos] == '\r') pos++;
        if (pos < s.len && s.ptr[pos] == '\n') pos++;

        /* Body (remainder) */
        if (pos < s.len) req->body = str_buf(s.ptr + pos, s.len - pos);

        /* Keep-alive */
        Str conn        = http_header(req, str_lit("Connection"));
        req->keep_alive = !_str_eq_nocase(conn, str_lit("close"));

        (void)a;
        return true;
}

/* ----------------------------------------------------------------
 *  Request helpers
 * ---------------------------------------------------------------- */

Str http_header(const HttpRequest* req, Str key) {
        size_t i;
        for (i = 0; i < req->num_headers; i++)
                if (_str_eq_nocase(req->headers[i].key, key))
                        return req->headers[i].value;
        return str_null();
}

Str http_query(const HttpRequest* req, Str key) {
        Str qs = req->query_string;
        while (qs.len) {
                size_t amp = 0;
                while (amp < qs.len && qs.ptr[amp] != '&') amp++;
                Str pair = str_buf(qs.ptr, amp);

                size_t eq = 0;
                while (eq < pair.len && pair.ptr[eq] != '=') eq++;
                Str pkey = str_buf(pair.ptr, eq);
                if (str_eq(pkey, key)) {
                        if (eq < pair.len)
                                return str_buf(pair.ptr + eq + 1,
                                               pair.len - eq - 1);
                        return str_buf("", 0);
                }

                qs.ptr += amp + (amp < qs.len ? 1 : 0);
                qs.len -= amp + (amp < qs.len ? 1 : 0);
        }
        return str_null();
}

Str http_path_seg(const HttpRequest* req, size_t index) {
        Str    p   = req->path;
        size_t idx = 0;
        if (p.len > 0 && p.ptr[0] == '/') {
                p.ptr++;
                p.len--;
        }
        while (p.len) {
                size_t sl = 0;
                while (sl < p.len && p.ptr[sl] != '/') sl++;
                if (idx == index) return str_buf(p.ptr, sl);
                idx++;
                p.ptr += sl + (sl < p.len ? 1 : 0);
                p.len -= sl + (sl < p.len ? 1 : 0);
        }
        return str_null();
}

/* ----------------------------------------------------------------
 *  ResponseWriter API
 * ---------------------------------------------------------------- */

static void _http_sock_write(Sock s, const void* buf, size_t n) {
        net_send(s, buf, n);
}

static void _http_flush_wbuf(HttpResponseWriter* w) {
        if (w->wpos > 0) {
                _http_sock_write(w->sock, w->wbuf, w->wpos);
                w->wpos = 0;
        }
}

static void _http_raw_write(HttpResponseWriter* w, const void* buf, size_t n) {
        const uint8_t* src = (const uint8_t*)buf;
        while (n > 0) {
                size_t space = HTTP_WRITE_BUF - w->wpos;
                if (space == 0) {
                        _http_flush_wbuf(w);
                        space = HTTP_WRITE_BUF;
                }
                size_t take = n < space ? n : space;
                memcpy(w->wbuf + w->wpos, src, take);
                w->wpos += take;
                src += take;
                n -= take;
        }
}

void http_set_status(HttpResponseWriter* w, int status) {
        w->status = status;
}

void http_set_header(HttpResponseWriter* w, Str key, Str value) {
        assert(!w->header_sent && "http: cannot set header after body write");
        if (w->num_headers >= HTTP_MAX_HEADERS) return;
        /* Overwrite existing */
        size_t i;
        for (i = 0; i < w->num_headers; i++) {
                if (_str_eq_nocase(w->headers[i].key, key)) {
                        w->headers[i].value = value;
                        return;
                }
        }
        w->headers[w->num_headers].key   = key;
        w->headers[w->num_headers].value = value;
        w->num_headers++;
}

void http_write_header(HttpResponseWriter* w) {
        if (w->header_sent) return;
        w->header_sent = true;

        char status_line[64];
        int  slen = snprintf(status_line,
                             sizeof status_line,
                             "HTTP/1.1 %d %s\r\n",
                             w->status,
                             http_status_text(w->status));
        _http_raw_write(w, status_line, (size_t)slen);

        /* Default headers */
        bool   has_ct   = false;
        bool   has_conn = false;
        size_t i;
        for (i = 0; i < w->num_headers; i++) {
                if (_str_eq_nocase(w->headers[i].key, str_lit("Content-Type")))
                        has_ct = true;
                if (_str_eq_nocase(w->headers[i].key, str_lit("Connection")))
                        has_conn = true;
        }
        if (!has_ct) {
                _http_raw_write(
                    w, "Content-Type: text/plain; charset=utf-8\r\n", 41);
        }
        if (!has_conn) {
                _http_raw_write(w, "Connection: close\r\n", 19);
        }

        for (i = 0; i < w->num_headers; i++) {
                _http_raw_write(
                    w, w->headers[i].key.ptr, w->headers[i].key.len);
                _http_raw_write(w, ": ", 2);
                _http_raw_write(
                    w, w->headers[i].value.ptr, w->headers[i].value.len);
                _http_raw_write(w, "\r\n", 2);
        }
        _http_raw_write(w, "\r\n", 2);
}

void http_write(HttpResponseWriter* w, const void* buf, size_t n) {
        if (!w->header_sent) http_write_header(w);
        _http_raw_write(w, buf, n);
}

void http_write_str(HttpResponseWriter* w, Str s) {
        http_write(w, s.ptr, s.len);
}

void http_write_fmt(HttpResponseWriter* w, Arena* a, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        if (n <= 0) return;
        char* buf = arena_push_array(a, char, (size_t)n + 1);
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
        http_write(w, buf, (size_t)n);
}

void http_flush(HttpResponseWriter* w) {
        _http_flush_wbuf(w);
}

void http_json(HttpResponseWriter* w, int status, Str body) {
        http_set_status(w, status);
        http_set_header(
            w, str_lit("Content-Type"), str_lit("application/json"));
        http_write_str(w, body);
}

void http_text(HttpResponseWriter* w, int status, Str body) {
        http_set_status(w, status);
        http_set_header(
            w, str_lit("Content-Type"), str_lit("text/plain; charset=utf-8"));
        http_write_str(w, body);
}

void http_html(HttpResponseWriter* w, int status, Str body) {
        http_set_status(w, status);
        http_set_header(
            w, str_lit("Content-Type"), str_lit("text/html; charset=utf-8"));
        http_write_str(w, body);
}

Str http_path_param(const HttpRequest* req, const char* name) {
        if (!req || str_is_null(req->_matched_pattern)) return str_null();
        Str pat  = req->_matched_pattern;
        Str path = req->path;
        if (pat.len > 0 && pat.ptr[0] == '/') {
                pat.ptr++;
                pat.len--;
        }
        if (path.len > 0 && path.ptr[0] == '/') {
                path.ptr++;
                path.len--;
        }
        while (pat.len > 0 && path.len > 0) {
                size_t pi = 0;
                while (pi < pat.len && pat.ptr[pi] != '/') pi++;
                size_t si = 0;
                while (si < path.len && path.ptr[si] != '/') si++;
                Str pseg = str_buf(pat.ptr, pi);
                Str vseg = str_buf(path.ptr, si);
                if (pseg.len > 0 && pseg.ptr[0] == ':') {
                        Str pname = str_buf(pseg.ptr + 1, pseg.len - 1);
                        if (str_eq(pname, str_from_c(name))) return vseg;
                }
                pat.ptr += pi + (pi < pat.len ? 1u : 0u);
                pat.len -= pi + (pi < pat.len ? 1u : 0u);
                path.ptr += si + (si < path.len ? 1u : 0u);
                path.len -= si + (si < path.len ? 1u : 0u);
        }
        return str_null();
}

void http_redirect(HttpResponseWriter* w, HttpRequest* req, Str url, int code) {
        (void)req;
        http_set_status(w, code);
        http_set_header(w, str_lit("Location"), url);
        http_write_str(w, str_lit("Redirecting..."));
}

void http_not_found(HttpResponseWriter* w) {
        http_error(w, str_lit("404 not found\n"), HTTP_404);
}

void http_method_not_allowed(HttpResponseWriter* w) {
        http_error(w, str_lit("405 method not allowed\n"), HTTP_405);
}

void http_error(HttpResponseWriter* w, Str message, int code) {
        http_set_status(w, code);
        http_write_str(w, message);
}

/* ----------------------------------------------------------------
 *  Connection handler (runs in its own thread)
 * ---------------------------------------------------------------- */

typedef struct {
        Sock     sock;
        HttpMux* mux;
} _HttpConnArg;

static void* _http_handle_conn(void* arg) {
        _HttpConnArg ca = *(_HttpConnArg*)arg;
        free(arg); /* tiny 16-byte malloc freed here */

        /* Per-connection arena */
        Arena* ca_arena = arena_new(HTTP_CONN_ARENA_SIZE);

        /* Read buffer on heap to avoid large stack frames */
        char* rbuf = (char*)malloc(HTTP_READ_BUF);
        if (!rbuf) {
                arena_free(ca_arena);
                net_close(ca.sock);
                return NULL;
        }

        int nr = net_recv(ca.sock, rbuf, HTTP_READ_BUF - 1);
        if (nr <= 0) {
                free(rbuf);
                arena_free(ca_arena);
                net_close(ca.sock);
                return NULL;
        }
        rbuf[nr] = '\0';

        /* Parse */
        HttpRequest req;
        if (!_http_parse_request(ca_arena, rbuf, (size_t)nr, &req)) {
                free(rbuf);
                arena_free(ca_arena);
                net_close(ca.sock);
                return NULL;
        }
        /* Match route and dispatch -- rbuf must stay alive until after handler
         */
        HttpResponseWriter w;
        memset(&w, 0, sizeof w);
        w.sock   = ca.sock;
        w.status = HTTP_200;

        _HttpRoute* route = _http_match(ca.mux, req.path, req.method);
        if (!route) {
                http_not_found(&w);
        } else {
                req._matched_pattern = route->pattern;
                HttpCtx ctx;
                ctx.arena = ca_arena;
                ctx.user  = route->user;

                if (route->kind == _HTTP_HANDLER_CTX && route->fn_ctx) {
                        route->fn_ctx(&w, &req, &ctx);
                } else if (route->fn) {
                        route->fn(&w, &req);
                } else {
                        http_not_found(&w);
                }
        }

        if (!w.header_sent) http_write_header(&w);
        http_flush(&w);
        free(rbuf); /* free after handler -- req strings point into rbuf */

        /* shutdown write side before close -- ensures client receives all data
           before the connection is torn down (avoids RST wiping send buffer) */
#                ifndef _BN_WIN
        shutdown(ca.sock._fd, SHUT_WR);
#                else
        shutdown(ca.sock._fd, SD_SEND);
#                endif
        net_close(ca.sock);
        arena_free(ca_arena);
        return NULL;
}

/* ----------------------------------------------------------------
 *  Thread-per-connection server
 * ---------------------------------------------------------------- */

int http_serve(HttpMux* mux, Str host, uint16_t port, Arena* a) {
        net_init();
        Sock srv = net_tcp_listen(host, port, 128);
        if (!sock_valid(srv)) {
                net_cleanup();
                return -1;
        }
        net_set_reuseaddr(srv, true);

        for (;;) {
                Sock conn = net_accept(srv, NULL);
                if (!sock_valid(conn)) continue;

                _HttpConnArg* arg = (_HttpConnArg*)malloc(sizeof(_HttpConnArg));
                if (!arg) {
                        net_close(conn);
                        continue;
                }
                arg->sock = conn;
                arg->mux  = mux;

                Thread t = thread_spawn(a, _http_handle_conn, arg);
                thread_detach(t);
        }
}

/* ----------------------------------------------------------------
 *  Thread pool server
 * ---------------------------------------------------------------- */

typedef struct {
        Sock     queue[HTTP_POOL_QUEUE_CAP];
        size_t   head, tail, len;
        Mutex    mu;
        Condvar  not_empty;
        Condvar  not_full;
        HttpMux* mux;
        Arena*   arena;
} _HttpPool;

static void* _http_pool_worker(void* arg) {
        _HttpPool* pool = (_HttpPool*)arg;
        for (;;) {
                mutex_lock(&pool->mu);
                while (pool->len == 0)
                        condvar_wait(&pool->not_empty, &pool->mu);

                Sock conn  = pool->queue[pool->head];
                pool->head = (pool->head + 1) % HTTP_POOL_QUEUE_CAP;
                pool->len--;
                condvar_signal(&pool->not_full);
                mutex_unlock(&pool->mu);

                /* Reuse the single-conn handler logic via a temp arg */
                _HttpConnArg* ca = (_HttpConnArg*)malloc(sizeof(_HttpConnArg));
                if (!ca) {
                        net_close(conn);
                        continue;
                }
                ca->sock = conn;
                ca->mux  = pool->mux;
                _http_handle_conn(ca); /* runs synchronously in this worker */
        }
        return NULL;
}

int http_serve_pool(
    HttpMux* mux, Str host, uint16_t port, int num_workers, Arena* a) {
        net_init();
        Sock srv = net_tcp_listen(host, port, 128);
        if (!sock_valid(srv)) {
                net_cleanup();
                return -1;
        }
        net_set_reuseaddr(srv, true);

        _HttpPool* pool = arena_push_type(a, _HttpPool);
        memset(pool, 0, sizeof *pool);
        pool->mux   = mux;
        pool->arena = a;

        int i;
        for (i = 0; i < num_workers; i++) {
                Thread t = thread_spawn(a, _http_pool_worker, pool);
                thread_detach(t);
        }

        for (;;) {
                Sock conn = net_accept(srv, NULL);
                if (!sock_valid(conn)) continue;

                mutex_lock(&pool->mu);
                while (pool->len >= HTTP_POOL_QUEUE_CAP)
                        condvar_wait(&pool->not_full, &pool->mu);

                pool->queue[(pool->head + pool->len) % HTTP_POOL_QUEUE_CAP] =
                    conn;
                pool->len++;
                condvar_signal(&pool->not_empty);
                mutex_unlock(&pool->mu);
        }
}

#        endif /* BAREHTTP_IMPLEMENTATION */

/*
 * ================================================================
 *  HTTP/1.1 Client
 * ================================================================
 *
 *  Simple blocking HTTP/1.1 client built on barenet.
 *  Supports GET, POST, and custom methods.
 *  Follows redirects up to HTTP_CLIENT_MAX_REDIRECTS times.
 *  Response body is arena-allocated.
 *
 *  EXAMPLE
 *
 *    Arena *a = arena_new(MB(1));
 *    HttpResponse res = http_get(a, str_lit("http://example.com/api/data"));
 *    if (res.status == 200) {
 *        printf(StrFmt "\n", StrArgs(res.body));
 *    }
 *
 *    // POST with JSON body
 *    HttpClientOpts opts = http_client_opts_default();
 *    opts.content_type = str_lit("application/json");
 *    HttpResponse post_res = http_post(a,
 *        str_lit("http://example.com/api/users"),
 *        str_lit("{\"name\":\"Alice\"}"),
 *        opts);
 * ================================================================ */

#        ifndef HTTP_CLIENT_MAX_REDIRECTS
#                define HTTP_CLIENT_MAX_REDIRECTS 5
#        endif
#        ifndef HTTP_CLIENT_TIMEOUT_MS
#                define HTTP_CLIENT_TIMEOUT_MS 10000
#        endif
#        ifndef HTTP_CLIENT_BUF
#                define HTTP_CLIENT_BUF KB(16)
#        endif

typedef struct {
        int         status; /* HTTP status code, 0 on connection error  */
        HttpHeader  headers[HTTP_MAX_HEADERS];
        size_t      num_headers;
        Str         body;  /* arena-allocated response body            */
        const char* error; /* non-NULL on connection/parse failure     */
} HttpResponse;

typedef struct {
        Str  content_type;     /* default: "application/octet-stream"      */
        int  timeout_ms;       /* default: HTTP_CLIENT_TIMEOUT_MS          */
        bool follow_redirects; /* default: true                            */
        /* Extra request headers (up to HTTP_MAX_HEADERS) */
        HttpHeader extra_headers[HTTP_MAX_HEADERS];
        size_t     num_extra;
} HttpClientOpts;

static inline HttpClientOpts http_client_opts_default(void) {
        HttpClientOpts o;
        memset(&o, 0, sizeof o);
        o.content_type     = str_lit("application/octet-stream");
        o.timeout_ms       = HTTP_CLIENT_TIMEOUT_MS;
        o.follow_redirects = true;
        return o;
}

/* Perform a GET request. */
HttpResponse http_get(Arena* a, Str url);

/* Perform a GET request with options. */
HttpResponse http_get_opts(Arena* a, Str url, HttpClientOpts opts);

/* Perform a POST request with a body. */
HttpResponse http_post(Arena* a, Str url, Str body, HttpClientOpts opts);

/* General request. method = "GET", "POST", "PUT", "DELETE", etc. */
HttpResponse http_request(
    Arena* a, Str method, Str url, Str body, HttpClientOpts opts);

/* Get a response header value. */
Str http_res_header(const HttpResponse* res, Str key);

#        ifdef BAREHTTP_IMPLEMENTATION

/* ================================================================
 *  HTTP Client Implementation
 * ================================================================ */

/* Parse URL into host, port, path */
typedef struct {
        Str      scheme; /* "http" or "https" */
        Str      host;
        uint16_t port;
        Str      path; /* including query string */
} _HttpUrl;

static bool _http_parse_url(Str url, _HttpUrl* out) {
        out->port = 80;
        out->path = str_lit("/");
        /* scheme */
        ptrdiff_t sep = str_find(url, str_lit("://"));
        if (sep < 0) {
                out->scheme = str_lit("http");
        } else {
                out->scheme = str_buf(url.ptr, (size_t)sep);
                url.ptr += (size_t)sep + 3;
                url.len -= (size_t)sep + 3;
                if (str_eq(out->scheme, str_lit("https"))) out->port = 443;
        }
        /* host[:port] */
        size_t slash = 0;
        while (slash < url.len && url.ptr[slash] != '/') slash++;
        Str host_part = str_buf(url.ptr, slash);
        if (slash < url.len) {
                out->path = str_buf(url.ptr + slash, url.len - slash);
        }
        /* port in host? */
        ptrdiff_t colon = str_find(host_part, str_lit(":"));
        if (colon >= 0) {
                out->host        = str_buf(host_part.ptr, (size_t)colon);
                Str     port_str = str_buf(host_part.ptr + colon + 1,
                                           host_part.len - (size_t)colon - 1);
                int64_t p        = 0;
                str_to_i64(port_str, &p);
                out->port = (uint16_t)p;
        } else {
                out->host = host_part;
        }
        return out->host.len > 0;
}

static bool _str_eq_nocase_client(Str a, Str b) {
        if (a.len != b.len) return false;
        size_t i;
        for (i = 0; i < a.len; i++)
                if (tolower((unsigned char)a.ptr[i]) !=
                    tolower((unsigned char)b.ptr[i]))
                        return false;
        return true;
}

Str http_res_header(const HttpResponse* res, Str key) {
        size_t i;
        for (i = 0; i < res->num_headers; i++)
                if (_str_eq_nocase_client(res->headers[i].key, key))
                        return res->headers[i].value;
        return str_null();
}

static HttpResponse _http_do_request(
    Arena* a, Str method, _HttpUrl* url, Str body, HttpClientOpts* opts) {
        HttpResponse res;
        memset(&res, 0, sizeof res);

        Sock s = net_tcp_connect(a, url->host, url->port);
        if (!sock_valid(s)) {
                res.error = "connection failed";
                return res;
        }
        net_set_recvtimeo(s, opts->timeout_ms);
        net_set_sendtimeo(s, opts->timeout_ms);

        /* Build request */
        /* We write directly into a stack buffer for the header, body sent
         * separately */
        char req_hdr[HTTP_READ_BUF];
        int  hlen = 0;

        /* Request line */
        hlen += snprintf(req_hdr + hlen,
                         sizeof req_hdr - (size_t)hlen,
                         StrFmt " " StrFmt " HTTP/1.1\r\n",
                         StrArgs(method),
                         StrArgs(url->path));

        /* Host header */
        hlen += snprintf(req_hdr + hlen,
                         sizeof req_hdr - (size_t)hlen,
                         "Host: " StrFmt "\r\n",
                         StrArgs(url->host));

        /* Content-Type + Content-Length if body */
        if (body.len > 0) {
                hlen += snprintf(req_hdr + hlen,
                                 sizeof req_hdr - (size_t)hlen,
                                 "Content-Type: " StrFmt "\r\n",
                                 StrArgs(opts->content_type));
                hlen += snprintf(req_hdr + hlen,
                                 sizeof req_hdr - (size_t)hlen,
                                 "Content-Length: %zu\r\n",
                                 body.len);
        }

        hlen += snprintf(req_hdr + hlen,
                         sizeof req_hdr - (size_t)hlen,
                         "Connection: close\r\n"
                         "Accept: */*\r\n");

        /* Extra headers */
        size_t ei;
        for (ei = 0; ei < opts->num_extra && hlen < (int)sizeof req_hdr - 4;
             ei++) {
                hlen += snprintf(req_hdr + hlen,
                                 sizeof req_hdr - (size_t)hlen,
                                 StrFmt ": " StrFmt "\r\n",
                                 StrArgs(opts->extra_headers[ei].key),
                                 StrArgs(opts->extra_headers[ei].value));
        }
        hlen += snprintf(req_hdr + hlen, sizeof req_hdr - (size_t)hlen, "\r\n");

        if (!net_send(s, req_hdr, (size_t)hlen)) {
                net_close(s);
                res.error = "send failed";
                return res;
        }
        if (body.len > 0 && !net_send(s, body.ptr, body.len)) {
                net_close(s);
                res.error = "send body failed";
                return res;
        }

        /* Read response into arena */
        Scratch sc      = scratch_begin(a);
        Slice(char) raw = slice_make(sc.a, char, KB(8));
        char tmp[KB(4)];
        int  nr;
        while ((nr = net_recv(s, tmp, sizeof tmp)) > 0) {
                int i;
                for (i = 0; i < nr; i++) slice_push(raw, char, tmp[i]);
        }
        net_close(s);

        size_t raw_len = slice_len(raw);
        if (raw_len < 12) {
                scratch_end(sc);
                res.error = "empty response";
                return res;
        }

        /* Parse status line: HTTP/1.x NNN ... */
        const char* rp   = raw;
        const char* rend = raw + raw_len;
        /* Skip "HTTP/1.x " */
        while (rp < rend && *rp != ' ') rp++;
        if (rp < rend) rp++;
        int status = 0;
        while (rp < rend && *rp >= '0' && *rp <= '9') {
                status = status * 10 + (*rp++ - '0');
        }
        res.status = status;

        /* Skip to end of status line */
        while (rp < rend && *rp != '\n') rp++;
        if (rp < rend) rp++;

        /* Parse headers */
        while (rp < rend) {
                if (*rp == '\r' || *rp == '\n') {
                        if (*rp == '\r' && rp + 1 < rend && rp[1] == '\n') rp++;
                        rp++;
                        break; /* blank line = end of headers */
                }
                const char* hstart = rp;
                while (rp < rend && *rp != '\n') rp++;
                if (rp < rend) rp++;
                const char* hend = rp - 1;
                if (hend > hstart && hend[-1] == '\r') hend--;
                /* Find ':' */
                const char* colon = hstart;
                while (colon < hend && *colon != ':') colon++;
                if (colon < hend && res.num_headers < HTTP_MAX_HEADERS) {
                        HttpHeader* h = &res.headers[res.num_headers++];
                        /* Trim key */
                        h->key = str_buf(hstart, (size_t)(colon - hstart));
                        while (h->key.len > 0 &&
                               (unsigned char)h->key.ptr[h->key.len - 1] <= ' ')
                                h->key.len--;
                        /* Trim value */
                        const char* vstart = colon + 1;
                        while (vstart < hend && (unsigned char)*vstart <= ' ')
                                vstart++;
                        h->value = str_buf(vstart, (size_t)(hend - vstart));
                        /* Arena-copy both so they outlive scratch */
                        h->key   = str_clone(a, h->key);
                        h->value = str_clone(a, h->value);
                }
        }

        /* Body = remainder, arena-copied */
        size_t body_len = (size_t)(rend - rp);
        char*  body_buf = arena_push_array(a, char, body_len + 1);
        memcpy(body_buf, rp, body_len);
        body_buf[body_len] = '\0';
        res.body           = str_buf(body_buf, body_len);

        scratch_end(sc);
        return res;
}

HttpResponse http_request(
    Arena* a, Str method, Str url, Str body, HttpClientOpts opts) {
        HttpResponse res;
        memset(&res, 0, sizeof res);

        _HttpUrl parsed;
        if (!_http_parse_url(url, &parsed)) {
                res.error = "invalid URL";
                return res;
        }

        int redirects = 0;
        for (;;) {
                res = _http_do_request(a, method, &parsed, body, &opts);
                if (!opts.follow_redirects) break;
                if (res.status != 301 && res.status != 302 &&
                    res.status != 303 && res.status != 307 && res.status != 308)
                        break;
                if (redirects++ >= HTTP_CLIENT_MAX_REDIRECTS) {
                        res.error = "too many redirects";
                        break;
                }
                Str loc = http_res_header(&res, str_lit("Location"));
                if (str_is_null(loc)) {
                        res.error = "redirect missing Location";
                        break;
                }
                if (!_http_parse_url(loc, &parsed)) {
                        res.error = "bad redirect URL";
                        break;
                }
                /* POST -> GET on 303 */
                if (res.status == 303) {
                        method = str_lit("GET");
                        body   = str_null();
                }
        }
        return res;
}

HttpResponse http_get(Arena* a, Str url) {
        HttpClientOpts opts = http_client_opts_default();
        return http_request(a, str_lit("GET"), url, str_null(), opts);
}
HttpResponse http_get_opts(Arena* a, Str url, HttpClientOpts opts) {
        return http_request(a, str_lit("GET"), url, str_null(), opts);
}
HttpResponse http_post(Arena* a, Str url, Str body, HttpClientOpts opts) {
        return http_request(a, str_lit("POST"), url, body, opts);
}

#        endif /* BAREHTTP_IMPLEMENTATION */

#endif /* BAREHTTP_H */
