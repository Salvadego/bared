/*
 * strbuilder.h - Arena-backed string builder
 * ============================================
 *
 *  USAGE
 *    #define STRBUILDER_IMPLEMENTATION
 *    #include "strbuilder.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena, Scratch, Slice, Str)
 *
 *  DESIGN
 *    StrBuilder is a Slice(char) with a Scratch save-point taken at
 *    sb_make time.  All growth lives in the scratch region - no
 *    permanent arena cost until you call sb_to_cstr().
 *
 *    sb_to_str()      - non-owning Str view; scratch still live.
 *    sb_to_cstr(a)    - copies len+1 bytes into arena a, then calls
 *                       scratch_end() to release the scratch buffer.
 *                       The builder is reset to empty afterwards.
 *    sb_reset()       - scratch_end + re-save; fast reuse, no alloc.
 *
 *  EXAMPLE
 *
 *    Arena     *a  = arena_new(KB(64));
 *    StrBuilder sb = sb_make(a, 256);
 *
 *    sb_write_str (&sb, str_lit("hello"));
 *    sb_write_char(&sb, ',');
 *    sb_write_fmt (&sb, " world %d", 42);
 *
 *    char *result = sb_to_cstr(a, &sb);  // "hello, world 42"
 *    // scratch buffer is gone; result lives in a
 */

#ifndef STRBUILDER_H
#define STRBUILDER_H

#include <stdarg.h>

#include "barestd.h"

typedef struct {
        Slice(char) buf; /* shadow header carries arena, len, cap, stride */
        Scratch mark;    /* save point in arena taken at sb_make time     */
} StrBuilder;

/* Allocate a builder inside a scratch region of a. */
StrBuilder sb_make(Arena* a, size_t initial_cap);

void sb_write_char(StrBuilder* sb, char c);
void sb_write_str(StrBuilder* sb, Str s);
void sb_write_cstr(StrBuilder* sb, const char* s);
void sb_write_fmt(StrBuilder* sb, const char* fmt, ...);
void sb_write_repeat(StrBuilder* sb, char c, size_t n);

#ifdef UTF8_H
/* Append a single Rune (1-4 bytes) to the builder. */
void sb_write_rune(StrBuilder* sb, Rune r);
#endif

/* Non-owning view into the scratch buffer. */
Str sb_to_str(StrBuilder* sb);

/*
 * Copy accumulated bytes + NUL into arena a, rewind the scratch
 * region, and reset the builder to empty.
 * The returned pointer is stable for the lifetime of a.
 */
char* sb_to_cstr(Arena* a, StrBuilder* sb);

/*
 * Discard accumulated content and reset to empty without copying.
 * The scratch region is rewound and re-saved.
 */
void sb_reset(StrBuilder* sb);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef STRBUILDER_IMPLEMENTATION

#        include <stdarg.h>
#        include <stdio.h>
#        include <string.h>

StrBuilder sb_make(Arena* a, size_t initial_cap) {
        StrBuilder sb;
        sb.mark = scratch_begin(a);
        sb.buf  = slice_make(a, char, initial_cap ? initial_cap : 256);
        return sb;
}

void sb_write_char(StrBuilder* sb, char c) {
        slice_push(sb->buf, char, c);
}

void sb_write_str(StrBuilder* sb, Str s) {
        size_t i;
        for (i = 0; i < s.len; i++) slice_push(sb->buf, char, s.ptr[i]);
}

void sb_write_cstr(StrBuilder* sb, const char* s) {
        sb_write_str(sb, str_from_c(s));
}

void sb_write_fmt(StrBuilder* sb, const char* fmt, ...) {
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        assert(n >= 0 && "sb_write_fmt: vsnprintf failed");
        /* write into a small stack buffer to avoid double-formatting */
        char  tmp[256];
        char* dst  = tmp;
        char* heap = NULL;
        Unused(heap);
        if ((size_t)n + 1 > sizeof tmp) {
                dst = (char*)arena_push(sb->mark.a, (size_t)n + 1, 1);
        }
        vsnprintf(dst, (size_t)n + 1, fmt, ap2);
        va_end(ap2);
        size_t i;
        for (i = 0; i < (size_t)n; i++) slice_push(sb->buf, char, dst[i]);
}

void sb_write_repeat(StrBuilder* sb, char c, size_t n) {
        size_t i;
        for (i = 0; i < n; i++) slice_push(sb->buf, char, c);
}

#        ifdef UTF8_H
void sb_write_rune(StrBuilder* sb, Rune r) {
        int i, n = utf8_width(r);
        for (i = 0; i < n; i++) slice_push(sb->buf, char, (char)r.bytes[i]);
}
#        endif

Str sb_to_str(StrBuilder* sb) {
        return str_buf(sb->buf, slice_len(sb->buf));
}

char* sb_to_cstr(Arena* a, StrBuilder* sb) {
        size_t len = slice_len(sb->buf);

#        if defined(_MSC_VER)
        char* tmp = (char*)malloc(len + 1);
        assert(tmp && "sb_to_cstr: malloc failed");
#        else
        char tmp[len + 1];  // us a vla to store
#        endif

        memcpy(tmp, sb->buf, len);
        tmp[len] = '\0';

        scratch_end(sb->mark);
        sb->mark = scratch_begin(sb->mark.a);
        sb->buf  = slice_make(sb->mark.a, char, 256);

        char* out = arena_push_array(a, char, len + 1);
        memcpy(out, tmp, len + 1);

#        if defined(_MSC_VER)
        free(tmp);
#        endif

        return out;
}

void sb_reset(StrBuilder* sb) {
        Arena* a = sb->mark.a;
        scratch_end(sb->mark);
        sb->mark = scratch_begin(a);
        sb->buf  = slice_make(a, char, 256);
}

#endif /* STRBUILDER_IMPLEMENTATION */
#endif /* STRBUILDER_H */
