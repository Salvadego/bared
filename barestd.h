/*
 * barestd.h - single-header C99 stdlib
 * ================================================
 *
 *  Modules
 *    0  Foundation  - macros, alignment, sizing
 *    1  Arena       - bump-pointer allocator, scratch scopes
 *    2  Slice       - fat-pointer array with shadow header
 *    3  Str         - non-owning UTF-8 string view
 *    4  Map         - Robin Hood open-addressing hash map
 *                     with shadow header
 *
 *  USAGE
 *    In exactly ONE translation unit:
 *
 *      #define BARESTD_IMPLEMENTATION
 *      #include "barestd.h"
 *
 *    All other TUs only need:
 *
 *      #include "barestd.h"
 *
 *  REQUIREMENTS
 *    C99. GCC or Clang recommended (VLAs used in Map internals).
 *    No external dependencies beyond the C standard library.
 *
 *  DESIGN NOTES
 *    - Every allocation goes through an Arena - no bare malloc/free.
 *    - Slice and Map store their metadata in a shadow header that
 *      sits immediately before the user-facing pointer in memory.
 *    - Str is always a non-owning view; use str_clone() for owned
 *      copies backed by an arena.
 *    - Map keys and values are copied by value into bucket storage.
 *    - hash_fn must never return 0 (0 is the "empty slot" sentinel).
 */

#ifndef BARESTD_H
#define BARESTD_H

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 *  0 - FOUNDATION
 * ================================================================ */

#define OffsetOfMember(T, m) offsetof(T, m)
#define AlignOfType(T)   \
        (sizeof(struct { \
                 char c; \
                 T    x; \
         }) -            \
         sizeof(T))
#define ArrayCount(a) (sizeof(a) / sizeof(*(a)))

#define KB(n) ((size_t)(n) * (size_t)1024)
#define MB(n) ((size_t)(n) * (size_t)1024 * 1024)
#define GB(n) ((size_t)(n) * (size_t)1024 * 1024 * 1024)

#define AlignUp(n, a)   (((size_t)(n) + (size_t)(a) - 1) & ~((size_t)(a) - 1))
#define AlignDown(n, a) ((size_t)(n) & ~((size_t)(a) - 1))
#define IsPow2(n)       ((n) != 0 && !((n) & ((n) - 1)))

#define Min(a, b)        ((a) < (b) ? (a) : (b))
#define Max(a, b)        ((a) > (b) ? (a) : (b))
#define Clamp(lo, x, hi) (Max((lo), Min((x), (hi))))

#define Unused(x) ((void)(x))

/* Compile-time assert - C99 compatible */
#define StaticAssert(expr) typedef char _barestd_sa_##__LINE__[(expr) ? 1 : -1]

/* ================================================================
 *  1 - ARENA
 * ================================================================
 *
 *  A bump-pointer allocator.  All allocations are O(1).
 *  There is no per-allocation free; memory is reclaimed by
 *  resetting or freeing the entire arena.
 *
 *  Two creation modes:
 *
 *    Stack / static buffer (no heap involved):
 *      uint8_t  backing[KB(64)];
 *      Arena    a = arena_from_buf(backing, sizeof backing);
 *
 *    Heap-backed (arena_free() required):
 *      Arena *a = arena_new(MB(4));
 *      ...
 *      arena_free(a);
 *
 *  Overflow:  when a chunk is exhausted a new heap-backed chunk
 *  is appended transparently via the `next` linked list.
 *
 *  Scratch pattern for short-lived allocations:
 *      Scratch tmp = scratch_begin(a);
 *      ... allocate freely from a ...
 *      scratch_end(tmp);   // rewinds a->pos to the save point
 *      (does NOT memset the memory back to zero)
 */

typedef struct Arena Arena;
struct Arena {
        uint8_t* buf;   /* raw backing buffer                      */
        size_t   pos;   /* index of next free byte                 */
        size_t   cap;   /* total capacity of buf                   */
        Arena*   next;  /* overflow chunk (heap-allocated)         */
        int      owned; /* non-zero -> buf was malloc'd by us      */
};

Arena  arena_from_buf(void* buf, size_t cap);
Arena* arena_new(size_t cap);
void*  arena_push(Arena* a, size_t size, size_t align);
void   arena_pop_to(Arena* a, size_t saved_pos);
void   arena_reset(Arena* a);
void   arena_free(Arena* a);

#define arena_push_type(a, T) ((T*)arena_push((a), sizeof(T), AlignOfType(T)))
#define arena_push_array(a, T, n) \
        ((T*)arena_push((a), sizeof(T) * (n), AlignOfType(T)))

typedef struct {
        Arena* a;
        size_t pos;
} Scratch;
Scratch scratch_begin(Arena* a);
void    scratch_end(Scratch s);

/* ================================================================
 *  2 - SLICE  (shadow-header fat pointer)
 * ================================================================
 *
 *  A growable, typed array where metadata lives in a hidden header
 *  placed immediately before the user-visible element pointer:
 *
 *    <------- SLICE_HDR_OFFSET ------->
 *   ┌──────────────────────────────────┬────────────────────────────┐
 *   │            SliceHdr              │   T[0]   T[1] ... T[n-1]   │
 *   └──────────────────────────────────┴────────────────────────────┘
 *                                      ^
 *                                  Slice(T) ptr
 *
 *  SLICE_HDR_OFFSET = AlignUp(sizeof(SliceHdr), 16)
 *
 *  Example:
 *
 *    Arena *a = arena_new(MB(1));
 *
 *    Slice(int) nums = slice_make(a, int, 8);
 *    int v = 10;
 *    slice_put(nums, v);
 *
 *    printf("len=%zu\n",   slice_len(nums));    // 1
 *    printf("first=%d\n",  slice_at(nums, 0));  // 10
 *
 *  NOTES
 *    - slice_put and slice_push take the address of a local variable.
 *      Inline literals (e.g. slice_put(s, 42)) are not supported.
 *      Assign to a variable first.
 *    - After slice_put/slice_push the slice pointer may change
 *      (on grow). Always use the macro form which updates in-place.
 */

typedef struct {
        Arena* arena;      /* owning arena - set once at slice_make    */
        size_t len;        /* number of live elements                  */
        size_t cap;        /* allocated capacity (element count)       */
        size_t stride;     /* sizeof one element                       */
        size_t elem_align; /* alignment of element type                */
} SliceHdr;

#define SLICE_HDR_OFFSET AlignUp(sizeof(SliceHdr), 16)

#define slice_hdr(s) ((SliceHdr*)((uint8_t*)(s) - SLICE_HDR_OFFSET))

#define slice_len(s)    (slice_hdr(s)->len)
#define slice_cap(s)    (slice_hdr(s)->cap)
#define slice_stride(s) (slice_hdr(s)->stride)

/* Slice(T): declare a typed slice variable */
#define Slice(T) T*

void* _slice_make(Arena* a, size_t stride, size_t elem_align, size_t cap);
void* _slice_push_raw(void* s, const void* elem);

static inline size_t _barestd_chk(size_t i, size_t len) {
        assert(i < len && "slice index out of bounds");
        return i;
}

static inline bool _slice_contains_raw(const void* s,
                                       const void* val,
                                       size_t      len,
                                       size_t      stride) {
        const uint8_t* p = (const uint8_t*)s;
        size_t         i;
        for (i = 0; i < len; i++, p += stride) {
                if (memcmp(p, val, stride) == 0) return true;
        }
        return false;
}

#define slice_make(a, T, cap) \
        ((T*)_slice_make((a), sizeof(T), AlignOfType(T), (size_t)(cap)))

/* Append a lvalue to the slice.
   val must be an addressable variable, not an inline literal. */
#define slice_put(s, val)                           \
        do {                                        \
                (s) = _slice_push_raw((s), &(val)); \
        } while (0)

/* Append with explicit type - for use inside other macros or when
   the variable is not yet declared. */
#define slice_push(s, T, val)                               \
        do {                                                \
                T _bs_v = (val);                            \
                (s)     = (T*)_slice_push_raw((s), &_bs_v); \
        } while (0)

/* Remove element at index i, shifting tail left. O(n). */
#define slice_remove(s, i)                                               \
        do {                                                             \
                size_t    _ri = (size_t)(i);                             \
                SliceHdr* _rh = slice_hdr(s);                            \
                assert(_ri < _rh->len && "slice_remove: out of bounds"); \
                memmove((uint8_t*)(s) + _ri * _rh->stride,               \
                        (uint8_t*)(s) + (_ri + 1) * _rh->stride,         \
                        (_rh->len - _ri - 1) * _rh->stride);             \
                _rh->len--;                                              \
        } while (0)

/* Linear scan for val (must be an lvalue). Returns bool. */
#define slice_contains(s, val) \
        _slice_contains_raw((s), &(val), slice_len(s), slice_stride(s))

#define slice_pop(s)                                            \
        do {                                                    \
                if (slice_hdr(s)->len > 0) slice_hdr(s)->len--; \
        } while (0)

#define slice_at(s, i) (s)[_barestd_chk((size_t)(i), slice_len(s))]

#define slice_last(s)  (s)[_barestd_chk(slice_len(s) - 1, slice_len(s))]
#define slice_clear(s) ((void)(slice_hdr(s)->len = 0))

/* ================================================================
 *  3 - STR  (non-owning string view)
 * ================================================================
 *
 *  Str is a (ptr, len) pair.  It never owns memory and is never
 *  required to be NUL-terminated.
 *
 *  str_next_token splits without touching the underlying buffer:
 *
 *    Str rest = str_lit("a,b,c");
 *    Str tok;
 *    while ((tok = str_next_token(&rest, str_lit(","))).ptr) {
 *        printf(StrFmt "\n", StrArgs(tok));
 *    }
 */

typedef struct {
        const char* ptr;
        size_t      len;
} Str;

#define str_lit(s)       ((Str){(s), sizeof(s) - 1})
#define str_from_c(cstr) ((Str){(cstr), strlen(cstr)})
#define str_buf(p, n)    ((Str){(const char*)(p), (size_t)(n)})
#define str_null()       ((Str){NULL, 0})

#define str_is_null(s)  ((s).ptr == NULL)
#define str_is_empty(s) ((s).len == 0)

/* printf helpers: printf(StrFmt "\n", StrArgs(s)); */
#define StrFmt     "%.*s"
#define StrArgs(s) (int)(s).len, (s).ptr

bool      str_eq(Str a, Str b);
bool      str_has_prefix(Str s, Str pfx);
bool      str_has_suffix(Str s, Str sfx);
ptrdiff_t str_find(Str hay, Str needle);

Str str_slice(Str s, size_t lo, size_t hi);
Str str_trim_left(Str s, Str cutset);
Str str_trim_right(Str s, Str cutset);
Str str_trim(Str s, Str cutset);

/* Non-destructive tokeniser.
   Advances *s past the next separator and returns the token before it.
   Returns str_null() when *s is exhausted.
   sep must not be empty. */
Str str_next_token(Str* s, Str sep);

/* Consume leading chars WHILE pred returns non-zero. Returns consumed prefix.
 */
Str str_consume_while(Str* s, int (*pred)(int));

/* Consume leading chars UNTIL pred returns non-zero. Returns consumed prefix.
 */
Str str_consume_until(Str* s, int (*pred)(int));

char* str_to_cstr(Arena* a, Str s);
Str   str_clone(Arena* a, Str s);

/* Arena-backed printf. Result is stable for the lifetime of a.
   No intermediate heap allocation -- sizes with vsnprintf then fills. */
Str str_fmt(Arena* a, const char* fmt, ...);

/* ----------------------------------------------------------------
 *  str_from -- parse numeric values out of a Str without needing
 *  a NUL-terminated copy.
 *
 *  Consuming variants advance *s past the parsed token on success.
 *  Non-consuming variants parse the whole Str and return true/false.
 *  On failure the output is unchanged and *s is not advanced.
 *
 *  Integers accept optional leading +/- and an optional 0x/0X prefix
 *  for hex.  Floats accept the formats strtod recognises.
 *  str_from_bool accepts "true"/"false" (case-insensitive) and "1"/"0".
 * ---------------------------------------------------------------- */

/* Consuming -- advances *s past whitespace then the number token. */
bool str_consume_i64(Str* s, int64_t* out);
bool str_consume_u64(Str* s, uint64_t* out);
bool str_consume_f64(Str* s, double* out);
bool str_consume_bool(Str* s, bool* out);

/* Non-consuming -- parses entire string (ignoring leading/trailing space). */
bool str_to_i64(Str s, int64_t* out);
bool str_to_u64(Str s, uint64_t* out);
bool str_to_f64(Str s, double* out);
bool str_to_bool(Str s, bool* out);

/* ================================================================
 *  4 - MAP  (Robin Hood open-addressing, shadow header)
 * ================================================================
 *
 *  <---- MAP_HDR_OFFSET --->
 * ┌────────────────────────┬─────────────────────────────────────┐
 * │         MapHdr         │ Bucket[0]  Bucket[1] ... Bucket[n]  │
 * └────────────────────────┴─────────────────────────────────────┘
 *                          ^
 *                        Map* ptr
 *
 *  Each bucket:
 *   ┌──────────┬────────┬──────────────┬─────────┬──────────────┐
 *   │ hash  8B │ psl 4B │  _pad 4B     │ key[ks] │  val[vs]     │
 *   └──────────┴────────┴──────────────┴─────────┴──────────────┘
 *     offset 0           offset 16
 *
 *  val is placed at AlignUp(sizeof(BucketHdr) + key_sz, val_align)
 *  so that values are always naturally aligned regardless of key size.
 *  val_align is stored in MapHdr and propagated to every bucket op.
 *
 *  Invariants:
 *    - hash == 0   ->  empty slot  (hash_fn must never return 0)
 *    - psl         ->  probe-sequence length used by Robin Hood
 *    - Load factor: rehash when len >= cap * 3/4
 *    - Capacity:    always a power of two, minimum 8
 *    - Deletion:    backward shift  (no tombstones)
 *
 *  Example:
 *
 *    Map *m = map_make(a, int, int, 16);
 *    int k = 7, v = 42;
 *    map_set(m, &k, &v);
 *
 *    int *got = (int *)map_get(m, &k);  // *got == 42
 *    map_del(m, &k);
 *
 *    MapIter it = {0};
 *    while (map_next(m, &it))
 *        printf("%d -> %d\n", iter_key(it, int), iter_val(it, int));
 */

typedef struct {
        uint64_t hash; /* 0 = empty slot                          */
        uint32_t psl;  /* probe-sequence length (Robin Hood)      */
        uint32_t _pad; /* key starts at offset 16                 */
} BucketHdr;

typedef struct {
        Arena* arena;
        size_t len;
        size_t cap;
        size_t key_sz;
        size_t val_sz;
        size_t val_align; /* alignment of value type - ensures val is
                             placed at a correctly aligned offset within
                             each bucket regardless of key_sz          */
        uint64_t (*hash_fn)(const void* key, size_t sz);
        bool (*eq_fn)(const void* a, const void* b, size_t sz);
} MapHdr;

typedef struct {
        size_t i;   /* current bucket index - zero-init to start */
        void*  key; /* pointer into live bucket after advance    */
        void*  val;
} MapIter;

typedef uint8_t Map;

#define MAP_HDR_OFFSET AlignUp(sizeof(MapHdr), 8)
#define map_hdr(m)     ((MapHdr*)((uint8_t*)(m) - MAP_HDR_OFFSET))
#define map_len(m)     (map_hdr(m)->len)
#define map_cap(m)     (map_hdr(m)->cap)

/* Typed iterator access.
   Uses memcpy into a correctly-aligned stack local so that the
   dereference is always UB-free, even when the bucket pointer is
   unaligned (e.g. char key followed by int val). */
#define iter_key(it, T) (*((T*)memcpy(&(T){0}, (it).key, sizeof(T))))
#define iter_val(it, T) (*((T*)memcpy(&(T){0}, (it).val, sizeof(T))))

/* Zero all buckets, reset len. Retains cap and arena. */
#define map_clear(m)                                                           \
        do {                                                                   \
                MapHdr* _mh = map_hdr(m);                                      \
                size_t  _bsz =                                                 \
                    _map_bkt_stride(_mh->key_sz, _mh->val_sz, _mh->val_align); \
                memset((m), 0, _mh->cap * _bsz);                               \
                _mh->len = 0;                                                  \
        } while (0)

uint64_t map_hash_bytes(const void* key, size_t sz);
bool     map_eq_bytes(const void* a, const void* b, size_t sz);

/* _map_bkt_stride is part of the public internal ABI so that map_clear
   can call it from a macro without pulling in a private helper. */
size_t _map_bkt_stride(size_t key_sz, size_t val_sz, size_t val_align);

Map*  _map_make(Arena* a,
                size_t key_sz,
                size_t val_sz,
                size_t val_align,
                size_t cap,
                uint64_t (*hash_fn)(const void*, size_t),
                bool (*eq_fn)(const void*, const void*, size_t));
void* _map_get(Map* m, const void* key);
void  _map_set(Map** mp, const void* key, const void* val);
bool  _map_del(Map* m, const void* key);
bool  map_next(Map* m, MapIter* it);
void* _map_keys(Map* m, Arena* a, size_t key_sz);
void* _map_values(Map* m, Arena* a, size_t val_sz, size_t val_align);

uint64_t map_hash_str(const void* key, size_t sz);
bool     map_eq_str(const void* a, const void* b, size_t sz);

/* Convenience map_make for Str keys */
#define map_make_str_key(a, VT, cap) \
        _map_make((a),               \
                  sizeof(Str),       \
                  sizeof(VT),        \
                  AlignOfType(VT),   \
                  (size_t)(cap),     \
                  map_hash_str,      \
                  map_eq_str)

/* Deep-copy map into a (preserves all live entries). */
Map* map_clone(Map* m, Arena* a);

#define map_make(a, KT, VT, cap)   \
        _map_make((a),             \
                  sizeof(KT),      \
                  sizeof(VT),      \
                  AlignOfType(VT), \
                  (size_t)(cap),   \
                  map_hash_bytes,  \
                  map_eq_bytes)

#define map_get(m, keyptr)         _map_get((m), (keyptr))
#define map_set(m, keyptr, valptr) _map_set(&(m), (keyptr), (valptr))
#define map_del(m, keyptr)         _map_del((m), (keyptr))
#define map_keys(m, a, KT)         ((KT*)_map_keys((m), (a), sizeof(KT)))
#define map_values(m, a, VT) \
        ((VT*)_map_values((m), (a), sizeof(VT), AlignOfType(VT)))

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARESTD_IMPLEMENTATION

/* ----------------------------------------------------------------
 *  1 impl - Arena
 * ---------------------------------------------------------------- */

Arena arena_from_buf(void* buf, size_t cap) {
        Arena a;
        memset(&a, 0, sizeof a);
        a.buf = (uint8_t*)buf;
        a.cap = cap;
        return a;
}

Arena* arena_new(size_t cap) {
        Arena* a = (Arena*)malloc(sizeof(Arena) + cap);
        assert(a && "arena_new: out of memory");
        memset(a, 0, sizeof(Arena));
        a->buf   = (uint8_t*)(a + 1);
        a->cap   = cap;
        a->owned = 1;
        return a;
}

void* arena_push(Arena* a, size_t size, size_t align) {
        assert(IsPow2(align) && "arena_push: align must be a power of two");
        size_t pos = AlignUp(a->pos, align);
        size_t end = pos + size;
        if (end > a->cap) {
                if (!a->next) {
                        size_t chunk =
                            Max(a->cap * 2, AlignUp(size, align) + align);
                        a->next = arena_new(chunk);
                }
                return arena_push(a->next, size, align);
        }
        a->pos = end;
        return a->buf + pos;
}

void arena_pop_to(Arena* a, size_t pos) {
        assert(pos <= a->pos && "arena_pop_to: saved_pos is ahead of current");
        a->pos = pos;
}

void arena_reset(Arena* a) {
        a->pos = 0;
        if (a->next) arena_reset(a->next);
}

void arena_free(Arena* a) {
        if (!a) return;
        if (a->next) arena_free(a->next);
        if (a->owned) free(a);
}

Scratch scratch_begin(Arena* a) {
        return (Scratch){a, a->pos};
}
void scratch_end(Scratch s) {
        arena_pop_to(s.a, s.pos);
}

/* ----------------------------------------------------------------
 *  2 impl - Slice
 * ---------------------------------------------------------------- */

void* _slice_make(Arena* a, size_t stride, size_t elem_align, size_t cap) {
        if (elem_align < 1) elem_align = 1;
        if (elem_align > 16) elem_align = 16;
        size_t   total = SLICE_HDR_OFFSET + stride * cap;
        uint8_t* raw   = (uint8_t*)arena_push(a, total, 16);
        memset(raw, 0, total);
        SliceHdr* hdr   = (SliceHdr*)raw;
        hdr->arena      = a;
        hdr->len        = 0;
        hdr->cap        = cap;
        hdr->stride     = stride;
        hdr->elem_align = elem_align;
        return raw + SLICE_HDR_OFFSET;
}

void* _slice_push_raw(void* s, const void* elem) {
        SliceHdr* hdr = slice_hdr(s); /* single header read */
        Arena*    a   = hdr->arena;

        if (hdr->len >= hdr->cap) {
                size_t new_cap = hdr->cap ? hdr->cap * 2 : 4;
                void*  new_s =
                    _slice_make(a, hdr->stride, hdr->elem_align, new_cap);
                memcpy(new_s, s, hdr->len * hdr->stride);
                slice_hdr(new_s)->len = hdr->len;
                s                     = new_s;
                hdr                   = slice_hdr(s);
        }

        memcpy((uint8_t*)s + hdr->len * hdr->stride, elem, hdr->stride);
        hdr->len++;
        return s;
}

/* ----------------------------------------------------------------
 *  3 impl - Str
 * ---------------------------------------------------------------- */

bool str_eq(Str a, Str b) {
        return a.len == b.len &&
               (a.ptr == b.ptr || memcmp(a.ptr, b.ptr, a.len) == 0);
}

bool str_has_prefix(Str s, Str pfx) {
        return s.len >= pfx.len && memcmp(s.ptr, pfx.ptr, pfx.len) == 0;
}

bool str_has_suffix(Str s, Str sfx) {
        return s.len >= sfx.len &&
               memcmp(s.ptr + (s.len - sfx.len), sfx.ptr, sfx.len) == 0;
}

ptrdiff_t str_find(Str hay, Str needle) {
        if (needle.len == 0) return 0;
        if (needle.len > hay.len) return -1;
        size_t i;
        for (i = 0; i <= hay.len - needle.len; ++i)
                if (memcmp(hay.ptr + i, needle.ptr, needle.len) == 0)
                        return (ptrdiff_t)i;
        return -1;
}

Str str_slice(Str s, size_t lo, size_t hi) {
        assert(lo <= hi && hi <= s.len && "str_slice: out of bounds");
        return str_buf(s.ptr + lo, hi - lo);
}

static bool _char_in_set(char c, Str set) {
        size_t i;
        for (i = 0; i < set.len; ++i)
                if (set.ptr[i] == c) return true;
        return false;
}

Str str_trim_left(Str s, Str cutset) {
        while (s.len > 0 && _char_in_set(s.ptr[0], cutset)) {
                s.ptr++;
                s.len--;
        }
        return s;
}

Str str_trim_right(Str s, Str cutset) {
        while (s.len > 0 && _char_in_set(s.ptr[s.len - 1], cutset)) s.len--;
        return s;
}

Str str_trim(Str s, Str cutset) {
        return str_trim_left(str_trim_right(s, cutset), cutset);
}

Str str_next_token(Str* s, Str sep) {
        assert(!str_is_empty(sep) &&
               "str_next_token: separator cannot be empty");
        if (str_is_empty(*s)) return str_null();

        ptrdiff_t pos = str_find(*s, sep);
        if (pos < 0) {
                /* sep not found - return the rest and exhaust s */
                Str tok = *s;
                s->ptr += s->len;
                s->len = 0;
                return tok;
        }
        Str tok = str_slice(*s, 0, (size_t)pos);
        s->ptr += (size_t)pos + sep.len;
        s->len -= (size_t)pos + sep.len;
        return tok;
}

char* str_to_cstr(Arena* a, Str s) {
        char* buf = arena_push_array(a, char, s.len + 1);
        memcpy(buf, s.ptr, s.len);
        buf[s.len] = '\0';
        return buf;
}

Str str_clone(Arena* a, Str s) {
        char* buf = arena_push_array(a, char, s.len ? s.len : 1);
        memcpy(buf, s.ptr, s.len);
        return str_buf(buf, s.len);
}

Str str_fmt(Arena* a, const char* fmt, ...) {
        va_list ap, ap2;
        va_start(ap, fmt);
        va_copy(ap2, ap);
        int n = vsnprintf(NULL, 0, fmt, ap);
        va_end(ap);
        if (n < 0) {
                va_end(ap2);
                return str_null();
        }
        char* buf = arena_push_array(a, char, (size_t)n + 1);
        vsnprintf(buf, (size_t)n + 1, fmt, ap2);
        va_end(ap2);
        return str_buf(buf, (size_t)n);
}

/* ---- str_from helpers --------------------------------------- */

static size_t _str_ws_len(Str s) {
        size_t n = 0;
        while (n < s.len && (unsigned char)s.ptr[n] <= ' ') n++;
        return n;
}

bool str_consume_i64(Str* s, int64_t* out) {
        size_t ws = _str_ws_len(*s);
        Str    r  = str_buf(s->ptr + ws, s->len - ws);
        if (!r.len) return false;

        size_t  i    = 0;
        int     neg  = 0;
        int64_t val  = 0;
        int     base = 10;

        if (r.ptr[i] == '-') {
                neg = 1;
                i++;
        } else if (r.ptr[i] == '+') {
                i++;
        }

        if (i + 1 < r.len && r.ptr[i] == '0' &&
            (r.ptr[i + 1] == 'x' || r.ptr[i + 1] == 'X')) {
                base = 16;
                i += 2;
        }

        size_t start = i;
        while (i < r.len) {
                char c = r.ptr[i];
                int  d;
                if (c >= '0' && c <= '9')
                        d = c - '0';
                else if (base == 16 && c >= 'a' && c <= 'f')
                        d = c - 'a' + 10;
                else if (base == 16 && c >= 'A' && c <= 'F')
                        d = c - 'A' + 10;
                else
                        break;
                val = val * base + d;
                i++;
        }
        if (i == start) return false;
        *out           = neg ? -val : val;
        size_t advance = ws + i;
        s->ptr += advance;
        s->len -= advance;
        return true;
}

bool str_consume_u64(Str* s, uint64_t* out) {
        size_t ws = _str_ws_len(*s);
        Str    r  = str_buf(s->ptr + ws, s->len - ws);
        if (!r.len) return false;

        size_t   i    = 0;
        uint64_t val  = 0;
        int      base = 10;

        if (r.ptr[i] == '+') i++;
        if (i + 1 < r.len && r.ptr[i] == '0' &&
            (r.ptr[i + 1] == 'x' || r.ptr[i + 1] == 'X')) {
                base = 16;
                i += 2;
        }

        size_t start = i;
        while (i < r.len) {
                char c = r.ptr[i];
                int  d;
                if (c >= '0' && c <= '9')
                        d = c - '0';
                else if (base == 16 && c >= 'a' && c <= 'f')
                        d = c - 'a' + 10;
                else if (base == 16 && c >= 'A' && c <= 'F')
                        d = c - 'A' + 10;
                else
                        break;
                val = val * (uint64_t)base + (uint64_t)d;
                i++;
        }
        if (i == start) return false;
        *out           = val;
        size_t advance = ws + i;
        s->ptr += advance;
        s->len -= advance;
        return true;
}

bool str_consume_f64(Str* s, double* out) {
        size_t ws = _str_ws_len(*s);
        Str    r  = str_buf(s->ptr + ws, s->len - ws);
        if (!r.len) return false;

        /* strtod needs a NUL-terminated string.
           Scan forward to find the end of the float token first so we
           can bound the copy, then NUL-terminate a stack buffer. */
        size_t n = 0;
        while (n < r.len) {
                char c = r.ptr[n];
                if ((c >= '0' && c <= '9') || c == '.' || c == '-' ||
                    c == '+' || c == 'e' || c == 'E' || c == 'n' || c == 'N' ||
                    c == 'a' || c == 'A' || c == 'f' || c == 'F' || c == 'i' ||
                    c == 'I')
                        n++;
                else
                        break;
        }
        if (n == 0) return false;

        /* copy into a stack buffer so strtod has a NUL-terminated string */
        char  tmp[64];
        char* buf;
        char* heap = NULL;
        if (n < sizeof(tmp)) {
                memcpy(tmp, r.ptr, n);
                tmp[n] = '\0';
                buf    = tmp;
        } else {
                heap = (char*)malloc(n + 1);
                if (!heap) return false;
                memcpy(heap, r.ptr, n);
                heap[n] = '\0';
                buf     = heap;
        }

        char*  end;
        double val      = strtod(buf, &end);
        size_t consumed = (size_t)(end - buf);
        if (heap) free(heap);
        if (consumed == 0) return false;

        *out           = val;
        size_t advance = ws + consumed;
        s->ptr += advance;
        s->len -= advance;
        return true;
}

bool str_consume_bool(Str* s, bool* out) {
        size_t ws      = _str_ws_len(*s);
        Str    r       = str_buf(s->ptr + ws, s->len - ws);
        size_t advance = 0;

        if (r.len >= 4 && (r.ptr[0] == 't' || r.ptr[0] == 'T') &&
            (r.ptr[1] == 'r' || r.ptr[1] == 'R') &&
            (r.ptr[2] == 'u' || r.ptr[2] == 'U') &&
            (r.ptr[3] == 'e' || r.ptr[3] == 'E')) {
                *out    = true;
                advance = 4;
        } else if (r.len >= 5 && (r.ptr[0] == 'f' || r.ptr[0] == 'F') &&
                   (r.ptr[1] == 'a' || r.ptr[1] == 'A') &&
                   (r.ptr[2] == 'l' || r.ptr[2] == 'L') &&
                   (r.ptr[3] == 's' || r.ptr[3] == 'S') &&
                   (r.ptr[4] == 'e' || r.ptr[4] == 'E')) {
                *out    = false;
                advance = 5;
        } else if (r.len >= 1 && r.ptr[0] == '1') {
                *out    = true;
                advance = 1;
        } else if (r.len >= 1 && r.ptr[0] == '0') {
                *out    = false;
                advance = 1;
        } else {
                return false;
        }

        s->ptr += ws + advance;
        s->len -= ws + advance;
        return true;
}

bool str_to_i64(Str s, int64_t* out) {
        return str_consume_i64(&s, out);
}
bool str_to_u64(Str s, uint64_t* out) {
        return str_consume_u64(&s, out);
}
bool str_to_f64(Str s, double* out) {
        return str_consume_f64(&s, out);
}
bool str_to_bool(Str s, bool* out) {
        return str_consume_bool(&s, out);
}

Str str_consume_while(Str* s, int (*pred)(int)) {
        const char* start = s->ptr;
        while (s->len > 0 && pred((unsigned char)s->ptr[0])) {
                s->ptr++;
                s->len--;
        }
        return str_buf(start, (size_t)(s->ptr - start));
}

Str str_consume_until(Str* s, int (*pred)(int)) {
        const char* start = s->ptr;
        while (s->len > 0 && !pred((unsigned char)s->ptr[0])) {
                s->ptr++;
                s->len--;
        }
        return str_buf(start, (size_t)(s->ptr - start));
}

/* ----------------------------------------------------------------
 *  4 impl - Map
 * ---------------------------------------------------------------- */

uint64_t map_hash_bytes(const void* key, size_t sz) {
        const uint8_t* p = (const uint8_t*)key;
        uint64_t       h = UINT64_C(14695981039346656037);
        size_t         i;
        for (i = 0; i < sz; ++i)
                h = (h ^ (uint64_t)p[i]) * UINT64_C(1099511628211);
        return h ? h : UINT64_C(1);
}

bool map_eq_bytes(const void* a, const void* b, size_t sz) {
        return memcmp(a, b, sz) == 0;
}

/*
 * Bucket stride: val is placed at AlignUp(sizeof(BucketHdr) + key_sz,
 * val_align) so values are always naturally aligned regardless of key_sz.
 * The whole bucket is then rounded up to 8 bytes for next-bucket alignment.
 */
size_t _map_bkt_stride(size_t key_sz, size_t val_sz, size_t val_align) {
        size_t val_off = AlignUp(sizeof(BucketHdr) + key_sz, val_align);
        return AlignUp(val_off + val_sz, 8);
}

static uint8_t* _map_bkt(Map* m, size_t i) {
        MapHdr* h = map_hdr(m);
        return (uint8_t*)m +
               i * _map_bkt_stride(h->key_sz, h->val_sz, h->val_align);
}

/* Pointer to key within a bucket (right after BucketHdr) */
static void* _bkt_key(uint8_t* b) {
        return b + sizeof(BucketHdr);
}

/* Pointer to value within a bucket, padded for alignment */
static void* _bkt_val(uint8_t* b, size_t key_sz, size_t val_align) {
        return b + AlignUp(sizeof(BucketHdr) + key_sz, val_align);
}

static size_t _next_pow2(size_t n) {
        size_t p = 1;
        if (IsPow2(n)) return n;
        while (p < n) p <<= 1;
        return p;
}

Map* _map_make(Arena* a,
               size_t key_sz,
               size_t val_sz,
               size_t val_align,
               size_t cap,
               uint64_t (*hash_fn)(const void*, size_t),
               bool (*eq_fn)(const void*, const void*, size_t)) {
        if (val_align < 1) val_align = 1;
        cap              = _next_pow2(Max(cap, 8));
        size_t   bstride = _map_bkt_stride(key_sz, val_sz, val_align);
        size_t   total   = MAP_HDR_OFFSET + cap * bstride;
        uint8_t* raw     = (uint8_t*)arena_push(a, total, 8);
        memset(raw, 0, total);
        MapHdr* hdr    = (MapHdr*)raw;
        hdr->arena     = a;
        hdr->len       = 0;
        hdr->cap       = cap;
        hdr->key_sz    = key_sz;
        hdr->val_sz    = val_sz;
        hdr->val_align = val_align;
        hdr->hash_fn   = hash_fn;
        hdr->eq_fn     = eq_fn;
        return (Map*)(raw + MAP_HDR_OFFSET);
}

void* _map_get(Map* m, const void* key) {
        MapHdr*  hdr = map_hdr(m);
        uint64_t h   = hdr->hash_fn(key, hdr->key_sz);
        size_t   idx = (size_t)(h & (hdr->cap - 1));
        uint32_t psl;
        for (psl = 0;; ++psl) {
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash == 0) return NULL;
                if (bh->psl < psl) return NULL;
                if (bh->hash == h && hdr->eq_fn(_bkt_key(b), key, hdr->key_sz))
                        return _bkt_val(b, hdr->key_sz, hdr->val_align);
                idx = (idx + 1) & (hdr->cap - 1);
        }
}

static void _map_insert(Map*        m,
                        uint64_t    hash,
                        const void* key,
                        const void* val) {
        MapHdr* hdr = map_hdr(m);
        size_t  bstride =
            _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);
        size_t idx = (size_t)(hash & (hdr->cap - 1));

#        if defined(_MSC_VER)
        uint8_t* tmp = (uint8_t*)malloc(bstride * 2);
        assert(tmp && "_map_insert: malloc failed");
        uint8_t* swap = tmp + bstride;
#        else
        uint8_t tmp[bstride];  /* C99 VLA */
        uint8_t swap[bstride]; /* C99 VLA */
#        endif

        memset(tmp, 0, bstride);
        BucketHdr* tmph = (BucketHdr*)tmp;
        tmph->hash      = hash;
        tmph->psl       = 0;
        memcpy(_bkt_key(tmp), key, hdr->key_sz);
        memcpy(_bkt_val(tmp, hdr->key_sz, hdr->val_align), val, hdr->val_sz);

        for (;;) {
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;

                if (bh->hash == 0) {
                        memcpy(b, tmp, bstride);
                        hdr->len++;
#        if defined(_MSC_VER)
                        free(tmp);
#        endif
                        return;
                }
                if (bh->hash == tmph->hash &&
                    hdr->eq_fn(_bkt_key(b), _bkt_key(tmp), hdr->key_sz)) {
                        memcpy(_bkt_val(b, hdr->key_sz, hdr->val_align),
                               _bkt_val(tmp, hdr->key_sz, hdr->val_align),
                               hdr->val_sz);
#        if defined(_MSC_VER)
                        free(tmp);
#        endif
                        return;
                }
                if (bh->psl < tmph->psl) {
                        memcpy(swap, b, bstride);
                        memcpy(b, tmp, bstride);
                        memcpy(tmp, swap, bstride);
                        tmph = (BucketHdr*)tmp;
                }
                tmph->psl++;
                idx = (idx + 1) & (hdr->cap - 1);
        }
}

void _map_set(Map** mp, const void* key, const void* val) {
        MapHdr* hdr = map_hdr(*mp);
        Arena*  a   = hdr->arena;

        if (hdr->len * 4 >= hdr->cap * 3) {
                Map*   new_m = _map_make(a,
                                         hdr->key_sz,
                                         hdr->val_sz,
                                         hdr->val_align,
                                         hdr->cap * 2,
                                         hdr->hash_fn,
                                         hdr->eq_fn);
                size_t i;
                for (i = 0; i < hdr->cap; ++i) {
                        uint8_t*   b  = _map_bkt(*mp, i);
                        BucketHdr* bh = (BucketHdr*)b;
                        if (bh->hash != 0)
                                _map_insert(
                                    new_m,
                                    bh->hash,
                                    _bkt_key(b),
                                    _bkt_val(b, hdr->key_sz, hdr->val_align));
                }
                *mp = new_m;
                hdr = map_hdr(*mp);
        }

        uint64_t h = hdr->hash_fn(key, hdr->key_sz);
        _map_insert(*mp, h, key, val);
}

bool _map_del(Map* m, const void* key) {
        MapHdr*  hdr = map_hdr(m);
        uint64_t h   = hdr->hash_fn(key, hdr->key_sz);
        size_t   idx = (size_t)(h & (hdr->cap - 1));
        size_t   bstride =
            _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);
        uint32_t psl;

        for (psl = 0;; ++psl) {
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;

                if (bh->hash == 0 || bh->psl < psl) return false;

                if (bh->hash == h &&
                    hdr->eq_fn(_bkt_key(b), key, hdr->key_sz)) {
                        for (;;) {
                                size_t     next = (idx + 1) & (hdr->cap - 1);
                                uint8_t*   nb   = _map_bkt(m, next);
                                BucketHdr* nbh  = (BucketHdr*)nb;
                                if (nbh->hash == 0 || nbh->psl == 0) {
                                        memset(b, 0, bstride);
                                        break;
                                }
                                memcpy(b, nb, bstride);
                                ((BucketHdr*)b)->psl--;
                                b   = nb;
                                idx = next;
                        }
                        hdr->len--;
                        return true;
                }

                idx = (idx + 1) & (hdr->cap - 1);
        }
}

bool map_next(Map* m, MapIter* it) {
        MapHdr* hdr = map_hdr(m);
        while (it->i < hdr->cap) {
                uint8_t*   b  = _map_bkt(m, it->i++);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0) {
                        it->key = _bkt_key(b);
                        it->val = _bkt_val(b, hdr->key_sz, hdr->val_align);
                        return true;
                }
        }
        return false;
}

Map* map_clone(Map* m, Arena* a) {
        MapHdr* hdr = map_hdr(m);
        size_t  bstride =
            _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);
        size_t   total = MAP_HDR_OFFSET + hdr->cap * bstride;
        uint8_t* raw   = (uint8_t*)arena_push(a, total, 8);
        memcpy(raw, (uint8_t*)m - MAP_HDR_OFFSET, total);
        Map* new_m            = (Map*)(raw + MAP_HDR_OFFSET);
        map_hdr(new_m)->arena = a;
        return new_m;
}

void* _map_keys(Map* m, Arena* a, size_t key_sz) {
        MapHdr* hdr  = map_hdr(m);
        void*   keys = _slice_make(a, key_sz, 8, hdr->len ? hdr->len : 1);
        size_t  i;
        for (i = 0; i < hdr->cap; i++) {
                uint8_t*   b  = _map_bkt(m, i);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0) keys = _slice_push_raw(keys, _bkt_key(b));
        }
        return keys;
}

void* _map_values(Map* m, Arena* a, size_t val_sz, size_t val_align) {
        MapHdr* hdr  = map_hdr(m);
        void*   vals = _slice_make(
            a, val_sz, val_align ? val_align : 8, hdr->len ? hdr->len : 1);
        size_t i;
        for (i = 0; i < hdr->cap; i++) {
                uint8_t*   b  = _map_bkt(m, i);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0)
                        vals = _slice_push_raw(
                            vals, _bkt_val(b, hdr->key_sz, hdr->val_align));
        }
        return vals;
}

uint64_t map_hash_str(const void* key, size_t sz) {
        Unused(sz);
        const Str* s = (const Str*)key;
        return map_hash_bytes(s->ptr, s->len);
}

bool map_eq_str(const void* a, const void* b, size_t sz) {
        Unused(sz);
        return str_eq(*(const Str*)a, *(const Str*)b);
}

#endif /* BARESTD_IMPLEMENTATION */
#endif /* BARESTD_H */
