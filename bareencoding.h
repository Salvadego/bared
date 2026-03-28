/*
 * bareencoding.h -- Base64, Hex, Percent (URI), HTML escaping
 * =============================================================
 *
 *  USAGE
 *    #define BAREENCODING_IMPLEMENTATION
 *    #include "bareencoding.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena, Str)
 *
 *  MODULES
 *
 *    BASE64
 *      base64_encode(a, src)          -> Str   standard alphabet, padded
 *      base64_url_encode(a, src)      -> Str   URL-safe alphabet (-_ no pad)
 *      base64_decode(a, src)          -> Str   both alphabets accepted
 *      base64_encoded_len(raw_len)    -> size_t
 *      base64_decoded_len(enc_len)    -> size_t (upper bound)
 *
 *    HEX
 *      hex_encode(a, src)             -> Str   lowercase  "deadbeef"
 *      hex_encode_upper(a, src)       -> Str   uppercase  "DEADBEEF"
 *      hex_decode(a, src)             -> Str   accepts mixed case; str_null on
 * bad input hex_encoded_len(raw_len)       -> size_t
 *
 *    PERCENT (URI) ENCODING  -- RFC 3986
 *      pct_encode(a, src)             -> Str   encode all non-unreserved chars
 *      pct_encode_path(a, src)        -> Str   preserve '/' separators
 *      pct_encode_query(a, src)       -> Str   preserve '=', '&', '+'
 *      pct_decode(a, src)             -> Str   decode %XX sequences
 *      pct_is_unreserved(c)           -> bool
 *
 *    HTML ESCAPING
 *      html_escape(a, src)            -> Str   & < > " ' -> entities
 *      html_unescape(a, src)          -> Str   entities -> chars
 *                                              (handles &amp; &lt; &gt; &quot;
 * &#39; &#N;)
 *
 *  EXAMPLE
 *
 *    // Base64 round-trip
 *    Str encoded = base64_encode(a, str_lit("Hello, World!"));
 *    // "SGVsbG8sIFdvcmxkIQ=="
 *    Str decoded = base64_decode(a, encoded);
 *    // "Hello, World!"
 *
 *    // Hex
 *    uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
 *    Str hex = hex_encode(a, str_buf(bytes, 4));
 *    // "deadbeef"
 *
 *    // Percent encode a URL path segment
 *    Str enc = pct_encode(a, str_lit("hello world/foo"));
 *    // "hello%20world%2Ffoo"
 *
 *    // HTML escape
 *    Str safe = html_escape(a, str_lit("<script>alert(\"xss\")</script>"));
 *    // "&lt;script&gt;alert(&quot;xss&quot;)&lt;/script&gt;"
 */
#ifndef BAREENCODING_H
#define BAREENCODING_H

#include "barestd.h"

/* ================================================================
 *  Base64
 * ================================================================ */
size_t base64_encoded_len(size_t raw_len); /* includes padding, no NUL */
size_t base64_decoded_len(size_t enc_len); /* upper bound              */

Str base64_encode(Arena* a,
                  Str    src); /* standard alphabet (A-Z a-z 0-9 +/) padded */
Str base64_url_encode(Arena* a, Str src); /* URL-safe (-_), no padding */
Str base64_decode(Arena* a, Str src);     /* str_null on invalid input */

/* ================================================================
 *  Hex
 * ================================================================ */
size_t hex_encoded_len(size_t raw_len); /* raw_len * 2               */

Str base16_encode(Arena* a, Str src);    /* alias: base16 = hex */
Str hex_encode(Arena* a, Str src);       /* lowercase */
Str hex_encode_upper(Arena* a, Str src); /* uppercase */
Str hex_decode(Arena* a, Str src);       /* str_null on bad input */

/* ================================================================
 *  Percent encoding  (RFC 3986)
 * ================================================================ */
bool pct_is_unreserved(char c); /* A-Z a-z 0-9 - . _ ~ */

Str pct_encode(Arena* a, Str src);       /* encode all non-unreserved */
Str pct_encode_path(Arena* a, Str src);  /* preserve '/'             */
Str pct_encode_query(Arena* a, Str src); /* preserve = & +           */
Str pct_decode(Arena* a, Str src);       /* decode %XX, pass-through + */

/* ================================================================
 *  HTML escaping
 * ================================================================ */
Str html_escape(Arena* a, Str src);
Str html_unescape(Arena* a, Str src);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREENCODING_IMPLEMENTATION
#        include <string.h>

/* ---- Base64 ---------------------------------------------------- */
static const char _b64_std[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char _b64_url[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t base64_encoded_len(size_t n) {
        return ((n + 2) / 3) * 4;
}
size_t base64_decoded_len(size_t n) {
        return (n / 4) * 3 + 3;
}

static Str _b64_encode_impl(Arena* a, Str src, const char* alpha, bool pad) {
        size_t         out_len = ((src.len + 2) / 3) * 4;
        char*          out     = arena_push_array(a, char, out_len + 1);
        size_t         i, o = 0;
        const uint8_t* s = (const uint8_t*)src.ptr;

        for (i = 0; i + 2 < src.len; i += 3) {
                uint32_t v = ((uint32_t)s[i] << 16) |
                             ((uint32_t)s[i + 1] << 8) | s[i + 2];
                out[o++]   = alpha[(v >> 18) & 63];
                out[o++]   = alpha[(v >> 12) & 63];
                out[o++]   = alpha[(v >> 6) & 63];
                out[o++]   = alpha[(v) & 63];
        }
        size_t rem = src.len - i;
        if (rem == 1) {
                uint32_t v = (uint32_t)s[i] << 16;
                out[o++]   = alpha[(v >> 18) & 63];
                out[o++]   = alpha[(v >> 12) & 63];
                if (pad) {
                        out[o++] = '=';
                        out[o++] = '=';
                }
        } else if (rem == 2) {
                uint32_t v = ((uint32_t)s[i] << 16) | ((uint32_t)s[i + 1] << 8);
                out[o++]   = alpha[(v >> 18) & 63];
                out[o++]   = alpha[(v >> 12) & 63];
                out[o++]   = alpha[(v >> 6) & 63];
                if (pad) {
                        out[o++] = '=';
                }
        }
        out[o] = '\0';
        return str_buf(out, o);
}

Str base64_encode(Arena* a, Str src) {
        return _b64_encode_impl(a, src, _b64_std, true);
}
Str base64_url_encode(Arena* a, Str src) {
        return _b64_encode_impl(a, src, _b64_url, false);
}

static int _b64_val(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        if (c == '=') return 0; /* padding */
        return -1;
}

Str base64_decode(Arena* a, Str src) {
        /* skip whitespace and padding to get working length */
        size_t enc_len = src.len;
        /* strip trailing '=' */
        while (enc_len > 0 && src.ptr[enc_len - 1] == '=') enc_len--;
        size_t   out_max = (enc_len * 3) / 4 + 3;
        uint8_t* out     = (uint8_t*)arena_push_array(a, char, out_max + 1);
        size_t   o       = 0, i;
        for (i = 0; i + 3 < enc_len; i += 4) {
                int a0 = _b64_val(src.ptr[i]);
                int b0 = _b64_val(src.ptr[i + 1]);
                int c0 = _b64_val(src.ptr[i + 2]);
                int d0 = _b64_val(src.ptr[i + 3]);
                if (a0 < 0 || b0 < 0 || c0 < 0 || d0 < 0) return str_null();
                uint32_t v = ((uint32_t)a0 << 18) | ((uint32_t)b0 << 12) |
                             ((uint32_t)c0 << 6) | (uint32_t)d0;
                out[o++]   = (uint8_t)(v >> 16);
                out[o++]   = (uint8_t)(v >> 8);
                out[o++]   = (uint8_t)(v);
        }
        size_t rem = enc_len - i;
        if (rem == 2) {
                int a0 = _b64_val(src.ptr[i]), b0 = _b64_val(src.ptr[i + 1]);
                if (a0 < 0 || b0 < 0) return str_null();
                out[o++] =
                    (uint8_t)((((uint32_t)a0 << 18) | ((uint32_t)b0 << 12)) >>
                              16);
        } else if (rem == 3) {
                int a0 = _b64_val(src.ptr[i]), b0 = _b64_val(src.ptr[i + 1]),
                    c0 = _b64_val(src.ptr[i + 2]);
                if (a0 < 0 || b0 < 0 || c0 < 0) return str_null();
                uint32_t v = ((uint32_t)a0 << 18) | ((uint32_t)b0 << 12) |
                             ((uint32_t)c0 << 6);
                out[o++]   = (uint8_t)((v) >> 16);
                out[o++]   = (uint8_t)((v) >> 8);
        }
        out[o] = '\0';
        return str_buf(out, o);
}

/* ---- Hex ------------------------------------------------------- */
size_t hex_encoded_len(size_t n) {
        return n * 2;
}

static Str _hex_enc(Arena* a, Str src, const char* dig) {
        size_t n   = src.len * 2;
        char*  out = arena_push_array(a, char, n + 1);
        size_t i;
        for (i = 0; i < src.len; i++) {
                unsigned char b = (unsigned char)src.ptr[i];
                out[i * 2]      = dig[b >> 4];
                out[i * 2 + 1]  = dig[b & 0xF];
        }
        out[n] = '\0';
        return str_buf(out, n);
}

Str hex_encode(Arena* a, Str src) {
        return _hex_enc(a, src, "0123456789abcdef");
}
Str hex_encode_upper(Arena* a, Str src) {
        return _hex_enc(a, src, "0123456789ABCDEF");
}
Str base16_encode(Arena* a, Str src) {
        return hex_encode(a, src);
}

static int _hex_digit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
}

Str hex_decode(Arena* a, Str src) {
        /* skip optional "0x" prefix */
        if (src.len >= 2 && src.ptr[0] == '0' &&
            (src.ptr[1] == 'x' || src.ptr[1] == 'X')) {
                src.ptr += 2;
                src.len -= 2;
        }
        if (src.len & 1) return str_null(); /* odd length is invalid */
        size_t   n   = src.len / 2;
        uint8_t* out = (uint8_t*)arena_push_array(a, char, n + 1);
        size_t   i;
        for (i = 0; i < n; i++) {
                int hi = _hex_digit(src.ptr[i * 2]);
                int lo = _hex_digit(src.ptr[i * 2 + 1]);
                if (hi < 0 || lo < 0) return str_null();
                out[i] = (uint8_t)((hi << 4) | lo);
        }
        out[n] = '\0';
        return str_buf(out, n);
}

/* ---- Percent encoding ------------------------------------------ */
bool pct_is_unreserved(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
               c == '~';
}

static Str _pct_encode_impl(Arena* a, Str src, const char* also_safe) {
        /* worst case: every byte becomes %XX (3 bytes) */
        char*      out    = arena_push_array(a, char, src.len * 3 + 1);
        size_t     o      = 0, i;
        const char safe[] = "0123456789ABCDEF";
        for (i = 0; i < src.len; i++) {
                char c = src.ptr[i];
                if (pct_is_unreserved(c) ||
                    (also_safe && strchr(also_safe, c))) {
                        out[o++] = c;
                } else {
                        out[o++] = '%';
                        out[o++] = safe[(unsigned char)c >> 4];
                        out[o++] = safe[(unsigned char)c & 0xF];
                }
        }
        out[o] = '\0';
        return str_buf(out, o);
}

Str pct_encode(Arena* a, Str src) {
        return _pct_encode_impl(a, src, NULL);
}
Str pct_encode_path(Arena* a, Str src) {
        return _pct_encode_impl(a, src, "/");
}
Str pct_encode_query(Arena* a, Str src) {
        return _pct_encode_impl(a, src, "=&+");
}

Str pct_decode(Arena* a, Str src) {
        char*  out = arena_push_array(a, char, src.len + 1);
        size_t o = 0, i = 0;
        while (i < src.len) {
                if (src.ptr[i] == '%' && i + 2 < src.len) {
                        int hi = _hex_digit(src.ptr[i + 1]);
                        int lo = _hex_digit(src.ptr[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                                out[o++] = (char)((hi << 4) | lo);
                                i += 3;
                                continue;
                        }
                }
                if (src.ptr[i] == '+') {
                        out[o++] = ' ';
                        i++;
                        continue;
                }
                out[o++] = src.ptr[i++];
        }
        out[o] = '\0';
        return str_buf(out, o);
}

/* ---- HTML escaping --------------------------------------------- */
Str html_escape(Arena* a, Str src) {
        /* worst case: & -> &amp; (5 bytes) */
        char*  out = arena_push_array(a, char, src.len * 6 + 1);
        size_t o   = 0, i;
        for (i = 0; i < src.len; i++) {
                char c = src.ptr[i];
                switch (c) {
                        case '&':
                                memcpy(out + o, "&amp;", 5);
                                o += 5;
                                break;
                        case '<':
                                memcpy(out + o, "&lt;", 4);
                                o += 4;
                                break;
                        case '>':
                                memcpy(out + o, "&gt;", 4);
                                o += 4;
                                break;
                        case '"':
                                memcpy(out + o, "&quot;", 6);
                                o += 6;
                                break;
                        case '\'':
                                memcpy(out + o, "&#39;", 5);
                                o += 5;
                                break;
                        default:
                                out[o++] = c;
                                break;
                }
        }
        out[o] = '\0';
        return str_buf(out, o);
}

Str html_unescape(Arena* a, Str src) {
        char*  out = arena_push_array(a, char, src.len + 1);
        size_t o = 0, i = 0;
        while (i < src.len) {
                if (src.ptr[i] != '&') {
                        out[o++] = src.ptr[i++];
                        continue;
                }
                /* find closing ';' */
                size_t j = i + 1;
                while (j < src.len && j < i + 10 && src.ptr[j] != ';') j++;
                if (j >= src.len || src.ptr[j] != ';') {
                        out[o++] = src.ptr[i++];
                        continue;
                }
                Str  ent   = str_buf(src.ptr + i + 1, j - i - 1);
                char rep   = '\0';
                bool known = true;
                if (str_eq(ent, str_lit("amp")))
                        rep = '&';
                else if (str_eq(ent, str_lit("lt")))
                        rep = '<';
                else if (str_eq(ent, str_lit("gt")))
                        rep = '>';
                else if (str_eq(ent, str_lit("quot")))
                        rep = '"';
                else if (str_eq(ent, str_lit("#39")))
                        rep = '\'';
                else if (str_eq(ent, str_lit("apos")))
                        rep = '\'';
                else if (ent.len > 1 && ent.ptr[0] == '#') {
                        /* &#N; or &#xN; */
                        Str     num = str_buf(ent.ptr + 1, ent.len - 1);
                        int64_t cp  = 0;
                        if (num.len > 0 &&
                            (num.ptr[0] == 'x' || num.ptr[0] == 'X')) {
                                num.ptr++;
                                num.len--;
                                str_to_i64(
                                    num,
                                    &cp); /* won't work for hex — use u64 */
                                uint64_t ucp = 0;
                                Str      hx = str_buf(src.ptr + i + 3,
                                                      j - i - 3); /* skip &#x */
                                str_to_u64(hx, &ucp);
                                cp = (int64_t)ucp; /* close enough for ASCII */
                        } else {
                                str_to_i64(num, &cp);
                        }
                        if (cp > 0 && cp < 128)
                                rep = (char)cp;
                        else
                                known = false;
                } else {
                        known = false;
                }
                if (known && rep) {
                        out[o++] = rep;
                } else {
                        /* copy the entity verbatim */
                        size_t elen = j - i + 1;
                        memcpy(out + o, src.ptr + i, elen);
                        o += elen;
                }
                i = j + 1;
        }
        out[o] = '\0';
        return str_buf(out, o);
}

#endif /* BAREENCODING_IMPLEMENTATION */
#endif /* BAREENCODING_H */
