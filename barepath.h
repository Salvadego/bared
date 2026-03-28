/*
 * barepath.h -- Portable path manipulation
 * =========================================
 *
 *  USAGE
 *    #define BAREPATH_IMPLEMENTATION
 *    #include "barepath.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Str, Arena, Slice)
 *
 *  DESIGN
 *    All functions operate on Str views.  Functions that return a new
 *    string allocate from the arena; decomposition functions return
 *    sub-views with no allocation.
 *
 *    Canonical separator is '/'.  Windows backslashes are accepted as
 *    input everywhere and normalised to '/' in output.
 *
 *  EXAMPLE
 *
 *    Str dir  = path_dir (str_lit("src/foo/bar.c"));  // "src/foo"
 *    Str base = path_base(str_lit("src/foo/bar.c"));  // "bar.c"
 *    Str ext  = path_ext (str_lit("src/foo/bar.c"));  // ".c"
 *    Str stem = path_stem(str_lit("src/foo/bar.c"));  // "bar"
 *
 *    Str joined = path_join(arena, str_lit("src/foo"), str_lit("bar.c"));
 *    // "src/foo/bar.c"
 *
 *    Str clean = path_clean(arena, str_lit("src//foo/../bar"));
 *    // "src/bar"
 */

#ifndef BAREPATH_H
#define BAREPATH_H

#include "barestd.h"

/* ================================================================
 *  Decomposition -- no allocation, returns views into the input
 * ================================================================ */

/* Directory component: everything up to the last '/'.
   Returns "." if there is no directory component. */
Str path_dir(Str p);

/* Final element of the path (filename + extension). */
Str path_base(Str p);

/* Extension including the dot: ".c".
   Returns an empty Str if the base has no extension. */
Str path_ext(Str p);

/* Base without extension. */
Str path_stem(Str p);

/* ================================================================
 *  Predicates -- no allocation
 * ================================================================ */

/* True if path is absolute ('/' prefix, or Windows "C:\"). */
bool path_is_abs(Str p);

/* True if the path has no ".." or "." components and no double slashes. */
bool path_is_clean(Str p);

/* ================================================================
 *  Construction -- allocates into arena
 * ================================================================ */

/* Join two path segments, inserting '/' as needed.
   An absolute right side replaces the left entirely:
   path_join(a, "src/foo", "bar")   -> "src/foo/bar"
   path_join(a, "src/foo/", "bar")  -> "src/foo/bar"
   path_join(a, "src/foo", "/bar")  -> "/bar"          */
Str path_join(Arena* a, Str left, Str right);

/* Join a Slice(Str) of segments. */
Str path_join_slice(Arena* a, Slice(Str) parts);

/* Resolve ".." and "." components, collapse duplicate separators.
   Does not access the filesystem.
   path_clean(a, "a//b/../c") -> "a/c"
   path_clean(a, "./x")       -> "x"
   path_clean(a, "")          -> "."               */
Str path_clean(Arena* a, Str p);

/* Make p relative to base.  Returns p unchanged if not possible.
   path_rel(a, "src/foo/bar.c", "src") -> "foo/bar.c" */
Str path_rel(Arena* a, Str p, Str base);

/* Return a copy of p with the extension replaced (or added).
   new_ext should include the dot, e.g. ".h".
   path_with_ext(a, "foo/bar.c", ".h") -> "foo/bar.h" */
Str path_with_ext(Arena* a, Str p, Str new_ext);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREPATH_IMPLEMENTATION

#        include <string.h>

static int _path_is_sep(char c) {
        return c == '/' || c == '\\';
}

Str path_dir(Str p) {
        if (str_is_empty(p)) return str_lit(".");
        /* strip trailing separators */
        while (p.len > 1 && _path_is_sep(p.ptr[p.len - 1])) p.len--;
        /* find last separator */
        size_t i = p.len;
        while (i > 0 && !_path_is_sep(p.ptr[i - 1])) i--;
        if (i == 0) return str_lit(".");
        /* trim trailing separators from dir part (but keep root "/") */
        while (i > 1 && _path_is_sep(p.ptr[i - 1])) i--;
        return str_buf(p.ptr, i);
}

Str path_base(Str p) {
        if (str_is_empty(p)) return str_lit(".");
        while (p.len > 1 && _path_is_sep(p.ptr[p.len - 1])) p.len--;
        size_t i = p.len;
        while (i > 0 && !_path_is_sep(p.ptr[i - 1])) i--;
        return str_buf(p.ptr + i, p.len - i);
}

Str path_ext(Str p) {
        Str    base = path_base(p);
        size_t i    = base.len;
        while (i > 0 && base.ptr[i - 1] != '.') i--;
        /* i==0: no dot; i==1: leading dot only (e.g. ".hidden") */
        if (i == 0 || i == 1) return str_buf("", 0);
        return str_buf(base.ptr + i - 1, base.len - i + 1);
}

Str path_stem(Str p) {
        Str ext = path_ext(p);
        Str b   = path_base(p);
        if (str_is_empty(ext)) return b;
        return str_buf(b.ptr, b.len - ext.len);
}

bool path_is_abs(Str p) {
        if (str_is_empty(p)) return false;
        if (_path_is_sep(p.ptr[0])) return true;
        /* Windows: "C:\" or "C:/" */
        if (p.len >= 3 && p.ptr[1] == ':' && _path_is_sep(p.ptr[2]))
                return true;
        return false;
}

bool path_is_clean(Str p) {
        size_t i;
        for (i = 0; i < p.len; i++) {
                if (p.ptr[i] == '\\') return false;
                if (p.ptr[i] == '/' && i + 1 < p.len && p.ptr[i + 1] == '/')
                        return false;
                if (p.ptr[i] == '.' && (i == 0 || p.ptr[i - 1] == '/')) {
                        if (i + 1 >= p.len || p.ptr[i + 1] == '/') return false;
                        if (p.ptr[i + 1] == '.' &&
                            (i + 2 >= p.len || p.ptr[i + 2] == '/'))
                                return false;
                }
        }
        return true;
}

Str path_join(Arena* a, Str left, Str right) {
        if (path_is_abs(right)) return str_clone(a, right);
        while (left.len > 0 && _path_is_sep(left.ptr[left.len - 1])) left.len--;
        while (right.len > 0 && _path_is_sep(right.ptr[0])) {
                right.ptr++;
                right.len--;
        }
        if (str_is_empty(left)) return str_clone(a, right);
        if (str_is_empty(right)) return str_clone(a, left);
        size_t total = left.len + 1 + right.len;
        char*  buf   = arena_push_array(a, char, total + 1);
        memcpy(buf, left.ptr, left.len);
        buf[left.len] = '/';
        memcpy(buf + left.len + 1, right.ptr, right.len);
        buf[total] = '\0';
        return str_buf(buf, total);
}

Str path_join_slice(Arena* a, Slice(Str) parts) {
        size_t n = slice_len(parts);
        if (n == 0) return str_buf("", 0);
        Str    r = parts[0];
        size_t i;
        for (i = 1; i < n; i++) r = path_join(a, r, parts[i]);
        return r;
}

Str path_clean(Arena* a, Str p) {
        if (str_is_empty(p)) return str_lit(".");

        bool   rooted = _path_is_sep(p.ptr[0]);
        char*  buf    = arena_push_array(a, char, p.len + 1);
        size_t out    = 0;
        size_t i      = rooted ? 1 : 0;

        if (rooted) buf[out++] = '/';

        while (i < p.len) {
                /* skip duplicate separators and backslashes */
                if (_path_is_sep(p.ptr[i])) {
                        i++;
                        continue;
                }

                size_t start = i;
                while (i < p.len && !_path_is_sep(p.ptr[i])) i++;
                size_t      comp_len = i - start;
                const char* comp     = p.ptr + start;

                if (comp_len == 1 && comp[0] == '.') {
                        /* "." -- skip */
                        continue;
                }

                if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
                        /* ".." -- pop last component */
                        if (rooted) {
                                while (out > 1 && buf[out - 1] != '/') out--;
                                if (out > 1) out--;
                        } else if (out > 0) {
                                /* pop including the preceding slash */
                                while (out > 0 && buf[out - 1] != '/') out--;
                                if (out > 0) out--;
                        } else {
                                /* at start with no root -- keep ".." literally
                                 */
                                buf[out++] = '.';
                                buf[out++] = '.';
                        }
                        continue;
                }

                if (out > 0 && buf[out - 1] != '/') buf[out++] = '/';
                memcpy(buf + out, comp, comp_len);
                out += comp_len;
        }

        if (out == 0) {
                buf[0] = '.';
                out    = 1;
        }
        buf[out] = '\0';
        return str_buf(buf, out);
}

Str path_rel(Arena* a, Str p, Str base) {
        Str cp = path_clean(a, p);
        Str cb = path_clean(a, base);
        /* find common prefix length at a separator boundary */
        size_t i = 0;
        while (i < cp.len && i < cb.len && cp.ptr[i] == cb.ptr[i]) i++;
        if (i < cb.len) return str_clone(a, p); /* base not a prefix */
        if (i < cp.len && !_path_is_sep(cp.ptr[i])) return str_clone(a, p);
        if (_path_is_sep(cp.ptr[i])) i++;
        return str_buf(cp.ptr + i, cp.len - i);
}

Str path_with_ext(Arena* a, Str p, Str new_ext) {
        Str    ext   = path_ext(p);
        Str    stem  = str_buf(p.ptr, p.len - ext.len);
        int    dot   = (new_ext.len > 0 && new_ext.ptr[0] == '.') ? 0 : 1;
        size_t total = stem.len + (size_t)dot + new_ext.len;
        char*  buf   = arena_push_array(a, char, total + 1);
        memcpy(buf, stem.ptr, stem.len);
        if (dot) buf[stem.len] = '.';
        memcpy(buf + stem.len + (size_t)dot, new_ext.ptr, new_ext.len);
        buf[total] = '\0';
        return str_buf(buf, total);
}

#endif /* BAREPATH_IMPLEMENTATION */
#endif /* BAREPATH_H */
