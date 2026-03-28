/*
 * bareio.h -- Reader / Writer vtable, buffered I/O, typed binary I/O
 * ===================================================================
 *
 *  USAGE
 *    #define BAREIO_IMPLEMENTATION
 *    #include "bareio.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena, Str)
 *
 *  OPTIONAL
 *    bareos.h   -- include before bareio.h to unlock file-backed I/O:
 *                  reader_from_osfile(), writer_from_osfile(),
 *                  io_read_file(), io_write_file()
 *
 *  DESIGN
 *    Reader and Writer are thin vtable structs {ctx, fn, close_fn}.
 *    Errors crash via assert; EOF signals by returning 0 from read.
 *    All internal buffers come from an Arena.
 *
 *  TYPED BINARY I/O
 *    Little-endian (LE) and big-endian (BE) integer read/write:
 *      io_write_u8 / io_read_u8
 *      io_write_u16le / io_read_u16le    io_write_u16be / io_read_u16be
 *      io_write_u32le / io_read_u32le    io_write_u32be / io_read_u32be
 *      io_write_u64le / io_read_u64le    io_write_u64be / io_read_u64be
 *      (signed variants: io_write_i8 etc.)
 *    Pascal strings: io_write_pstr16 / io_read_pstr16
 *
 *  EXAMPLE
 *    MemWriter mw;
 *    Writer w = writer_to_mem(&mw, a, 256);
 *    io_write_u32le(&w, 0xDEADBEEF);
 *    io_write_pstr16(&w, str_lit("hello"));
 *
 *    Reader r = reader_from_mem(a, mw.buf, mw.len);
 *    uint32_t v; io_read_u32le(&r, &v);   // 0xDEADBEEF
 *    Str s = io_read_pstr16(&r, a);        // "hello"
 *
 *  EXAMPLE -- line reader
 *    Reader r = reader_from_file(stdin);
 *    BufReader *br = bufreader_make(a, r, KB(8));
 *    Str line;
 *    while (!str_is_null(line = buf_read_line(a, br)))
 *        printf(StrFmt "\n", StrArgs(line));
 *
 *  EXAMPLE -- tee: write to two Writers simultaneously
 *    Writer tee = writer_tee(a, &w1, &w2);
 *    io_write_str(&tee, str_lit("goes to both"));
 */
#ifndef BAREIO_H
#define BAREIO_H

#include <stdio.h>

#include "barestd.h"

/* ================================================================
 *  Core vtable types
 * ================================================================ */
typedef size_t (*ReadFn)(void* ctx, void* buf, size_t n);
typedef void (*WriteFn)(void* ctx, const void* buf, size_t n);
typedef void (*CloseFn)(void* ctx);

typedef struct {
        void*   ctx;
        ReadFn  read_fn;
        CloseFn close_fn;
} Reader;
typedef struct {
        void*   ctx;
        WriteFn write_fn;
        CloseFn close_fn;
} Writer;

/* ================================================================
 *  Buffered wrappers
 * ================================================================ */
typedef struct {
        Reader   src;
        uint8_t* buf;
        size_t   cap, pos, fill;
        bool     eof;
} BufReader;

typedef struct {
        Writer   dst;
        uint8_t* buf;
        size_t   cap, pos;
        bool     line_buffered; /* flush on '\n' if true */
} BufWriter;

typedef struct {
        Arena*   arena;
        uint8_t* buf;
        size_t   len, cap;
} MemWriter;

/* ================================================================
 *  Constructors — always available
 * ================================================================ */
Reader reader_from_file(FILE* f);
Writer writer_from_file(FILE* f);
Reader reader_from_mem(Arena* a, const void* data, size_t len);
Writer writer_to_mem(MemWriter* mw, Arena* a, size_t initial_cap);
Str    mem_writer_result(MemWriter* mw);

BufReader* bufreader_make(Arena* a, Reader src, size_t buf_cap);
BufWriter* bufwriter_make(Arena* a, Writer dst, size_t buf_cap);

/* Tee: writes to both w1 and w2. State allocated from arena. */
Writer writer_tee(Arena* a, Writer* w1, Writer* w2);

/* Null writer: discards all bytes (useful for dry-run / size counting). */
Writer writer_null(void);

/* ================================================================
 *  File-backed I/O (only when bareos.h included first)
 * ================================================================ */
#ifdef BAREOS_H
Reader reader_from_osfile(File* f);
Writer writer_from_osfile(File* f);
Str    io_read_file(Arena* a, Str path);
void   io_write_file(Str path, Str data);
#endif

/* ================================================================
 *  Raw vtable dispatch
 * ================================================================ */
size_t io_read(Reader* r, void* buf, size_t n);
void   io_write(Writer* w, const void* buf, size_t n);
void   io_write_str(Writer* w, Str s);
void   io_close_reader(Reader* r);
void   io_close_writer(Writer* w);

/* Read all remaining bytes into arena. */
Str io_read_all(Arena* a, Reader* r);

/* Read exactly n bytes; asserts if not enough data. */
void io_read_exactly(Reader* r, void* buf, size_t n);

/* Copy all bytes from r into w until EOF. Returns bytes copied. */
size_t io_copy(Reader* r, Writer* w);

/* printf-style write into w using a scratch arena. */
void io_printf(Arena* scratch, Writer* w, const char* fmt, ...);

/* ================================================================
 *  Buffered operations
 * ================================================================ */
size_t buf_read(BufReader* br, void* buf, size_t n);
Str  buf_read_line(Arena* a, BufReader* br); /* strips \r\n; str_null on EOF */
void buf_write(BufWriter* bw, const void* buf, size_t n);
void buf_write_str(BufWriter* bw, Str s);
void buf_flush(BufWriter* bw);

/* ================================================================
 *  Typed integer I/O — little-endian
 * ================================================================ */
static inline void io_write_u8(Writer* w, uint8_t v) {
        w->write_fn(w->ctx, &v, 1);
}
static inline void io_write_u16le(Writer* w, uint16_t v) {
        uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
        w->write_fn(w->ctx, b, 2);
}
static inline void io_write_u32le(Writer* w, uint32_t v) {
        uint8_t b[4] = {(uint8_t)v,
                        (uint8_t)(v >> 8),
                        (uint8_t)(v >> 16),
                        (uint8_t)(v >> 24)};
        w->write_fn(w->ctx, b, 4);
}
static inline void io_write_u64le(Writer* w, uint64_t v) {
        uint8_t b[8];
        int     i;
        for (i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (i * 8));
        w->write_fn(w->ctx, b, 8);
}

static inline bool io_read_u8(Reader* r, uint8_t* v) {
        return r->read_fn(r->ctx, v, 1) == 1;
}
static inline bool io_read_u16le(Reader* r, uint16_t* v) {
        uint8_t b[2];
        if (r->read_fn(r->ctx, b, 2) != 2) return false;
        *v = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
        return true;
}
static inline bool io_read_u32le(Reader* r, uint32_t* v) {
        uint8_t b[4];
        if (r->read_fn(r->ctx, b, 4) != 4) return false;
        *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
             ((uint32_t)b[3] << 24);
        return true;
}
static inline bool io_read_u64le(Reader* r, uint64_t* v) {
        uint8_t b[8];
        int     i;
        if (r->read_fn(r->ctx, b, 8) != 8) return false;
        *v = 0;
        for (i = 0; i < 8; i++) *v |= (uint64_t)b[i] << (i * 8);
        return true;
}

/* ================================================================
 *  Typed integer I/O — big-endian
 * ================================================================ */
static inline void io_write_u16be(Writer* w, uint16_t v) {
        uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
        w->write_fn(w->ctx, b, 2);
}
static inline void io_write_u32be(Writer* w, uint32_t v) {
        uint8_t b[4] = {(uint8_t)(v >> 24),
                        (uint8_t)(v >> 16),
                        (uint8_t)(v >> 8),
                        (uint8_t)v};
        w->write_fn(w->ctx, b, 4);
}
static inline void io_write_u64be(Writer* w, uint64_t v) {
        uint8_t b[8];
        int     i;
        for (i = 0; i < 8; i++) b[7 - i] = (uint8_t)(v >> (i * 8));
        w->write_fn(w->ctx, b, 8);
}
static inline bool io_read_u16be(Reader* r, uint16_t* v) {
        uint8_t b[2];
        if (r->read_fn(r->ctx, b, 2) != 2) return false;
        *v = ((uint16_t)b[0] << 8) | (uint16_t)b[1];
        return true;
}
static inline bool io_read_u32be(Reader* r, uint32_t* v) {
        uint8_t b[4];
        if (r->read_fn(r->ctx, b, 4) != 4) return false;
        *v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8) | (uint32_t)b[3];
        return true;
}
static inline bool io_read_u64be(Reader* r, uint64_t* v) {
        uint8_t b[8];
        int     i;
        if (r->read_fn(r->ctx, b, 8) != 8) return false;
        *v = 0;
        for (i = 0; i < 8; i++) *v = (*v << 8) | b[i];
        return true;
}

/* ================================================================
 *  Signed integer aliases
 * ================================================================ */
static inline void io_write_i8(Writer* w, int8_t v) {
        io_write_u8(w, (uint8_t)v);
}
static inline void io_write_i16le(Writer* w, int16_t v) {
        io_write_u16le(w, (uint16_t)v);
}
static inline void io_write_i32le(Writer* w, int32_t v) {
        io_write_u32le(w, (uint32_t)v);
}
static inline void io_write_i64le(Writer* w, int64_t v) {
        io_write_u64le(w, (uint64_t)v);
}
static inline void io_write_i16be(Writer* w, int16_t v) {
        io_write_u16be(w, (uint16_t)v);
}
static inline void io_write_i32be(Writer* w, int32_t v) {
        io_write_u32be(w, (uint32_t)v);
}
static inline void io_write_i64be(Writer* w, int64_t v) {
        io_write_u64be(w, (uint64_t)v);
}
static inline bool io_read_i8(Reader* r, int8_t* v) {
        return io_read_u8(r, (uint8_t*)v);
}
static inline bool io_read_i16le(Reader* r, int16_t* v) {
        return io_read_u16le(r, (uint16_t*)v);
}
static inline bool io_read_i32le(Reader* r, int32_t* v) {
        return io_read_u32le(r, (uint32_t*)v);
}
static inline bool io_read_i64le(Reader* r, int64_t* v) {
        return io_read_u64le(r, (uint64_t*)v);
}
static inline bool io_read_i16be(Reader* r, int16_t* v) {
        return io_read_u16be(r, (uint16_t*)v);
}
static inline bool io_read_i32be(Reader* r, int32_t* v) {
        return io_read_u32be(r, (uint32_t*)v);
}
static inline bool io_read_i64be(Reader* r, int64_t* v) {
        return io_read_u64be(r, (uint64_t*)v);
}

/* ================================================================
 *  Pascal string: u16le length prefix + bytes (no NUL)
 * ================================================================ */
static inline void io_write_pstr16(Writer* w, Str s) {
        uint16_t len = (uint16_t)(s.len < 65535 ? s.len : 65535);
        io_write_u16le(w, len);
        w->write_fn(w->ctx, s.ptr, len);
}
static inline Str io_read_pstr16(Reader* r, Arena* a) {
        uint16_t len;
        if (!io_read_u16le(r, &len)) return str_null();
        char* buf = arena_push_array(a, char, len + 1);
        if (r->read_fn(r->ctx, buf, len) != len) return str_null();
        buf[len] = '\0';
        return str_buf(buf, len);
}

/* ================================================================
 *  Convenience — stdout / stderr
 * ================================================================ */
void io_print(Str s);
void io_println(Str s);
void io_eprint(Str s);
void io_eprintln(Str s);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREIO_IMPLEMENTATION
#        include <stdarg.h>
#        include <stdio.h>
#        include <string.h>

/* ---- FILE* backend -------------------------------------------- */
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
        Reader r = {f, _file_read_fn, NULL};
        return r;
}
Writer writer_from_file(FILE* f) {
        Writer w = {f, _file_write_fn, NULL};
        return w;
}

/* ---- Memory reader --------------------------------------------- */
typedef struct {
        const uint8_t* data;
        size_t         len, pos;
} _MemRCtx;
static size_t _mem_read_fn(void* ctx, void* buf, size_t n) {
        _MemRCtx* m     = (_MemRCtx*)ctx;
        size_t    avail = m->len - m->pos;
        size_t    got   = n < avail ? n : avail;
        memcpy(buf, m->data + m->pos, got);
        m->pos += got;
        return got;
}
Reader reader_from_mem(Arena* a, const void* data, size_t len) {
        _MemRCtx* ctx = arena_push_type(a, _MemRCtx);
        ctx->data     = (const uint8_t*)data;
        ctx->len      = len;
        ctx->pos      = 0;
        Reader r      = {ctx, _mem_read_fn, NULL};
        return r;
}

/* ---- Memory writer --------------------------------------------- */
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
        Writer w  = {mw, _mem_write_fn, NULL};
        return w;
}
Str mem_writer_result(MemWriter* mw) {
        return str_buf(mw->buf, mw->len);
}

/* ---- Tee writer ------------------------------------------------ */
typedef struct {
        Writer* a;
        Writer* b;
} _TeeCtx;
static void _tee_write_fn(void* ctx, const void* buf, size_t n) {
        _TeeCtx* t = (_TeeCtx*)ctx;
        t->a->write_fn(t->a->ctx, buf, n);
        t->b->write_fn(t->b->ctx, buf, n);
}
Writer writer_tee(Arena* a, Writer* w1, Writer* w2) {
        _TeeCtx* ctx = arena_push_type(a, _TeeCtx);
        ctx->a       = w1;
        ctx->b       = w2;
        Writer w     = {ctx, _tee_write_fn, NULL};
        return w;
}

/* ---- Null writer ----------------------------------------------- */
static void _null_write_fn(void* ctx, const void* buf, size_t n) {
        Unused(ctx);
        Unused(buf);
        Unused(n);
}
Writer writer_null(void) {
        Writer w = {NULL, _null_write_fn, NULL};
        return w;
}

/* ---- OS file backend (requires bareos.h) ----------------------- */
#        ifdef BAREOS_H
static size_t _osfile_read_fn(void* ctx, void* buf, size_t n) {
        ssize_t r = read(((File*)ctx)->fd, buf, n);
        assert(r >= 0 && "io: read error");
        return r < 0 ? 0 : (size_t)r;
}
static void _osfile_write_fn(void* ctx, const void* buf, size_t n) {
        ssize_t w = write(((File*)ctx)->fd, buf, n);
        assert((size_t)w == n && "io: write error");
}
Reader reader_from_osfile(File* f) {
        Reader r = {f, _osfile_read_fn, NULL};
        return r;
}
Writer writer_from_osfile(File* f) {
        Writer w = {f, _osfile_write_fn, NULL};
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
#        endif

/* ---- Buffered reader ------------------------------------------- */
BufReader* bufreader_make(Arena* a, Reader src, size_t cap) {
        if (!cap) cap = KB(8);
        BufReader* br = arena_push_type(a, BufReader);
        br->src       = src;
        br->buf       = arena_push_array(a, uint8_t, cap);
        br->cap       = cap;
        br->pos = br->fill = 0;
        br->eof            = false;
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
                size_t avail = br->fill - br->pos, take = n < avail ? n : avail;
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

/* ---- Buffered writer ------------------------------------------- */
BufWriter* bufwriter_make(Arena* a, Writer dst, size_t cap) {
        if (!cap) cap = KB(8);
        BufWriter* bw     = arena_push_type(a, BufWriter);
        bw->dst           = dst;
        bw->buf           = arena_push_array(a, uint8_t, cap);
        bw->cap           = cap;
        bw->pos           = 0;
        bw->line_buffered = false;
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
                /* line-buffered: flush on newline */
                if (bw->line_buffered) {
                        size_t i;
                        for (i = bw->pos - put; i < bw->pos; i++) {
                                if (bw->buf[i] == '\n') {
                                        buf_flush(bw);
                                        break;
                                }
                        }
                }
        }
}
void buf_write_str(BufWriter* bw, Str s) {
        buf_write(bw, s.ptr, s.len);
}

/* ---- Raw dispatch --------------------------------------------- */
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
        Slice(char) buf = slice_make(a, char, KB(4));
        uint8_t tmp[KB(4)];
        size_t  got, i;
        while ((got = r->read_fn(r->ctx, tmp, sizeof tmp)) > 0)
                for (i = 0; i < got; i++) slice_push(buf, char, (char)tmp[i]);
        return str_buf(buf, slice_len(buf));
}

void io_read_exactly(Reader* r, void* buf, size_t n) {
        uint8_t* p   = (uint8_t*)buf;
        size_t   got = 0;
        while (got < n) {
                size_t chunk = r->read_fn(r->ctx, p + got, n - got);
                assert(chunk > 0 && "io_read_exactly: unexpected EOF");
                got += chunk;
        }
}

size_t io_copy(Reader* r, Writer* w) {
        uint8_t tmp[KB(4)];
        size_t  total = 0, got;
        while ((got = r->read_fn(r->ctx, tmp, sizeof tmp)) > 0) {
                w->write_fn(w->ctx, tmp, got);
                total += got;
        }
        return total;
}

void io_printf(Arena* scratch, Writer* w, const char* fmt, ...) {
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        if (n <= 0) {
                va_end(ap2);
                return;
        }
        char* buf = arena_push_array(scratch, char, (size_t)n + 1);
        vsnprintf(buf, (size_t)n + 1, fmt, ap2);
        va_end(ap2);
        w->write_fn(w->ctx, buf, (size_t)n);
}

/* ---- Convenience --------------------------------------------- */
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

#endif /* BAREIO_IMPLEMENTATION */
#endif /* BAREIO_H */
