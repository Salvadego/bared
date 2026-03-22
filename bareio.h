/*
 * io.h - Reader / Writer vtable interfaces
 * =========================================
 *
 *  USAGE
 *    #define IO_IMPLEMENTATION
 *    #include "io.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena, Str)
 *
 *  OPTIONAL DEPENDENCY
 *    os.h   - if included before io.h, file-backed Reader/Writer
 *             and convenience functions are unlocked:
 *               reader_from_osfile()
 *               writer_from_osfile()
 *               io_read_file()
 *               io_write_file()
 *
 *  INCLUDE ORDER
 *    // File IO available:
 *    #include "os.h"
 *    #include "io.h"
 *
 *    // Memory/buffer IO only:
 *    #include "io.h"
 *
 *  DESIGN
 *    Reader and Writer are thin vtable structs {ctx, fn, close_fn}.
 *    All internal buffers are allocated from an Arena.
 *    Errors crash via assert - no error return path.
 *    EOF is signalled by io_read returning 0.
 *
 *  EXAMPLE
 *
 *    // Memory round-trip
 *    MemWriter mw;
 *    Writer    w = writer_to_mem(&mw, arena, 256);
 *    io_write_str(&w, str_lit("hello, world"));
 *    Str result = mem_writer_result(&mw);
 *
 *    // Buffered line reader from FILE*
 *    Reader     r  = reader_from_file(stdin);
 *    BufReader *br = bufreader_make(arena, r, KB(8));
 *    Str line;
 *    while (!str_is_null(line = buf_read_line(arena, br)))
 *        printf(StrFmt "\n", StrArgs(line));
 *
 *    // File IO (requires os.h included first):
 *    // Str data = io_read_file(arena, str_lit("data.txt"));
 */

#ifndef IO_H
#define IO_H

#include <stdio.h>

#include "barestd.h"

/* ================================================================
 *  Reader / Writer vtable types
 * ================================================================ */

typedef size_t (*ReadFn)(void* ctx, void* buf, size_t n);
typedef void (*WriteFn)(void* ctx, const void* buf, size_t n);
typedef void (*CloseFn)(void* ctx);

typedef struct {
        void*   ctx;
        ReadFn  read_fn;
        CloseFn close_fn; /* NULL = no-op */
} Reader;

typedef struct {
        void*   ctx;
        WriteFn write_fn;
        CloseFn close_fn; /* NULL = no-op */
} Writer;

/* ================================================================
 *  Buffered wrappers
 * ================================================================ */

typedef struct {
        Reader   src;
        uint8_t* buf;
        size_t   cap;
        size_t   pos;
        size_t   fill;
        bool     eof;
} BufReader;

typedef struct {
        Writer   dst;
        uint8_t* buf;
        size_t   cap;
        size_t   pos;
} BufWriter;

/* ================================================================
 *  Memory writer state
 * ================================================================ */

typedef struct {
        Arena*   arena;
        uint8_t* buf;
        size_t   len;
        size_t   cap;
} MemWriter;

/* ================================================================
 *  Constructors - always available
 * ================================================================ */

/* Wrap a FILE* (caller manages fopen/fclose). */
Reader reader_from_file(FILE* f);
Writer writer_from_file(FILE* f);

/* Read-only view over an existing byte buffer. */
Reader reader_from_mem(Arena* a, const void* data, size_t len);

/* Growable arena-backed write buffer. */
Writer writer_to_mem(MemWriter* mw, Arena* a, size_t initial_cap);
Str    mem_writer_result(MemWriter* mw);

BufReader* bufreader_make(Arena* a, Reader src, size_t buf_cap);
BufWriter* bufwriter_make(Arena* a, Writer dst, size_t buf_cap);

/* ================================================================
 *  Constructors - available when os.h is included first
 * ================================================================ */

#ifdef OS_H
/* Reader/Writer over an open File (caller manages os_open/os_close). */
Reader reader_from_osfile(File* f);
Writer writer_from_osfile(File* f);

/* Convenience: open -> read_all -> close / open -> write -> close. */
Str  io_read_file(Arena* a, Str path);
void io_write_file(Str path, Str data);
#endif /* OS_H */

/* ================================================================
 *  Raw vtable dispatch
 * ================================================================ */

size_t io_read(Reader* r, void* buf, size_t n);
void   io_write(Writer* w, const void* buf, size_t n);
void   io_write_str(Writer* w, Str s);
void   io_close_reader(Reader* r);
void   io_close_writer(Writer* w);

/* Read everything remaining into arena memory, return as Str. */
Str io_read_all(Arena* a, Reader* r);

/* ================================================================
 *  Buffered operations
 * ================================================================ */

size_t buf_read(BufReader* br, void* buf, size_t n);
Str  buf_read_line(Arena* a, BufReader* br); /* strips \r\n; str_null on EOF */
void buf_write(BufWriter* bw, const void* buf, size_t n);
void buf_write_str(BufWriter* bw, Str s);
void buf_flush(BufWriter* bw);

/* ================================================================
 *  Convenience - stdout / stderr
 * ================================================================ */

void io_print(Str s);
void io_println(Str s);
void io_eprint(Str s);
void io_eprintln(Str s);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef IO_IMPLEMENTATION

/* ----------------------------------------------------------------
 *  FILE* backend
 * ---------------------------------------------------------------- */

static size_t _file_read_fn(void* ctx, void* buf, size_t n) {
        size_t r = fread(buf, 1, n, (FILE*)ctx);
        assert(!ferror((FILE*)ctx) && "io: fread error");
        return r;
}

static void _file_write_fn(void* ctx, const void* buf, size_t n) {
        size_t w = fwrite(buf, 1, n, (FILE*)ctx);
        assert(w == n && "io: fwrite error");
}

Reader reader_from_file(FILE* f) {
        Reader r;
        r.ctx      = f;
        r.read_fn  = _file_read_fn;
        r.close_fn = NULL;
        return r;
}

Writer writer_from_file(FILE* f) {
        Writer w;
        w.ctx      = f;
        w.write_fn = _file_write_fn;
        w.close_fn = NULL;
        return w;
}

/* ----------------------------------------------------------------
 *  Memory reader backend
 * ---------------------------------------------------------------- */

typedef struct {
        const uint8_t* data;
        size_t         len;
        size_t         pos;
} _MemRCtx;

static size_t _mem_read_fn(void* ctx, void* buf, size_t n) {
        _MemRCtx* m     = (_MemRCtx*)ctx;
        size_t    avail = m->len - m->pos;
        if (!avail) return 0;
        size_t got = n < avail ? n : avail;
        memcpy(buf, m->data + m->pos, got);
        m->pos += got;
        return got;
}

Reader reader_from_mem(Arena* a, const void* data, size_t len) {
        _MemRCtx* ctx = arena_push_type(a, _MemRCtx);
        ctx->data     = (const uint8_t*)data;
        ctx->len      = len;
        ctx->pos      = 0;
        Reader r;
        r.ctx      = ctx;
        r.read_fn  = _mem_read_fn;
        r.close_fn = NULL;
        return r;
}

/* ----------------------------------------------------------------
 *  Memory writer backend
 * ---------------------------------------------------------------- */

static void _mem_write_fn(void* ctx, const void* buf, size_t n) {
        MemWriter* mw = (MemWriter*)ctx;
        if (mw->len + n > mw->cap) {
                size_t nc = mw->cap ? mw->cap * 2 : 256;
                while (nc < mw->len + n) nc *= 2;
                uint8_t* nb = (uint8_t*)arena_push(mw->arena, nc, 1);
                if (mw->len) memcpy(nb, mw->buf, mw->len);
                mw->buf = nb;
                mw->cap = nc;
        }
        memcpy(mw->buf + mw->len, buf, n);
        mw->len += n;
}

Writer writer_to_mem(MemWriter* mw, Arena* a, size_t cap) {
        mw->arena = a;
        mw->len   = 0;
        mw->cap   = cap ? cap : 256;
        mw->buf   = (uint8_t*)arena_push(a, mw->cap, 1);
        Writer w;
        w.ctx      = mw;
        w.write_fn = _mem_write_fn;
        w.close_fn = NULL;
        return w;
}

Str mem_writer_result(MemWriter* mw) {
        return str_buf(mw->buf, mw->len);
}

/* ----------------------------------------------------------------
 *  OS file backend - only compiled when os.h was included
 * ---------------------------------------------------------------- */

#        ifdef OS_H

static size_t _osfile_read_fn(void* ctx, void* buf, size_t n) {
#                if defined(_WIN32) || defined(_WIN64)
        DWORD got = 0;
        ReadFile(((File*)ctx)->fd, buf, (DWORD)n, &got, NULL);
        return (size_t)got;
#                else
        ssize_t r = read(((File*)ctx)->fd, buf, n);
        assert(r >= 0 && "io: read error");
        return r < 0 ? 0 : (size_t)r;
#                endif
}

static void _osfile_write_fn(void* ctx, const void* buf, size_t n) {
#                if defined(_WIN32) || defined(_WIN64)
        DWORD w = 0;
        WriteFile(((File*)ctx)->fd, buf, (DWORD)n, &w, NULL);
        assert((size_t)w == n && "io: write error");
#                else
        ssize_t w = write(((File*)ctx)->fd, buf, n);
        assert((size_t)w == n && "io: write error");
#                endif
}

Reader reader_from_osfile(File* f) {
        Reader r;
        r.ctx      = f;
        r.read_fn  = _osfile_read_fn;
        r.close_fn = NULL;
        return r;
}

Writer writer_from_osfile(File* f) {
        Writer w;
        w.ctx      = f;
        w.write_fn = _osfile_write_fn;
        w.close_fn = NULL;
        return w;
}

Str io_read_file(Arena* a, Str path) {
        File   f = os_open(a, path, OS_RDONLY);
        Reader r = reader_from_osfile(&f);
        Str    s = io_read_all(a, &r);
        os_close(&f);
        return s;
}

void io_write_file(Str path, Str data) {
        bool ok = os_write_file(path, data);
        assert(ok && "io_write_file: failed");
}

#        endif /* OS_H */

/* ----------------------------------------------------------------
 *  Buffered reader
 * ---------------------------------------------------------------- */

BufReader* bufreader_make(Arena* a, Reader src, size_t cap) {
        if (!cap) cap = KB(8);
        BufReader* br = arena_push_type(a, BufReader);
        br->src       = src;
        br->buf       = arena_push_array(a, uint8_t, cap);
        br->cap       = cap;
        br->pos       = 0;
        br->fill      = 0;
        br->eof       = false;
        return br;
}

static void _bufreader_refill(BufReader* br) {
        if (br->eof) return;
        size_t rem = br->fill - br->pos;
        if (rem) memmove(br->buf, br->buf + br->pos, rem);
        br->pos    = 0;
        br->fill   = rem;
        size_t got = br->src.read_fn(
            br->src.ctx, br->buf + br->fill, br->cap - br->fill);
        br->fill += got;
        if (!got) br->eof = true;
}

size_t buf_read(BufReader* br, void* buf, size_t n) {
        size_t   total = 0;
        uint8_t* dst   = (uint8_t*)buf;
        while (n > 0) {
                if (br->pos >= br->fill) {
                        _bufreader_refill(br);
                        if (br->pos >= br->fill) break;
                }
                size_t avail = br->fill - br->pos;
                size_t take  = n < avail ? n : avail;
                memcpy(dst, br->buf + br->pos, take);
                br->pos += take;
                dst += take;
                total += take;
                n -= take;
        }
        return total;
}

Str buf_read_line(Arena* a, BufReader* br) {
        if (br->eof && br->pos >= br->fill) return str_null();

        char*  start = NULL;
        size_t len   = 0;

        for (;;) {
                if (br->pos >= br->fill) {
                        _bufreader_refill(br);
                        if (br->pos >= br->fill) break;
                }
                uint8_t* p     = br->buf + br->pos;
                size_t   avail = br->fill - br->pos;
                uint8_t* nl    = (uint8_t*)memchr(p, '\n', avail);
                size_t   take  = nl ? (size_t)(nl - p + 1) : avail;
                char*    chunk = (char*)arena_push(a, take, 1);
                if (!start) start = chunk;
                memcpy(chunk, p, take);
                br->pos += take;
                len += take;
                if (nl) break;
        }

        if (!start) return str_null();
        while (len > 0 && (start[len - 1] == '\n' || start[len - 1] == '\r'))
                len--;
        return str_buf(start, len);
}

/* ----------------------------------------------------------------
 *  Buffered writer
 * ---------------------------------------------------------------- */

BufWriter* bufwriter_make(Arena* a, Writer dst, size_t cap) {
        if (!cap) cap = KB(8);
        BufWriter* bw = arena_push_type(a, BufWriter);
        bw->dst       = dst;
        bw->buf       = arena_push_array(a, uint8_t, cap);
        bw->cap       = cap;
        bw->pos       = 0;
        return bw;
}

void buf_flush(BufWriter* bw) {
        if (bw->pos) {
                bw->dst.write_fn(bw->dst.ctx, bw->buf, bw->pos);
                bw->pos = 0;
        }
}

void buf_write(BufWriter* bw, const void* buf, size_t n) {
        const uint8_t* src = (const uint8_t*)buf;
        while (n > 0) {
                size_t space = bw->cap - bw->pos;
                if (!space) {
                        buf_flush(bw);
                        space = bw->cap;
                }
                size_t put = n < space ? n : space;
                memcpy(bw->buf + bw->pos, src, put);
                bw->pos += put;
                src += put;
                n -= put;
        }
}

void buf_write_str(BufWriter* bw, Str s) {
        buf_write(bw, s.ptr, s.len);
}

/* ----------------------------------------------------------------
 *  Raw dispatch
 * ---------------------------------------------------------------- */

size_t io_read(Reader* r, void* buf, size_t n) {
        return r->read_fn(r->ctx, buf, n);
}
void io_write(Writer* w, const void* buf, size_t n) {
        w->write_fn(w->ctx, buf, n);
}
void io_write_str(Writer* w, Str s) {
        w->write_fn(w->ctx, s.ptr, s.len);
}
void io_close_reader(Reader* r) {
        if (r->close_fn) r->close_fn(r->ctx);
}
void io_close_writer(Writer* w) {
        if (w->close_fn) w->close_fn(w->ctx);
}

Str io_read_all(Arena* a, Reader* r) {
        char*   start = NULL;
        size_t  total = 0;
        uint8_t tmp[KB(4)];
        size_t  got;
        while ((got = r->read_fn(r->ctx, tmp, sizeof tmp)) > 0) {
                char* chunk = (char*)arena_push(a, got, 1);
                if (!start) start = chunk;
                memcpy(chunk, tmp, got);
                total += got;
        }
        return start ? str_buf(start, total) : str_buf("", 0);
}

/* ----------------------------------------------------------------
 *  Convenience
 * ---------------------------------------------------------------- */

void io_print(Str s) {
        fwrite(s.ptr, 1, s.len, stdout);
}
void io_println(Str s) {
        fwrite(s.ptr, 1, s.len, stdout);
        fputc('\n', stdout);
}
void io_eprint(Str s) {
        fwrite(s.ptr, 1, s.len, stderr);
}
void io_eprintln(Str s) {
        fwrite(s.ptr, 1, s.len, stderr);
        fputc('\n', stderr);
}

#endif /* IO_IMPLEMENTATION */
#endif /* IO_H */
