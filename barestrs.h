/*
 * strings.h - Higher-level string operations
 * ============================================
 *
 *  USAGE
 *    #define BARESTRS_IMPLEMENTATION
 *    #include "barestrs.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Str, Arena, Slice, Scratch)
 *
 *  OPTIONAL
 *    bareutf8.h - include before strings.h to unlock rune-aware functions:
 *               str_contains_rune(), str_index_rune(),
 *               str_map_rune(), str_to_valid_utf8()
 *
 *  DESIGN
 *    - View functions (trim, cut, prefix/suffix) never allocate.
 *    - Build functions (join, repeat, replace, case) allocate exactly
 *      once using two-pass sizing - count first, then fill.
 *    - Split functions return Slice(Str) of views into the original s.
 *    - Predicates follow <ctype.h> convention: int (*pred)(int c).
 *
 *  EXAMPLE
 *    Slice(Str) words = str_fields(a, str_lit("  hello   world  "));
 *    Str joined = str_join(a, words, str_lit(", "));  // "hello, world"
 *    StrCut cut = str_cut(str_lit("user@host"), str_lit("@"));
 *    // cut.before="user"  cut.after="host"  cut.found=true
 */

#ifndef BARESTRS_H
#define BARESTRS_H

#include <ctype.h>

#include "barestd.h"

typedef struct {
        Str  before;
        Str  after;
        bool found;
} StrCut;

/* ================================================================
 *  Search - no allocation
 * ================================================================ */
ptrdiff_t str_last_find(Str hay, Str needle);
ptrdiff_t str_index_byte(Str s, char c);
ptrdiff_t str_last_index_byte(Str s, char c);
ptrdiff_t str_index_any(Str s, Str chars);
ptrdiff_t str_last_index_any(Str s, Str chars);
ptrdiff_t str_index_func(Str s, int (*pred)(int));
ptrdiff_t str_last_index_func(Str s, int (*pred)(int));

/* ================================================================
 *  Predicates - no allocation
 * ================================================================ */
bool   str_contains_any(Str s, Str chars);
bool   str_contains_func(Str s, int (*pred)(int));
size_t str_count(Str s, Str sub);    /* empty sub -> s.len+1 */
bool   str_equal_fold(Str a, Str b); /* ASCII case-insensitive */

/* ================================================================
 *  Trim - no allocation, returns sub-views
 * ================================================================ */
Str str_trim_space(Str s);
Str str_trim_left_func(Str s, int (*pred)(int));
Str str_trim_right_func(Str s, int (*pred)(int));
Str str_trim_func(Str s, int (*pred)(int));
Str str_trim_prefix(Str s, Str prefix);
Str str_trim_suffix(Str s, Str suffix);

/* ================================================================
 *  Cut - no allocation
 *  str_cut:        split on first sep; before/after are views
 *  str_cut_prefix: strip leading prefix -> after is remainder
 *  str_cut_suffix: strip trailing suffix -> before is remainder
 *  str_cut_byte:   split on single char
 *  str_cut_func:   split on first char where pred(c) is true
 *  str_cut_any:    split on any char in set
 * ================================================================ */
StrCut str_cut(Str s, Str sep);
StrCut str_cut_prefix(Str s, Str prefix);
StrCut str_cut_suffix(Str s, Str suffix);
StrCut str_cut_byte(Str s, char c);
StrCut str_cut_func(Str s, int (*pred)(int));
StrCut str_cut_any(Str s, Str chars);

/* ================================================================
 *  Split - returns Slice(Str); elements are views into s
 * ================================================================ */
Slice(Str) str_split(Arena* a, Str s, Str sep);
Slice(Str) str_split_n(Arena* a, Str s, Str sep, int n); /* n=-1: all */
Slice(Str) str_split_after(Arena* a, Str s, Str sep);
Slice(Str) str_fields(Arena* a, Str s);
Slice(Str) str_fields_func(Arena* a, Str s, int (*pred)(int));

/* ================================================================
 *  Build - allocates into arena
 * ================================================================ */
Str str_join(Arena* a, Slice(Str) parts, Str sep);
Str str_repeat(Arena* a, Str s, size_t count);
Str str_replace(Arena* a, Str s, Str old_s, Str new_s, int n);
Str str_replace_all(Arena* a, Str s, Str old_s, Str new_s);
Str str_to_upper(Arena* a, Str s);
Str str_to_lower(Arena* a, Str s);
Str str_title(Arena* a, Str s); /* ASCII: capitalise each word */

/* ================================================================
 *  UTF-8 aware - requires bareutf8.h included first
 * ================================================================ */
#ifdef UTF8_H
bool      str_contains_rune(Str s, Rune r);
ptrdiff_t str_index_rune(Str s, Rune r);
Str       str_map_rune(Arena* a, Str s, Rune (*fn)(Rune));
Str       str_to_valid_utf8(Arena* a, Str s, Str replacement);
#endif

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARESTRS_IMPLEMENTATION

#        include <string.h>

/* ---- Search --------------------------------------------------- */

ptrdiff_t str_last_find(Str hay, Str needle) {
        if (needle.len == 0) return (ptrdiff_t)hay.len;
        if (needle.len > hay.len) return -1;
        ptrdiff_t last = -1;
        size_t    i;
        for (i = 0; i <= hay.len - needle.len; i++)
                if (memcmp(hay.ptr + i, needle.ptr, needle.len) == 0)
                        last = (ptrdiff_t)i;
        return last;
}

ptrdiff_t str_index_byte(Str s, char c) {
        size_t i;
        for (i = 0; i < s.len; i++)
                if (s.ptr[i] == c) return (ptrdiff_t)i;
        return -1;
}

ptrdiff_t str_last_index_byte(Str s, char c) {
        if (!s.len) return -1;
        size_t i = s.len;
        do {
                i--;
                if (s.ptr[i] == c) return (ptrdiff_t)i;
        } while (i);
        return -1;
}

ptrdiff_t str_index_any(Str s, Str chars) {
        size_t i, j;
        for (i = 0; i < s.len; i++)
                for (j = 0; j < chars.len; j++)
                        if (s.ptr[i] == chars.ptr[j]) return (ptrdiff_t)i;
        return -1;
}

ptrdiff_t str_last_index_any(Str s, Str chars) {
        if (!s.len) return -1;
        size_t i = s.len;
        do {
                i--;
                size_t j;
                for (j = 0; j < chars.len; j++)
                        if (s.ptr[i] == chars.ptr[j]) return (ptrdiff_t)i;
        } while (i);
        return -1;
}

ptrdiff_t str_index_func(Str s, int (*pred)(int)) {
        size_t i;
        for (i = 0; i < s.len; i++)
                if (pred((unsigned char)s.ptr[i])) return (ptrdiff_t)i;
        return -1;
}

ptrdiff_t str_last_index_func(Str s, int (*pred)(int)) {
        if (!s.len) return -1;
        size_t i = s.len;
        do {
                i--;
                if (pred((unsigned char)s.ptr[i])) return (ptrdiff_t)i;
        } while (i);
        return -1;
}

/* ---- Predicates ----------------------------------------------- */

bool str_contains_any(Str s, Str chars) {
        return str_index_any(s, chars) >= 0;
}

bool str_contains_func(Str s, int (*pred)(int)) {
        return str_index_func(s, pred) >= 0;
}

size_t str_count(Str s, Str sub) {
        if (sub.len == 0) return s.len + 1;
        size_t count = 0;
        while (s.len >= sub.len) {
                ptrdiff_t pos = str_find(s, sub);
                if (pos < 0) break;
                count++;
                s.ptr += (size_t)pos + sub.len;
                s.len -= (size_t)pos + sub.len;
        }
        return count;
}

bool str_equal_fold(Str a, Str b) {
        if (a.len != b.len) return false;
        size_t i;
        for (i = 0; i < a.len; i++)
                if (tolower((unsigned char)a.ptr[i]) !=
                                tolower((unsigned char)b.ptr[i]))
                        return false;
        return true;
}

/* ---- Trim ----------------------------------------------------- */

Str str_trim_space(Str s) {
        while (s.len && isspace((unsigned char)s.ptr[0])) {
                s.ptr++;
                s.len--;
        }
        while (s.len && isspace((unsigned char)s.ptr[s.len - 1])) s.len--;
        return s;
}
Str str_trim_left_func(Str s, int (*pred)(int)) {
        while (s.len && pred((unsigned char)s.ptr[0])) {
                s.ptr++;
                s.len--;
        }
        return s;
}
Str str_trim_right_func(Str s, int (*pred)(int)) {
        while (s.len && pred((unsigned char)s.ptr[s.len - 1])) s.len--;
        return s;
}
Str str_trim_func(Str s, int (*pred)(int)) {
        return str_trim_left_func(str_trim_right_func(s, pred), pred);
}
Str str_trim_prefix(Str s, Str prefix) {
        if (str_has_prefix(s, prefix)) {
                s.ptr += prefix.len;
                s.len -= prefix.len;
        }
        return s;
}
Str str_trim_suffix(Str s, Str suffix) {
        if (str_has_suffix(s, suffix)) s.len -= suffix.len;
        return s;
}

/* ---- Cut ------------------------------------------------------ */

StrCut str_cut(Str s, Str sep) {
        ptrdiff_t pos = str_find(s, sep);
        if (pos < 0) {
                StrCut c;
                c.before = s;
                c.after  = str_null();
                c.found  = false;
                return c;
        }
        StrCut c;
        c.before = str_slice(s, 0, (size_t)pos);
        c.after  = str_slice(s, (size_t)pos + sep.len, s.len);
        c.found  = true;
        return c;
}

StrCut str_cut_prefix(Str s, Str prefix) {
        StrCut c;
        if (str_has_prefix(s, prefix)) {
                c.before = str_null();
                c.after  = str_slice(s, prefix.len, s.len);
                c.found  = true;
        } else {
                c.before = str_null();
                c.after  = str_null();
                c.found  = false;
        }
        return c;
}

StrCut str_cut_suffix(Str s, Str suffix) {
        StrCut c;
        if (str_has_suffix(s, suffix)) {
                c.before = str_slice(s, 0, s.len - suffix.len);
                c.after  = str_null();
                c.found  = true;
        } else {
                c.before = s;
                c.after  = str_null();
                c.found  = false;
        }
        return c;
}

StrCut str_cut_byte(Str s, char c) {
        ptrdiff_t i = str_index_byte(s, c);
        if (i < 0) return (StrCut){s, str_null(), false};
        return (StrCut){str_slice(s, 0, (size_t)i),
                str_slice(s, (size_t)i + 1, s.len),
                true};
}

StrCut str_cut_any(Str s, Str chars) {
        ptrdiff_t i = str_index_any(s, chars);
        if (i < 0) return (StrCut){s, str_null(), false};
        return (StrCut){str_slice(s, 0, (size_t)i),
                str_slice(s, (size_t)i + 1, s.len),
                true};
}

StrCut str_cut_func(Str s, int (*pred)(int)) {
        ptrdiff_t i = str_index_func(s, pred);
        if (i < 0) return (StrCut){s, str_null(), false};
        return (StrCut){str_slice(s, 0, (size_t)i),
                str_slice(s, (size_t)i + 1, s.len),
                true};
}

/* ---- Split ---------------------------------------------------- */

Slice(Str) str_split_n(Arena* a, Str s, Str sep, int n) {
        Slice(Str) r = slice_make(a, Str, 8);
        assert(sep.len > 0 && "str_split_n: sep must not be empty");
        if (n == 0) return r;
        int splits = 0;
        for (;;) {
                if (n > 0 && splits >= n - 1) break;
                ptrdiff_t pos = str_find(s, sep);
                if (pos < 0) break;
                Str part = str_slice(s, 0, (size_t)pos);
                slice_push(r, Str, part);
                s.ptr += (size_t)pos + sep.len;
                s.len -= (size_t)pos + sep.len;
                splits++;
        }
        slice_push(r, Str, s);
        return r;
}

Slice(Str) str_split(Arena* a, Str s, Str sep) {
        return str_split_n(a, s, sep, -1);
}

Slice(Str) str_split_after(Arena* a, Str s, Str sep) {
        Slice(Str) r = slice_make(a, Str, 8);
        assert(sep.len > 0 && "str_split_after: sep must not be empty");
        for (;;) {
                ptrdiff_t pos = str_find(s, sep);
                if (pos < 0) break;
                size_t end  = (size_t)pos + sep.len;
                Str    part = str_slice(s, 0, end);
                slice_push(r, Str, part);
                s.ptr += end;
                s.len -= end;
        }
        slice_push(r, Str, s);
        return r;
}

Slice(Str) str_fields(Arena* a, Str s) {
        Slice(Str) r = slice_make(a, Str, 8);
        while (s.len) {
                while (s.len && isspace((unsigned char)s.ptr[0])) {
                        s.ptr++;
                        s.len--;
                }
                if (!s.len) break;
                const char* start = s.ptr;
                while (s.len && !isspace((unsigned char)s.ptr[0])) {
                        s.ptr++;
                        s.len--;
                }
                Str word = str_buf(start, (size_t)(s.ptr - start));
                slice_push(r, Str, word);
        }
        return r;
}

Slice(Str) str_fields_func(Arena* a, Str s, int (*pred)(int)) {
        Slice(Str) r = slice_make(a, Str, 8);
        while (s.len) {
                while (s.len && pred((unsigned char)s.ptr[0])) {
                        s.ptr++;
                        s.len--;
                }
                if (!s.len) break;
                const char* start = s.ptr;
                while (s.len && !pred((unsigned char)s.ptr[0])) {
                        s.ptr++;
                        s.len--;
                }
                Str tok = str_buf(start, (size_t)(s.ptr - start));
                slice_push(r, Str, tok);
        }
        return r;
}

/* ---- Build ---------------------------------------------------- */

Str str_join(Arena* a, Slice(Str) parts, Str sep) {
        size_t n = slice_len(parts);
        if (!n) return str_buf("", 0);
        size_t total = sep.len * (n - 1);
        size_t i;
        for (i = 0; i < n; i++) total += parts[i].len;
        char* buf = arena_push_array(a, char, total + 1);
        char* p   = buf;
        for (i = 0; i < n; i++) {
                if (i > 0 && sep.len) {
                        memcpy(p, sep.ptr, sep.len);
                        p += sep.len;
                }
                if (parts[i].len) {
                        memcpy(p, parts[i].ptr, parts[i].len);
                        p += parts[i].len;
                }
        }
        *p = '\0';
        return str_buf(buf, total);
}

Str str_repeat(Arena* a, Str s, size_t count) {
        if (!count || !s.len) return str_buf("", 0);
        size_t total = s.len * count;
        char*  buf   = arena_push_array(a, char, total + 1);
        size_t i;
        for (i = 0; i < count; i++) memcpy(buf + i * s.len, s.ptr, s.len);
        buf[total] = '\0';
        return str_buf(buf, total);
}

Str str_replace(Arena* a, Str s, Str old_s, Str new_s, int n) {
        if (!old_s.len || n == 0) return str_clone(a, s);
        /* Pass 1: count occurrences */
        int occ = 0;
        Str tmp = s;
        while (tmp.len >= old_s.len && (n < 0 || occ < n)) {
                ptrdiff_t pos = str_find(tmp, old_s);
                if (pos < 0) break;
                occ++;
                tmp.ptr += (size_t)pos + old_s.len;
                tmp.len -= (size_t)pos + old_s.len;
        }
        if (!occ) return str_clone(a, s);
        /* Pass 2: allocate exactly */
        ptrdiff_t delta = (ptrdiff_t)new_s.len - (ptrdiff_t)old_s.len;
        size_t    total = (size_t)((ptrdiff_t)s.len + (ptrdiff_t)occ * delta);
        char*     buf   = arena_push_array(a, char, total + 1);
        char*     p     = buf;
        int       done  = 0;
        /* Pass 3: fill */
        while (s.len && (n < 0 || done < n)) {
                ptrdiff_t pos = str_find(s, old_s);
                if (pos < 0) break;
                memcpy(p, s.ptr, (size_t)pos);
                p += (size_t)pos;
                if (new_s.len) {
                        memcpy(p, new_s.ptr, new_s.len);
                        p += new_s.len;
                }
                s.ptr += (size_t)pos + old_s.len;
                s.len -= (size_t)pos + old_s.len;
                done++;
        }
        memcpy(p, s.ptr, s.len);
        p += s.len;
        *p = '\0';
        return str_buf(buf, total);
}

Str str_replace_all(Arena* a, Str s, Str old_s, Str new_s) {
        return str_replace(a, s, old_s, new_s, -1);
}

Str str_to_upper(Arena* a, Str s) {
        char*  buf = arena_push_array(a, char, s.len + 1);
        size_t i;
        for (i = 0; i < s.len; i++)
                buf[i] = (char)toupper((unsigned char)s.ptr[i]);
        buf[s.len] = '\0';
        return str_buf(buf, s.len);
}

Str str_to_lower(Arena* a, Str s) {
        char*  buf = arena_push_array(a, char, s.len + 1);
        size_t i;
        for (i = 0; i < s.len; i++)
                buf[i] = (char)tolower((unsigned char)s.ptr[i]);
        buf[s.len] = '\0';
        return str_buf(buf, s.len);
}

Str str_title(Arena* a, Str s) {
        char*  buf      = arena_push_array(a, char, s.len + 1);
        bool   new_word = true;
        size_t i;
        for (i = 0; i < s.len; i++) {
                unsigned char c = (unsigned char)s.ptr[i];
                if (isspace(c)) {
                        buf[i]   = (char)c;
                        new_word = true;
                } else if (new_word) {
                        buf[i]   = (char)toupper(c);
                        new_word = false;
                } else
                        buf[i] = (char)c;
        }
        buf[s.len] = '\0';
        return str_buf(buf, s.len);
}

/* ---- UTF-8 ---------------------------------------------------- */

#        ifdef UTF8_H

bool str_contains_rune(Str s, Rune r) {
        Utf8Iter it = utf8_iter(s);
        Rune     cur;
        while (utf8_next(&it, &cur))
                if (cur.cp == r.cp) return true;
        return false;
}

ptrdiff_t str_index_rune(Str s, Rune r) {
        const char *p = s.ptr, *end = s.ptr + s.len;
        while (p < end) {
                Rune cur;
                int  n = utf8_decode(p, (size_t)(end - p), &cur);
                if (n <= 0) break;
                if (cur.cp == r.cp) return (ptrdiff_t)(p - s.ptr);
                p += n;
        }
        return -1;
}

Str str_map_rune(Arena* a, Str s, Rune (*fn)(Rune)) {
        /* Pass 1 */ size_t total = 0;
        {
                Utf8Iter it = utf8_iter(s);
                Rune     r;
                while (utf8_next(&it, &r)) {
                        Rune o = fn(r);
                        total += (size_t)utf8_width(o);
                }
        }
        /* Pass 2 */ char* buf = arena_push_array(a, char, total + 1);
        char*              p   = buf;
        {
                Utf8Iter it = utf8_iter(s);
                Rune     r;
                while (utf8_next(&it, &r)) {
                        Rune o = fn(r);
                        int  w = utf8_width(o), i;
                        for (i = 0; i < w; i++) *p++ = (char)o.bytes[i];
                }
        }
        *p = '\0';
        return str_buf(buf, total);
}

Str str_to_valid_utf8(Arena* a, Str s, Str replacement) {
        const char*         end   = s.ptr + s.len;
        /* Pass 1 */ size_t total = 0;
        const char*         p     = s.ptr;
        while (p < end) {
                Rune r;
                int  n = utf8_decode(p, (size_t)(end - p), &r);
                if (n <= 0) break;
                total += (r.cp == RUNE_ERROR && n == 1) ? replacement.len
                        : (size_t)n;
                p += n;
        }
        /* Pass 2 */ char* buf = arena_push_array(a, char, total + 1);
        char*              o   = buf;
        p                      = s.ptr;
        while (p < end) {
                Rune r;
                int  n = utf8_decode(p, (size_t)(end - p), &r);
                if (n <= 0) break;
                if (r.cp == RUNE_ERROR && n == 1) {
                        memcpy(o, replacement.ptr, replacement.len);
                        o += replacement.len;
                } else {
                        memcpy(o, p, (size_t)n);
                        o += n;
                }
                p += n;
        }
        *o = '\0';
        return str_buf(buf, total);
}

#        endif /* UTF8_H */
#endif         /* BARESTRS_IMPLEMENTATION */
#endif         /* BARESTRS_H */
