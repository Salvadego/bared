/*
 * utf8.h - Rune type, UTF-8 decode / encode, iteration
 * ======================================================
 *
 *  USAGE
 *    #define BAREUTF8_IMPLEMENTATION
 *    #include "utf8.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Str, StaticAssert)
 *
 *  DESIGN
 *    Rune is a union of:
 *      int32_t  cp       - the Unicode codepoint (compare, classify)
 *      uint8_t  bytes[4] - UTF-8 encoded bytes   (write directly)
 *    Both fields are always valid after utf8_decode / utf8_encode.
 *    sizeof(Rune) == sizeof(int32_t) == 4.  Punning is valid C99.
 *
 *  EXAMPLE
 *
 *    Str      s  = str_lit("héllo 🌍");
 *    Utf8Iter it = utf8_iter(s);
 *    Rune     r;
 *    while (utf8_next(&it, &r))
 *        fwrite(r.bytes, 1, (size_t)utf8_width(r), stdout);
 *
 *    printf("runes=%zu bytes=%zu\n", utf8_len(s), s.len);
 *
 *  ENCODING
 *    To encode a known codepoint:
 *      Rune r;
 *      int  n = utf8_encode(&r, 0x1F30D);   // 🌍
 *      fwrite(r.bytes, 1, (size_t)n, f);
 */

#ifndef BAREUTF8_H
#define BAREUTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "barestd.h"

/* ================================================================
 *  Rune
 * ================================================================ */

typedef struct {
        int32_t cp;       /* codepoint  - compare / classify / store */
        uint8_t bytes[4]; /* UTF-8 bytes - write directly to output   */
} Rune;

#define RUNE_ERROR ((int32_t)0xFFFD)   /* U+FFFD replacement character  */
#define RUNE_EOF   ((int32_t)-1)       /* returned when input exhausted */
#define RUNE_MAX   ((int32_t)0x10FFFF) /* largest valid codepoint       */

/* ================================================================
 *  Encode / decode
 * ================================================================ */

/*
 * Decode the first codepoint from s[0..len).
 * Populates both r->cp and r->bytes.
 * Returns bytes consumed (1-4).
 * On invalid input: r->cp = RUNE_ERROR, 1 byte consumed.
 * On empty input:   r->cp = RUNE_EOF,   0 returned.
 */
int utf8_decode(const char* s, size_t len, Rune* r);

/*
 * Encode codepoint cp into r->bytes and set r->cp = cp.
 * Returns number of bytes written (1-4).
 * Encodes RUNE_ERROR for invalid / surrogate codepoints.
 */
int utf8_encode(Rune* r, int32_t cp);

/* Encoded byte width of r (1-4). Valid after decode/encode. */
int utf8_width(Rune r);

/* Count runes (codepoints), not bytes. */
size_t utf8_len(Str s);

/* ================================================================
 *  Utf8Iter - zero-allocation cursor over a Str
 * ================================================================ */

typedef struct {
        const char* ptr;
        size_t      remaining;
} Utf8Iter;

Utf8Iter utf8_iter(Str s);

/*
 * Advance iterator, populate *r.
 * Returns true while runes remain; false on EOF (r->cp == RUNE_EOF).
 */
bool utf8_next(Utf8Iter* it, Rune* r);

/* ================================================================
 *  Rune predicates  (full accuracy for ASCII range;
 *  non-ASCII codepoints return false for all ASCII predicates)
 * ================================================================ */

bool rune_is_valid(Rune r);
bool rune_is_ascii(Rune r);
bool rune_is_digit(Rune r);
bool rune_is_upper(Rune r);
bool rune_is_lower(Rune r);
bool rune_is_alpha(Rune r);
bool rune_is_alnum(Rune r);
bool rune_is_space(Rune r);
bool rune_is_punct(Rune r);

/* ASCII case conversion; non-ASCII runes returned unchanged. */
Rune rune_to_upper(Rune r);
Rune rune_to_lower(Rune r);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREUTF8_IMPLEMENTATION

#        include <string.h>

/* Internal: encode cp into r->bytes only, returns width.
   Does NOT set r->cp - caller handles that. */
static int _rune_encode_bytes(Rune* r, int32_t cp) {
        memset(r->bytes, 0, 4);
        if (cp < 0x80) {
                r->bytes[0] = (uint8_t)cp;
                return 1;
        }
        if (cp < 0x800) {
                r->bytes[0] = (uint8_t)(0xC0 | (cp >> 6));
                r->bytes[1] = (uint8_t)(0x80 | (cp & 0x3F));
                return 2;
        }
        if (cp < 0x10000) {
                r->bytes[0] = (uint8_t)(0xE0 | (cp >> 12));
                r->bytes[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                r->bytes[2] = (uint8_t)(0x80 | (cp & 0x3F));
                return 3;
        }
        r->bytes[0] = (uint8_t)(0xF0 | (cp >> 18));
        r->bytes[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        r->bytes[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        r->bytes[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
}

static void _rune_set_error(Rune* r) {
        r->cp = RUNE_ERROR;
        _rune_encode_bytes(r, RUNE_ERROR);
}

int utf8_decode(const char* s, size_t len, Rune* r) {
        memset(r->bytes, 0, 4);
        if (len == 0) {
                r->cp = RUNE_EOF;
                return 0;
        }

        uint8_t b0 = (uint8_t)s[0];

        if (b0 < 0x80) { /* 1-byte ASCII */
                r->bytes[0] = b0;
                r->cp       = (int32_t)b0;
                return 1;
        }
        if (b0 < 0xC0) {
                _rune_set_error(r);
                return 1;
        } /* bare continuation */

        if (b0 < 0xE0) { /* 2-byte */
                if (len < 2 || ((uint8_t)s[1] & 0xC0) != 0x80) {
                        _rune_set_error(r);
                        return 1;
                }
                int32_t cp = ((int32_t)(b0 & 0x1F) << 6) |
                             (int32_t)((uint8_t)s[1] & 0x3F);
                if (cp < 0x80) {
                        _rune_set_error(r);
                        return 1;
                } /* overlong */
                r->bytes[0] = (uint8_t)s[0];
                r->bytes[1] = (uint8_t)s[1];
                r->cp       = cp;
                return 2;
        }

        if (b0 < 0xF0) { /* 3-byte */
                if (len < 3 || ((uint8_t)s[1] & 0xC0) != 0x80 ||
                    ((uint8_t)s[2] & 0xC0) != 0x80) {
                        _rune_set_error(r);
                        return 1;
                }
                int32_t cp = ((int32_t)(b0 & 0x0F) << 12) |
                             ((int32_t)((uint8_t)s[1] & 0x3F) << 6) |
                             (int32_t)((uint8_t)s[2] & 0x3F);
                if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                        _rune_set_error(r);
                        return 1;
                }
                r->bytes[0] = (uint8_t)s[0];
                r->bytes[1] = (uint8_t)s[1];
                r->bytes[2] = (uint8_t)s[2];
                r->cp       = cp;
                return 3;
        }

        if (b0 < 0xF8) { /* 4-byte */
                if (len < 4 || ((uint8_t)s[1] & 0xC0) != 0x80 ||
                    ((uint8_t)s[2] & 0xC0) != 0x80 ||
                    ((uint8_t)s[3] & 0xC0) != 0x80) {
                        _rune_set_error(r);
                        return 1;
                }
                int32_t cp = ((int32_t)(b0 & 0x07) << 18) |
                             ((int32_t)((uint8_t)s[1] & 0x3F) << 12) |
                             ((int32_t)((uint8_t)s[2] & 0x3F) << 6) |
                             (int32_t)((uint8_t)s[3] & 0x3F);
                if (cp < 0x10000 || cp > RUNE_MAX) {
                        _rune_set_error(r);
                        return 1;
                }
                r->bytes[0] = (uint8_t)s[0];
                r->bytes[1] = (uint8_t)s[1];
                r->bytes[2] = (uint8_t)s[2];
                r->bytes[3] = (uint8_t)s[3];
                r->cp       = cp;
                return 4;
        }

        _rune_set_error(r);
        return 1;
}

int utf8_encode(Rune* r, int32_t cp) {
        if (cp < 0 || cp > RUNE_MAX || (cp >= 0xD800 && cp <= 0xDFFF)) {
                r->cp = RUNE_ERROR;
                return _rune_encode_bytes(r, RUNE_ERROR);
        }
        r->cp = cp;
        return _rune_encode_bytes(r, cp);
}

int utf8_width(Rune r) {
        if (r.cp < 0) return 0;
        if (r.cp < 0x80) return 1;
        if (r.cp < 0x800) return 2;
        if (r.cp < 0x10000) return 3;
        return 4;
}

size_t utf8_len(Str s) {
        size_t count = 0, i = 0;
        while (i < s.len) {
                Rune r;
                int  n = utf8_decode(s.ptr + i, s.len - i, &r);
                if (n <= 0) break;
                i += (size_t)n;
                count++;
        }
        return count;
}

Utf8Iter utf8_iter(Str s) {
        Utf8Iter it;
        it.ptr       = s.ptr;
        it.remaining = s.len;
        return it;
}

bool utf8_next(Utf8Iter* it, Rune* r) {
        if (it->remaining == 0) {
                r->cp = RUNE_EOF;
                return false;
        }
        int n = utf8_decode(it->ptr, it->remaining, r);
        if (n <= 0) return false;
        it->ptr += n;
        it->remaining -= (size_t)n;
        return true;
}

bool rune_is_valid(Rune r) {
        return r.cp >= 0 && r.cp <= RUNE_MAX &&
               !(r.cp >= 0xD800 && r.cp <= 0xDFFF);
}
bool rune_is_ascii(Rune r) {
        return r.cp >= 0 && r.cp < 0x80;
}
bool rune_is_digit(Rune r) {
        return r.cp >= '0' && r.cp <= '9';
}
bool rune_is_upper(Rune r) {
        return r.cp >= 'A' && r.cp <= 'Z';
}
bool rune_is_lower(Rune r) {
        return r.cp >= 'a' && r.cp <= 'z';
}
bool rune_is_alpha(Rune r) {
        return rune_is_upper(r) || rune_is_lower(r);
}
bool rune_is_alnum(Rune r) {
        return rune_is_alpha(r) || rune_is_digit(r);
}
bool rune_is_space(Rune r) {
        return r.cp == ' ' || r.cp == '\t' || r.cp == '\n' || r.cp == '\r' ||
               r.cp == '\f' || r.cp == '\v';
}
bool rune_is_punct(Rune r) {
        return (r.cp >= '!' && r.cp <= '/') || (r.cp >= ':' && r.cp <= '@') ||
               (r.cp >= '[' && r.cp <= '`') || (r.cp >= '{' && r.cp <= '~');
}

Rune rune_to_upper(Rune r) {
        if (rune_is_lower(r)) {
                Rune out;
                utf8_encode(&out, r.cp - ('a' - 'A'));
                return out;
        }
        return r;
}
Rune rune_to_lower(Rune r) {
        if (rune_is_upper(r)) {
                Rune out;
                utf8_encode(&out, r.cp + ('a' - 'A'));
                return out;
        }
        return r;
}

#endif /* BAREUTF8_IMPLEMENTATION */
#endif /* BAREUTF8_H */
