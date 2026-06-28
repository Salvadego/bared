/*
 * barestd.h - arenas, slices, strings, maps, dynamic arrays
 * =============================================================
 *
 *  USAGE
 *    In exactly one translation unit:
 *
 *      #define BARESTD_IMPLEMENTATION
 *      #include "barestd.h"
 *
 *    Everywhere else:
 *
 *      #include "barestd.h"
 *
 *  DEPENDS ON
 *    baredefs.h       (assertions, sizing macros, BAREDEF linkage)
 *    bareplatform.h   (virtual memory, only for allocator_arena's
 *                      optional VM-backed growth path; everything
 *                      else in this file is plain, portable C99)
 *
 *  PORTABILITY
 *    This file compiles, with no warnings, under:
 *      gcc   -std=c99 -pedantic-errors -Wall -Wextra
 *      clang -std=c99 -pedantic-errors -Wall -Wextra
 *      MSVC  /W4 (C11 mode; MSVC has never shipped a conforming C99)
 *    and cross-compiles and links for Windows via MinGW. No GNU or
 *      Clang statement-expression or __typeof__ extension is used
 *      anywhere in this file. See barecompilers.h for what that
 *      buys you and why it matters: a feature that needs a compiler
 *      extension to spell is a feature a studio shipping on three
 *      compilers gets to debug three times instead of once.
 *
 *  MODULES
 *    Arena       bump-pointer allocator, scratch save/restore points
 *    AllocStats  allocation accounting (bytes current/peak, counts)
 *    Allocator   explicit vtable allocator (libc-backed and
 *                arena-backed implementations provided)
 *    DynArray    malloc-backed growable array with its own lifetime,
 *                independent of any arena
 *    Slice       arena-backed growable array with a hidden header
 *    Str         non-owning string view
 *    Map         open-addressing hash map (Robin Hood hashing)
 *
 *  WHY AN ARENA AT ALL
 *    Individually freeing every small allocation a program makes is
 *    not free: each call to free() has to find and merge the right
 *    bookkeeping, and a long-running program that allocates and frees
 *    constantly pays that cost on every single object. An arena flips
 *    the trade: pay for bookkeeping once per *batch* of allocations
 *    (one bump of a pointer, no bookkeeping at all, really) and release
 *    the whole batch at once when its natural lifetime ends -- end of
 *    frame, end of level, end of request. This is also why arenas
 *    compose so well with virtual memory (see bareplatform.h): reserve
 *    a generous address range once, commit pages into it as the arena
 *    grows, and the arena's base address never has to move, which
 *    means nothing holding a pointer into it is ever invalidated by
 *    growth the way a realloc-based dynamic array would invalidate it.
 */
#ifndef BARESTD_H
#define BARESTD_H

/* barestd.h's VM-backed allocator path needs bareplatform.h's
   implementation; requesting BARESTD_IMPLEMENTATION pulls it in
   automatically so callers only ever need to remember one macro. */
#if defined(BARESTD_IMPLEMENTATION) && !defined(BAREPLATFORM_IMPLEMENTATION)
#        define BAREPLATFORM_IMPLEMENTATION
#endif

#include "baredefs.h"
#include "bareplatform.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BARE_MAX_ALLOCATOR_STACK
#        define BARE_MAX_ALLOCATOR_STACK 32
#endif
#ifndef BARE_MAX_ARENA_CHAIN
#        define BARE_MAX_ARENA_CHAIN 4096
#endif

/* ================================================================
 *  AllocStats - allocation accounting
 *
 *  Every Allocator and every Arena can optionally report into one of
 *  these. NULL ("don't track") is always a valid stats pointer
 *  everywhere one is accepted.
 * ================================================================ */

typedef struct {
        size_t   bytes_current;
        size_t   bytes_peak;
        size_t   bytes_total;
        uint64_t alloc_count;
        uint64_t free_count;
        uint64_t resize_count;
} AllocStats;

/*
 * alloc_stats_record_alloc - record a new allocation
 * @stats: target stats block, or NULL to do nothing
 * @size: size in bytes of the allocation just made; may be 0
 */
BAREDEF void alloc_stats_record_alloc(AllocStats* stats, size_t size);

/*
 * alloc_stats_record_free - record that an allocation was released
 * @stats: target stats block, or NULL to do nothing
 * @size: size in bytes previously passed to record_alloc or
 *        record_resize for this allocation
 */
BAREDEF void alloc_stats_record_free(AllocStats* stats, size_t size);

/*
 * alloc_stats_record_resize - record that an allocation changed size
 * @stats: target stats block, or NULL to do nothing
 * @old_size: the size being resized away from
 * @new_size: the size being resized to
 */
BAREDEF void alloc_stats_record_resize(AllocStats* stats,
                                       size_t      old_size,
                                       size_t      new_size);

/* Forward declaration: Arena is defined further down, but
   allocator_arena() needs to name Arena* before that point. */
typedef struct Arena Arena;

/* ================================================================
 *  Allocator - an explicit, swappable allocation interface
 *
 *  An Allocator is two words: a function pointer and a context
 *  pointer. Every allocation-capable function in application code
 *  that wants to support more than "always use malloc" should take
 *  one as a parameter, the same way it would take any other
 *  dependency, rather than reach for a global. Two implementations
 *  ship here: allocator_default() (malloc/realloc/free) and
 *  allocator_arena() (push-only, bulk-freed through the Arena it
 *  wraps). Application code is free to implement its own by writing
 *  an AllocatorProc.
 * ================================================================ */

typedef enum {
        ALLOCATOR_MODE_ALLOC = 0,
        ALLOCATOR_MODE_FREE,
        ALLOCATOR_MODE_RESIZE,
        ALLOCATOR_MODE_FREE_ALL,
        ALLOCATOR_MODE_QUERY_STATS,
} AllocatorMode;

typedef struct Allocator Allocator;

/*
 * AllocatorProc - the single entry point of an allocator implementation
 *
 * Switches on @mode: ALLOC and RESIZE return the new pointer (NULL on
 * failure); FREE and FREE_ALL return NULL unconditionally, their
 * return value unused; QUERY_STATS returns an AllocStats* (or NULL if
 * the allocator does not track statistics).
 */
typedef void* (*AllocatorProc)(void*         allocator_context,
                               AllocatorMode mode,
                               size_t        size,
                               size_t        old_size,
                               void*         old_ptr,
                               size_t        align);

struct Allocator {
        AllocatorProc proc;
        void*         context;
};

/* allocator_make - build an Allocator from a proc and its context */
BAREDEF Allocator allocator_make(AllocatorProc proc, void* context);

/*
 * allocator_default - the libc malloc/realloc/free allocator
 * @stats: optional accounting target, or NULL
 *
 * Stateless and free to call as often as you like: the returned
 * value is just @stats stored directly as the context pointer, so
 * there is no allocation here to leak or to free later. Does not
 * support ALLOCATOR_MODE_FREE_ALL (a flat heap has no way to free
 * "everything this allocator owns" -- it owns nothing centrally).
 */
BAREDEF Allocator allocator_default(AllocStats* stats);

/*
 * allocator_arena - an Allocator backed by an existing Arena
 * @a: the arena to allocate from; must outlive the returned Allocator
 *
 * ALLOC pushes onto @a. RESIZE grows by pushing a fresh block and
 * copying (an arena cannot grow a block in place once something else
 * may have been pushed after it). FREE is a deliberate no-op, since
 * an arena has no notion of one block's individual lifetime. FREE_ALL
 * calls arena_reset(a) -- this is the allocator to reach for whenever
 * "free everything at once" is the operation you actually want.
 */
BAREDEF Allocator allocator_arena(Arena* a);

/*
 * allocator_alloc - allocate through an Allocator
 * @a: the allocator to use
 * @size: bytes to allocate; 0 always returns NULL without calling @a
 * @align: required alignment in bytes, a power of two (0 means "the
 *         allocator's own natural minimum", typically max_align_t)
 *
 * Return: a pointer to @size freshly allocated bytes, or NULL on
 * failure. The memory's contents are unspecified (not zeroed) unless
 * the specific Allocator implementation documents otherwise.
 */
BAREDEF void* allocator_alloc(Allocator a, size_t size, size_t align);

/*
 * allocator_resize - grow or shrink a previous allocation
 * @a: the allocator that produced @old_ptr
 * @old_ptr: the pointer being resized, or NULL (meaning "allocate
 *           fresh", in which case @old_size must be 0)
 * @old_size: the size @old_ptr was allocated or last resized to
 * @new_size: the desired new size
 * @align: required alignment, as for allocator_alloc()
 *
 * The first @old_size bytes of content are preserved (or as many as
 * fit, if @new_size < @old_size). On failure, @old_ptr remains valid
 * and unchanged -- the same guarantee the C standard makes for
 * realloc().
 *
 * Return: a pointer to the resized allocation, or NULL on failure.
 */
BAREDEF void* allocator_resize(
    Allocator a, void* old_ptr, size_t old_size, size_t new_size, size_t align);

/*
 * allocator_free - release a single allocation
 * @a: the allocator that produced @ptr
 * @ptr: the pointer to release, or NULL (meaning "do nothing", in
 *       which case @size must be 0)
 * @size: the size @ptr was allocated or last resized to
 *
 * What "release" means depends on the Allocator: allocator_default()
 * frees @ptr immediately; allocator_arena() does nothing at all,
 * since an arena has no notion of one block's individual lifetime
 * (see allocator_arena's own documentation above).
 */
BAREDEF void allocator_free(Allocator a, void* ptr, size_t size);

/*
 * allocator_free_all - release every allocation this allocator owns
 * @a: the allocator to reset
 *
 * Not every Allocator implementation can support this: a flat heap
 * (allocator_default()) has no central registry of its own
 * outstanding blocks to free en masse, and asserts if you call this
 * on one. allocator_arena() supports it directly, as arena_reset().
 */
BAREDEF void allocator_free_all(Allocator a);

/*
 * allocator_query_stats - retrieve the AllocStats this allocator was
 * constructed with, if any
 * @a: the allocator to query
 *
 * Return: the AllocStats* passed when @a was constructed
 * (allocator_default()'s @stats argument, or the Arena's via
 * arena_set_stats() for allocator_arena()), or NULL if none was
 * provided.
 */
BAREDEF AllocStats* allocator_query_stats(Allocator a);

/*
 * Implicit allocator context, opt-in only: a fixed-size, per-thread
 * stack of Allocator values. Nothing in this file consults it
 * automatically -- code that wants "ambient" allocator behavior
 * calls allocator_context_current() itself at its own call sites.
 * Everything else stays explicit.
 */

/*
 * allocator_context_push - push @a onto this thread's allocator stack
 * @a: the allocator to make current
 *
 * Asserts if the stack is already at BARE_MAX_ALLOCATOR_STACK depth.
 * Every push must be matched by exactly one allocator_context_pop().
 */
BAREDEF void allocator_context_push(Allocator a);

/*
 * allocator_context_pop - pop the most recently pushed allocator
 *
 * Asserts if the stack is already empty (an unbalanced pop).
 */
BAREDEF void allocator_context_pop(void);

/*
 * allocator_context_current - the allocator at the top of this
 * thread's allocator stack
 *
 * Return: the most recently pushed Allocator, or
 * allocator_default(NULL) if the stack is empty.
 */
BAREDEF Allocator allocator_context_current(void);

/* ================================================================
 *  Arena - bump-pointer allocator
 * ================================================================ */

struct Arena {
        uint8_t*    buf;
        size_t      pos;
        size_t      cap;
        Arena*      next;
        int         owned;
        AllocStats* stats;
};

/* arena_from_buf - wrap a caller-owned buffer as an arena; never freed */
BAREDEF Arena arena_from_buf(void* buf, size_t cap);

/* arena_new - allocate a new arena of @cap bytes from the heap */
BAREDEF Arena* arena_new(size_t cap);

/*
 * arena_push - allocate @size bytes aligned to @align from @a
 *
 * Grows @a's overflow chain automatically if @a is full; the returned
 * pointer is always valid for the arena's remaining lifetime,
 * regardless of which chunk in the chain it came from.
 */
BAREDEF void* arena_push(Arena* a, size_t size, size_t align);

/* arena_pop_to - rewind @a to a position saved earlier (e.g. by Scratch) */
BAREDEF void arena_pop_to(Arena* a, size_t saved_pos);

/* arena_reset - rewind @a and its entire overflow chain to empty */
BAREDEF void arena_reset(Arena* a);

/* arena_free - release @a and its overflow chain back to the heap */
BAREDEF void arena_free(Arena* a);

/* arena_set_stats - attach (or detach, with NULL) an accounting target */
BAREDEF void arena_set_stats(Arena* a, AllocStats* stats);

/* arena_push_type - allocate one @T, correctly sized and aligned, from @a */
#define arena_push_type(a, T) ((T*)arena_push((a), sizeof(T), AlignOfType(T)))

/* arena_push_array - allocate an array of @n @T elements from @a */
#define arena_push_array(a, T, n) \
        ((T*)arena_push((a), sizeof(T) * (n), AlignOfType(T)))

/*
 * Scratch - a save point for temporary, scoped arena allocations
 *
 *   Scratch s = scratch_begin(a);
 *   ... push temporary things onto a ...
 *   scratch_end(s);   // a is rewound to exactly where it was
 */
typedef struct {
        Arena* a;
        size_t pos;
} Scratch;

/*
 * scratch_begin - record @a's current position as a save point
 * @a: the arena to save a position in
 *
 * Return: a Scratch value to later pass to scratch_end() to rewind
 * @a back to exactly this position, releasing everything pushed onto
 * it in between.
 */
BAREDEF Scratch scratch_begin(Arena* a);

/*
 * scratch_end - rewind an arena to a previously saved position
 * @s: a Scratch value returned by an earlier scratch_begin()
 *
 * Equivalent to arena_pop_to(s.a, s.pos).
 */
BAREDEF void scratch_end(Scratch s);

/* ================================================================
 *  DynArray - a malloc-backed growable array with its own lifetime
 *
 *  A Slice (below) is deliberately arena-only: it has no individual
 *  lifetime, which is the entire point of an arena. Some data really
 *  does need a classic grow/free dynamic array with an independent
 *  lifetime instead -- a parser's result the caller will own, a
 *  registry that outlives any one frame. DynArray is that.
 *
 *  Unlike a templated container, these macros never name the element
 *  type. They only ever write `(da)->items[...]`, `(da)->len`, and
 *  `(da)->cap` -- plain member access -- so the compiler infers the
 *  element type from whatever concrete struct the caller already
 *  declared, the same way it would for any other struct member
 *  assignment. The one thing required of that struct is that it has
 *  exactly these field names, types, and order:
 *
 *    typedef struct {
 *            T*        items;
 *            size_t    len;
 *            size_t    cap;
 *            Allocator allocator;
 *    } MyArray;
 *
 *  DYNARRAY_FIELDS(T) expands to exactly that field list, so the
 *  typedef above can be written once as:
 *
 *    typedef struct { DYNARRAY_FIELDS(int); } IntArray;
 *
 *  and from then on:
 *
 *    IntArray xs = {0};
 *    xs.allocator = allocator_default(NULL);
 *    da_append(&xs, 10);
 *    da_append(&xs, 20);
 *    for (size_t i = 0; i < xs.len; i++) printf("%d\n", xs.items[i]);
 *    da_free(&xs);
 *
 *  Declaring one named struct per distinct element type, instead of
 *  one anonymous type per call site, is also strictly better for
 *  debugging: a debugger and a compiler error message both show
 *  "IntArray", not an anonymous struct's mangled internal name.
 *
 *  ALIGNMENT CAVEAT
 *    Because these macros never name the element type, they cannot
 *    compute that type's natural alignment either (doing so without
 *    naming the type needs __typeof__ or _Generic, exactly the
 *    extensions this file avoids). da_reserve therefore requests a
 *    conservative pointer-sized alignment from the allocator, which
 *    is always correct for allocator_default() (malloc/realloc
 *    already over-align everything to max_align_t regardless of what
 *    you ask for) but is NOT guaranteed sufficient for
 *    allocator_arena() if T's natural alignment exceeds that of a
 *    pointer -- a SIMD vector type with 16- or 32-byte alignment, for
 *    instance. If you need an arena-backed DynArray of an over-aligned
 *    type, either pad the type's size to a multiple of its alignment
 *    and accept the waste, or allocate that one array with
 *    allocator_default() instead and keep the rest of your data in
 *    the arena.
 * ================================================================ */

#define DYNARRAY_FIELDS(T)\
        T*        items;  \
        size_t    len;    \
        size_t    cap;    \
        Allocator allocator

/*
 * da_reserve - ensure room for at least @min_cap elements
 * @da: pointer to any struct shaped like DYNARRAY_FIELDS(T)
 * @min_cap: minimum capacity to guarantee
 *
 * Grows geometrically (doubling, or to @min_cap, whichever is larger)
 * when the current capacity is insufficient; a no-op otherwise.
 */
#define da_reserve(da, min_cap)                                          \
        Statement(                                                       \
            if ((size_t)(min_cap) > (da)->cap) {                         \
                    size_t _bare_new_cap = (da)->cap ? (da)->cap * 2 : 4;\
                    while (_bare_new_cap < (size_t)(min_cap)) {          \
                            _bare_new_cap *= 2;                          \
                    }                                                    \
                    (da)->items = allocator_resize(                      \
                        (da)->allocator,                                 \
                        (da)->items,                                     \
                        (da)->cap * sizeof(*(da)->items),                \
                        _bare_new_cap * sizeof(*(da)->items),            \
                        AlignOfType(void*));                             \
                    Assert((da)->items != NULL,                          \
                           "da_reserve: allocator_resize must succeed"); \
                    (da)->cap = _bare_new_cap;                           \
            })

/*
 * da_append - append @value, growing the array first if necessary
 * @da: pointer to any struct shaped like DYNARRAY_FIELDS(T)
 * @value: the value to append; its type must match (da)->items[0]'s
 */
#define da_append(da, value)                  \
        Statement(                            \
            da_reserve((da), (da)->len + 1);  \
            (da)->items[(da)->len] = (value); \
            (da)->len++;)

/*
 * da_append_many - append @count elements from @src in one call
 * @da: pointer to any struct shaped like DYNARRAY_FIELDS(T)
 * @src: pointer to the first of @count elements to copy in
 * @count: how many elements to copy
 *
 * Equivalent to @count calls to da_append, but reserves once instead
 * of once per element, which matters when @count is large: growth is
 * amortized across the whole batch instead of possibly happening
 * partway through it.
 */
#define da_append_many(da, src, count)                \
        Statement(                                    \
            size_t _bare_count = (size_t)(count);     \
            da_reserve((da), (da)->len + _bare_count);\
            memcpy((da)->items + (da)->len,           \
                  (src),                              \
                  _bare_count * sizeof(*(da)->items));\
            (da)->len += _bare_count;)

/*
 * da_free - release the backing storage
 * @da: pointer to any struct shaped like DYNARRAY_FIELDS(T)
 *
 * Leaves the array's bookkeeping zeroed and immediately reusable with
 * da_append again, exactly like a freshly zero-initialized one.
 */
#define da_free(da)                                         \
        Statement(                                          \
            allocator_free((da)->allocator,                 \
                          (da)->items,                      \
                          (da)->cap * sizeof(*(da)->items));\
            (da)->items = NULL;                             \
            (da)->len   = 0;                                \
            (da)->cap   = 0;)

/* da_at - bounds-checked element access, asserting on an out-of-range index */
#define da_at(da, i) ((da)->items[_bare_bounds_check((size_t)(i), (da)->len)])

/* da_last - bounds-checked access to the final element; asserts on an empty array */
#define da_last(da) ((da)->items[_bare_bounds_check((da)->len - 1, (da)->len)])

/* da_clear - set length to zero without releasing the backing storage */
#define da_clear(da) ((void)((da)->len = 0))

/* da_is_empty - true if the array currently holds no elements */
#define da_is_empty(da) ((da)->len == 0)

/*
 * _bare_bounds_check - the shared bounds check behind da_at, da_last,
 * slice_at, and slice_last
 * @i: the index being accessed
 * @len: the current length of the container being indexed
 *
 * Returns @i unchanged so it can be used directly as an array
 * subscript; asserts (rather than silently clamping or wrapping)
 * when @i is out of range, since either of those alternatives would
 * turn a caller's bug into a different, harder-to-find bug instead
 * of surfacing it immediately.
 */
static inline size_t _bare_bounds_check(size_t i, size_t len) {
        Assert(i < len, "index out of bounds");
        return i;
}

/* ================================================================
 *  Slice - an arena-backed growable array with a hidden header
 *
 *  The header (length, capacity, stride, owning arena) lives just
 *  before the data pointer in memory, so a Slice(T) is just a T*
 *  everywhere except where you grow it -- it can be passed to any
 *  function expecting a plain array.
 * ================================================================ */

/* SliceHdr - the hidden header living immediately before a Slice's data */
typedef struct {
        Arena* arena;      /* the arena every growth reallocates from          */
        size_t len;        /* elements currently in use                        */
        size_t cap;        /* elements the current backing store can hold      */
        size_t stride;     /* bytes per element                                */
        size_t elem_align; /* required alignment per element, clamped to <= 16 */
} SliceHdr;

/* SLICE_HDR_OFFSET - byte distance from a SliceHdr to its data pointer */
#define SLICE_HDR_OFFSET AlignUp(sizeof(SliceHdr), 16)

/* slice_hdr - recover the hidden SliceHdr* from a Slice(T) data pointer */
#define slice_hdr(s) ((SliceHdr*)((uint8_t*)(s) - SLICE_HDR_OFFSET))

/* slice_len - number of elements currently in @s */
#define slice_len(s) (slice_hdr(s)->len)

/* slice_cap - number of elements @s's current backing store can hold */
#define slice_cap(s) (slice_hdr(s)->cap)

/* slice_stride - bytes per element in @s */
#define slice_stride(s) (slice_hdr(s)->stride)

/* Slice(T) - the type of a growable array of T; expands to plain T* */
#define Slice(T) T*

/*
 * _slice_make - allocate a fresh slice's backing storage
 * @a: arena to allocate from
 * @stride: bytes per element
 * @elem_align: required per-element alignment (clamped to [1, 16])
 * @cap: initial capacity in elements
 *
 * Return: a Slice(T) data pointer (cast by the slice_make() macro to
 * the right T*) with length 0 and capacity @cap.
 */
BAREDEF void* _slice_make(Arena* a, size_t stride, size_t elem_align, size_t cap);

/*
 * _slice_push_raw - the type-erased append behind slice_put/slice_push
 * @s: a Slice(T) data pointer
 * @elem: pointer to one element's worth of bytes to copy in
 *
 * Grows @s (reallocating from its owning arena and copying existing
 * elements across) if it is already at capacity.
 *
 * Return: the (possibly relocated) Slice(T) data pointer; callers
 * must always assign this back to their slice variable, which is
 * exactly what slice_put() and slice_push() do for you.
 */
BAREDEF void* _slice_push_raw(void* s, const void* elem);

/*
 * _slice_contains_raw - the type-erased linear search behind slice_contains
 * @s: a Slice(T) data pointer (or any array of @len elements of @stride bytes)
 * @val: pointer to the value to search for
 * @len: number of elements to search
 * @stride: bytes per element
 *
 * Return: true if any element compares equal to @val via memcmp.
 */
BAREDEF bool _slice_contains_raw(const void* s,
                                 const void* val,
                                 size_t      len,
                                 size_t      stride);

/* slice_make - allocate a new Slice(T) of capacity @cap from arena @a */
#define slice_make(a, T, cap) \
        ((T*)_slice_make((a), sizeof(T), AlignOfType(T), (size_t)(cap)))

/*
 * slice_put - append the lvalue @val to slice @s, growing if necessary
 *
 * Unlike slice_push(), @val must already be an lvalue of the slice's
 * element type (so its address can be taken directly); use this when
 * you already have a named variable to append rather than a literal
 * or other non-addressable expression.
 */
#define slice_put(s, val)                          \
        do {                                       \
                (s) = _slice_push_raw((s), &(val));\
        } while (0)

/*
 * slice_push - append @val to slice @s, growing if necessary
 * @s: a Slice(T) variable (not a pointer to one -- the macro itself
 *     takes care of reassigning it if growth relocates the data)
 * @T: the slice's element type
 * @val: any expression of type T, including a literal
 */
#define slice_push(s, T, val)                                  \
        do {                                                   \
                T _bare_v = (val);                             \
                (s)       = (T*)_slice_push_raw((s), &_bare_v);\
        } while (0)

/*
 * slice_remove - remove the element at index @i, shifting later
 * elements left to close the gap
 * @s: a Slice(T) variable
 * @i: the index to remove; asserts if out of range
 *
 * O(n) in the number of elements after @i, since they all shift down
 * by one position. Use slice_pop() instead if you only ever need to
 * remove from the end.
 */
#define slice_remove(s, i)                                                          \
        do {                                                                        \
                size_t    _bare_i = (size_t)(i);                                    \
                SliceHdr* _bare_h = slice_hdr(s);                                   \
                Assert(_bare_i < _bare_h->len, "slice_remove: index out of bounds");\
                memmove((uint8_t*)(s) + _bare_i * _bare_h->stride,                  \
                        (uint8_t*)(s) + (_bare_i + 1) * _bare_h->stride,            \
                        (_bare_h->len - _bare_i - 1) * _bare_h->stride);            \
                _bare_h->len--;                                                     \
        } while (0)

/* slice_contains - true if @val (an lvalue) is present anywhere in @s */
#define slice_contains(s, val) \
        _slice_contains_raw((s), &(val), slice_len(s), slice_stride(s))

/*
 * slice_pop - drop the last element by decrementing the length
 *
 * A no-op (not an assertion failure) if @s is already empty, since
 * "pop an empty collection" is common enough at the end of a loop
 * that asserting on it would push every caller to add their own
 * is-empty check first for no real benefit. Use slice_last() right
 * before popping if you need the value that is about to be dropped.
 */
#define slice_pop(s)                                            \
        do {                                                    \
                if (slice_hdr(s)->len > 0) slice_hdr(s)->len--; \
        } while (0)

/* slice_at - bounds-checked element access, asserting on an out-of-range index */
#define slice_at(s, i) (s)[_bare_bounds_check((size_t)(i), slice_len(s))]

/* slice_last - bounds-checked access to the final element; asserts on an empty slice */
#define slice_last(s) (s)[_bare_bounds_check(slice_len(s) - 1, slice_len(s))]

/* slice_clear - set length to zero without releasing the backing storage */
#define slice_clear(s) ((void)(slice_hdr(s)->len = 0))

/* ================================================================
 *  Str - a non-owning UTF-8 string view
 *
 *  A Str never owns the bytes it points at: it is exactly a pointer
 *  and a length, the same shape as Go's string or Rust's &str. This
 *  means slicing, trimming, and tokenizing are all zero-allocation --
 *  they return a new Str pointing into the same bytes -- and it means
 *  a Str is only ever as valid as whatever it points into. Use
 *  str_clone() when you need bytes that outlive the original buffer.
 * ================================================================ */

typedef struct {
        const char* ptr;
        size_t      len;
} Str;

/* str_lit - build a Str from a string literal at compile time, e.g. str_lit("hi") */
#define str_lit(s) ((Str){(s), sizeof(s) - 1})

/*
 * str_from_c - build a Str viewing a NUL-terminated C string
 * @cstr: the C string to view, or NULL
 *
 * Return: a Str of length strlen(cstr), or str_null() if @cstr is NULL.
 */
static inline Str str_from_c(const char* cstr) {
        Str s;
        s.ptr = cstr;
        s.len = cstr ? strlen(cstr) : 0;
        return s;
}

/* str_buf - build a Str viewing @n bytes starting at @p; @p need not be NUL-terminated */
#define str_buf(p, n) ((Str){(const char*)(p), (size_t)(n)})

/* str_null - the empty, pointer-less Str; the canonical "no value" Str */
#define str_null() ((Str){NULL, 0})

/* str_is_null - true if @s's pointer is NULL (the str_null() convention) */
#define str_is_null(s) ((s).ptr == NULL)

/* str_is_empty - true if @s has zero length, regardless of its pointer */
#define str_is_empty(s) ((s).len == 0)

/* StrFmt / StrArgs - printf a Str without a temporary C string:
   printf(StrFmt, StrArgs(s)) */
#define StrFmt     "%.*s"
#define StrArgs(s) (int)(s).len, (s).ptr

/*
 * str_eq - byte-for-byte equality
 * @a: first string
 * @b: second string
 *
 * Two empty strings compare equal regardless of whether either's
 * pointer is NULL (the str_null() convention), so str_eq never
 * dereferences either pointer when both lengths are zero.
 *
 * Return: true if @a and @b have the same length and the same bytes.
 */
BAREDEF bool str_eq(Str a, Str b);

/*
 * str_has_prefix - true if @s begins with @pfx
 *
 * An empty @pfx is a prefix of everything, including an empty @s.
 */
BAREDEF bool str_has_prefix(Str s, Str pfx);

/*
 * str_has_suffix - true if @s ends with @sfx
 *
 * An empty @sfx is a suffix of everything, including an empty @s.
 */
BAREDEF bool str_has_suffix(Str s, Str sfx);

/*
 * str_find - locate the first occurrence of @needle in @hay
 * @hay: the string to search within
 * @needle: the string to search for; an empty @needle matches at
 *          index 0 of any @hay, including an empty one
 *
 * Return: the byte offset of the first match, or -1 if @needle does
 * not occur in @hay.
 */
BAREDEF ptrdiff_t str_find(Str hay, Str needle);

/*
 * str_slice - a sub-view of @s spanning [@lo, @hi)
 * @s: the string to slice
 * @lo: start offset, inclusive
 * @hi: end offset, exclusive; asserts if @hi < @lo or @hi > s.len
 *
 * Return: a Str of length (@hi - @lo) pointing into @s's own bytes;
 * no copy is made.
 */
BAREDEF Str str_slice(Str s, size_t lo, size_t hi);

/* str_trim_left - @s with every leading byte that appears in @cutset removed */
BAREDEF Str str_trim_left(Str s, Str cutset);

/* str_trim_right - @s with every trailing byte that appears in @cutset removed */
BAREDEF Str str_trim_right(Str s, Str cutset);

/* str_trim - @s with every leading and trailing byte in @cutset removed */
BAREDEF Str str_trim(Str s, Str cutset);

/*
 * str_next_token - split off and consume the next token delimited by @sep
 * @s: in/out: the remaining input; advanced past the returned token
 *     and its trailing separator on each call
 * @sep: the separator to split on; must not be empty
 *
 * Designed for a `while (!str_is_null(tok = str_next_token(&s, sep)))`
 * loop: returns str_null() once @s is fully consumed, and otherwise
 * returns each token in turn, including empty ones between adjacent
 * separators.
 *
 * Return: the next token, or str_null() if @s was already empty.
 */
BAREDEF Str str_next_token(Str* s, Str sep);

/*
 * str_consume_while - consume and return a leading run matching @pred
 * @s: in/out: advanced past the consumed run
 * @pred: an <ctype.h>-style predicate (isalpha, isdigit, ...) or any
 *        function of the same shape
 *
 * Return: the consumed run, possibly empty if @s did not start with
 * a byte satisfying @pred.
 */
BAREDEF Str str_consume_while(Str* s, int (*pred)(int));

/*
 * str_consume_until - consume and return a leading run NOT matching @pred
 * @s: in/out: advanced past the consumed run
 * @pred: an <ctype.h>-style predicate or any function of the same shape
 *
 * Return: the consumed run, possibly empty if @s started with a byte
 * already satisfying @pred.
 */
BAREDEF Str str_consume_until(Str* s, int (*pred)(int));

/*
 * str_to_cstr - copy @s into a freshly NUL-terminated buffer
 * @a: arena to allocate the buffer from
 * @s: the string to copy
 *
 * Return: a NUL-terminated copy of @s's bytes, valid for @a's lifetime.
 */
BAREDEF char* str_to_cstr(Arena* a, Str s);

/*
 * str_clone - copy @s's bytes into @a, producing an owned Str
 * @a: arena to allocate the copy from
 * @s: the string to copy
 *
 * Use this whenever a Str needs to outlive the buffer it currently
 * points into.
 *
 * Return: a Str of the same length as @s, pointing into @a instead.
 */
BAREDEF Str str_clone(Arena* a, Str s);

/*
 * str_fmt - printf-style formatting into an arena-owned Str
 * @a: arena to allocate the result from
 * @fmt: a printf format string
 *
 * Return: the formatted result, or str_null() if formatting itself
 * failed (which only happens for a malformed @fmt or a vsnprintf
 * implementation error, never for a valid format applied to valid
 * arguments).
 */
BAREDEF Str str_fmt(Arena* a, const char* fmt, ...);

/*
 * str_consume_i64 - parse a leading signed integer, advancing @s past it
 * @s: in/out: advanced past any leading whitespace and the parsed
 *     number itself on success; left unchanged on failure
 * @out: where to store the parsed value
 *
 * Accepts an optional leading '+' or '-', and an optional "0x"/"0X"
 * prefix for hexadecimal.
 *
 * Return: true if a number was found and parsed, false otherwise
 * (leaving @s and *@out untouched).
 */
BAREDEF bool str_consume_i64(Str* s, int64_t* out);

/* str_consume_u64 - like str_consume_i64, but unsigned and without a leading '-' */
BAREDEF bool str_consume_u64(Str* s, uint64_t* out);

/*
 * str_consume_f64 - parse a leading floating-point number, advancing
 * @s past it
 * @s: in/out: advanced past the parsed number on success
 * @out: where to store the parsed value
 *
 * Delegates to the platform's strtod() after isolating the candidate
 * substring, so it accepts the same syntax strtod() does (including
 * "inf" and "nan" spellings).
 *
 * Return: true if a number was found and parsed, false otherwise.
 */
BAREDEF bool str_consume_f64(Str* s, double* out);

/*
 * str_consume_bool - parse a leading boolean, advancing @s past it
 * @s: in/out: advanced past the parsed token on success
 * @out: where to store the parsed value
 *
 * Accepts "true"/"false" (case-insensitive) or a single "1"/"0" digit.
 *
 * Return: true if a recognized token was found, false otherwise.
 */
BAREDEF bool str_consume_bool(Str* s, bool* out);

/* str_to_i64 - parse @s as a signed integer without modifying it; see str_consume_i64 */
BAREDEF bool str_to_i64(Str s, int64_t* out);

/* str_to_u64 - parse @s as an unsigned integer without modifying it; see str_consume_u64 */
BAREDEF bool str_to_u64(Str s, uint64_t* out);

/* str_to_f64 - parse @s as a floating-point number without modifying it; see str_consume_f64 */
BAREDEF bool str_to_f64(Str s, double* out);

/* str_to_bool - parse @s as a boolean without modifying it; see str_consume_bool */
BAREDEF bool str_to_bool(Str s, bool* out);

/* ================================================================
 *  Map - an open-addressing hash map using Robin Hood hashing
 *
 *  Robin Hood hashing keeps probe-sequence lengths balanced across
 *  entries (an entry that has probed further than another "steals"
 *  the slot and pushes the other one onward), which bounds worst-case
 *  lookup cost much tighter than naive linear probing under the same
 *  load factor. Keys and values are copied by value into bucket
 *  storage. A bucket's hash field doubles as its occupancy flag, so a
 *  hash function must never return 0 -- map_hash_bytes and
 *  map_hash_str both guarantee this and remap a genuine zero result
 *  to 1.
 * ================================================================ */

/* BucketHdr - the per-slot header inside a Map's backing storage */
typedef struct {
        uint64_t hash; /* the key's hash; 0 means "this slot is empty" */
        uint32_t psl;  /* probe sequence length: how far this entry has
                          moved from its ideal slot, for Robin Hood's
                          steal-from-the-shorter-probe rule           */
        uint32_t _pad;
} BucketHdr;

/* MapHdr - the hidden header living immediately before a Map's bucket array */
typedef struct {
        Arena* arena;
        size_t len, cap, key_sz, val_sz, val_align;
        uint64_t (*hash_fn)(const void* key, size_t sz);
        bool (*eq_fn)(const void* a, const void* b, size_t sz);
} MapHdr;

/* MapIter - cursor state for map_next(); zero-initialize before the first call */
typedef struct {
        size_t i;   /* internal bucket cursor; do not read or modify */
        void*  key; /* set by map_next() on each successful call     */
        void*  val; /* set by map_next() on each successful call     */
} MapIter;

/* Map - opaque handle to a hash map; always used as a Map* */
typedef uint8_t Map;

/* MAP_HDR_OFFSET - byte distance from a MapHdr to its bucket array */
#define MAP_HDR_OFFSET AlignUp(sizeof(MapHdr), 8)

/* map_hdr - recover the hidden MapHdr* from a Map* */
#define map_hdr(m) ((MapHdr*)((uint8_t*)(m) - MAP_HDR_OFFSET))

/* map_len - number of entries currently stored in @m */
#define map_len(m) (map_hdr(m)->len)

/* map_cap - number of buckets currently allocated in @m (always a power of two) */
#define map_cap(m) (map_hdr(m)->cap)

/*
 * iter_key / iter_val - read a MapIter's current key or value as type @T
 * @it: a MapIter value just returned true from map_next()
 * @T: the map's key type (for iter_key) or value type (for iter_val)
 *
 * Copies sizeof(T) bytes out of the iterator's internal pointer, so
 * the result is a plain T value, not a pointer into the map.
 */
#define iter_key(it, T) (*((T*)memcpy(&(T){0}, (it).key, sizeof(T))))
#define iter_val(it, T) (*((T*)memcpy(&(T){0}, (it).val, sizeof(T))))

/* map_clear - remove every entry from @m without releasing its bucket array */
#define map_clear(m)                                                         \
        do {                                                                 \
                MapHdr* _bare_mh = map_hdr(m);                               \
                size_t  _bare_bsz = _map_bkt_stride(                         \
                    _bare_mh->key_sz, _bare_mh->val_sz, _bare_mh->val_align);\
                memset((m), 0, _bare_mh->cap * _bare_bsz);                   \
                _bare_mh->len = 0;                                           \
        } while (0)

/*
 * map_hash_bytes - FNV-1a hash over @sz raw bytes at @key
 * @key: pointer to the bytes to hash; may be NULL only if @sz is 0
 * @sz: number of bytes to hash
 *
 * The default hash_fn for map_make(); suitable for any plain-old-data
 * key type compared via memcmp (map_eq_bytes).
 *
 * Return: a 64-bit hash, remapped from 0 to 1 if the raw computation
 * happens to be exactly 0, since 0 is reserved as this map's
 * empty-bucket sentinel.
 */
BAREDEF uint64_t map_hash_bytes(const void* key, size_t sz);

/* map_eq_bytes - byte-for-byte equality via memcmp; the default eq_fn for map_make() */
BAREDEF bool map_eq_bytes(const void* a, const void* b, size_t sz);

/*
 * _map_bkt_stride - bytes occupied by one bucket (header + key + value,
 * padded for alignment)
 * @key_sz: size of the key type
 * @val_sz: size of the value type
 * @val_align: required alignment of the value type
 */
BAREDEF size_t _map_bkt_stride(size_t key_sz, size_t val_sz, size_t val_align);

/*
 * _map_make - allocate a fresh map's bucket array
 * @a: arena to allocate from
 * @key_sz: size of the key type in bytes
 * @val_sz: size of the value type in bytes
 * @val_align: required alignment of the value type
 * @cap: minimum initial capacity in entries; rounded up to a power of two
 * @hash_fn: hash function; must never return 0
 * @eq_fn: equality function for resolving hash collisions
 *
 * Called via the map_make() / map_make_str_key() macros, which supply
 * @key_sz, @val_sz, @val_align, @hash_fn, and @eq_fn automatically
 * from the key/value types you name.
 *
 * Return: a Map* with length 0 and the requested (rounded-up) capacity.
 */
BAREDEF Map* _map_make(Arena* a,
                       size_t key_sz,
                       size_t val_sz,
                       size_t val_align,
                       size_t cap,
                       uint64_t (*hash_fn)(const void*, size_t),
                       bool (*eq_fn)(const void*, const void*, size_t));

/*
 * _map_get - look up a key, the implementation behind map_get()
 * @m: the map to search
 * @key: pointer to the key to look up
 *
 * Return: a pointer to the stored value if @key is present, or NULL.
 * The returned pointer is invalidated by any subsequent map_set()
 * that triggers a rehash; do not retain it across one.
 */
BAREDEF void* _map_get(Map* m, const void* key);

/*
 * _map_set - insert or update a key/value pair, the implementation
 * behind map_set()
 * @mp: address of the Map* variable; updated in place if growth
 *      causes the map to be rehashed into a new, larger allocation
 * @key: pointer to the key
 * @val: pointer to the value
 *
 * Grows (rehashing into double the capacity) whenever the load factor
 * would exceed 75% after the insert.
 */
BAREDEF void _map_set(Map** mp, const void* key, const void* val);

/*
 * _map_del - remove a key, the implementation behind map_del()
 * @m: the map to remove from
 * @key: pointer to the key to remove
 *
 * Return: true if @key was present and has been removed, false if it
 * was not found.
 */
BAREDEF bool _map_del(Map* m, const void* key);

/*
 * map_next - advance a MapIter to the next occupied bucket
 * @m: the map being iterated
 * @it: in/out: the iterator; zero-initialize before the first call
 *
 * Usage:
 *   MapIter it = {0};
 *   while (map_next(m, &it)) {
 *           int k = iter_key(it, int);
 *           int v = iter_val(it, int);
 *   }
 *
 * Insertion or removal during iteration invalidates the iterator;
 * finish iterating before mutating the map, or restart iteration
 * after mutating it.
 *
 * Return: true if @it now points at a valid entry, false once
 * iteration is complete.
 */
BAREDEF bool map_next(Map* m, MapIter* it);

/*
 * _map_keys - collect every key into a freshly allocated Slice
 * @m: the map to collect from
 * @a: arena to allocate the result Slice from
 * @key_sz: size of the key type; must match @m's actual key size
 *
 * Called via the map_keys() macro, which supplies @key_sz from the
 * type you name.
 *
 * Return: a Slice(KT) (cast by map_keys()) containing map_len(m) keys
 * in unspecified order.
 */
BAREDEF void* _map_keys(Map* m, Arena* a, size_t key_sz);

/*
 * _map_values - collect every value into a freshly allocated Slice
 * @m: the map to collect from
 * @a: arena to allocate the result Slice from
 * @val_sz: size of the value type; must match @m's actual value size
 * @val_align: alignment of the value type
 *
 * Called via the map_values() macro. Values are returned in the same
 * relative order as _map_keys()'s keys would be, for the same map
 * state, so zipping the two together by index reconstructs valid
 * pairs.
 *
 * Return: a Slice(VT) (cast by map_values()) containing map_len(m)
 * values.
 */
BAREDEF void* _map_values(Map* m, Arena* a, size_t val_sz, size_t val_align);

/*
 * map_hash_str - hash a Str key by its bytes, ignoring @sz
 * @key: pointer to a Str
 * @sz: unused; present only to match the hash_fn signature
 *
 * The hash_fn supplied automatically by map_make_str_key().
 */
BAREDEF uint64_t map_hash_str(const void* key, size_t sz);

/*
 * map_eq_str - compare two Str keys via str_eq(), ignoring @sz
 *
 * The eq_fn supplied automatically by map_make_str_key().
 */
BAREDEF bool map_eq_str(const void* a, const void* b, size_t sz);

/*
 * map_clone - a full copy of @m, including its bucket array, in @a
 * @m: the map to copy
 * @a: arena to allocate the copy's bucket array from
 *
 * Return: a new Map* with the same entries, capacity, hash_fn, and
 * eq_fn as @m, independent of it from this point on.
 */
BAREDEF Map* map_clone(Map* m, Arena* a);

/*
 * map_make_str_key - declare a new map with Str keys
 * @a: arena to allocate the map's bucket array from
 * @VT: the value type
 * @cap: minimum initial capacity in entries
 *
 * Return: a Map* ready for map_set()/map_get() with `Str` keys.
 */
#define map_make_str_key(a, VT, cap) \
        _map_make((a),               \
                  sizeof(Str),       \
                  sizeof(VT),        \
                  AlignOfType(VT),   \
                  (size_t)(cap),     \
                  map_hash_str,      \
                  map_eq_str)

/*
 * map_make - declare a new map with plain-old-data keys
 * @a: arena to allocate the map's bucket array from
 * @KT: the key type, compared and hashed by raw bytes
 * @VT: the value type
 * @cap: minimum initial capacity in entries
 *
 * Return: a Map* ready for map_set()/map_get() with `KT` keys.
 */
#define map_make(a, KT, VT, cap)   \
        _map_make((a),             \
                  sizeof(KT),      \
                  sizeof(VT),      \
                  AlignOfType(VT), \
                  (size_t)(cap),   \
                  map_hash_bytes,  \
                  map_eq_bytes)

/* map_get - look up @keyptr in @m; returns a pointer to the value, or NULL */
#define map_get(m, keyptr) _map_get((m), (keyptr))

/* map_set - insert or update @keyptr -> @valptr in @m, growing @m in place if needed */
#define map_set(m, keyptr, valptr) _map_set(&(m), (keyptr), (valptr))

/* map_del - remove @keyptr from @m; returns true if it was present */
#define map_del(m, keyptr) _map_del((m), (keyptr))

/* map_keys - collect every key of @m into a freshly allocated Slice(KT) in @a */
#define map_keys(m, a, KT) ((KT*)_map_keys((m), (a), sizeof(KT)))

/* map_values - collect every value of @m into a freshly allocated Slice(VT) in @a */
#define map_values(m, a, VT) \
        ((VT*)_map_values((m), (a), sizeof(VT), AlignOfType(VT)))

#endif /* BARESTD_H */

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BARESTD_IMPLEMENTATION

/* ---- AllocStats ------------------------------------------------ */

void alloc_stats_record_alloc(AllocStats* stats, size_t size) {
        if (!stats) return; /* NULL stats means "don't track"; not an error */

        stats->bytes_current += size;
        stats->bytes_total += size;
        stats->alloc_count++;
        if (stats->bytes_current > stats->bytes_peak) {
                stats->bytes_peak = stats->bytes_current;
        }

        Assert(stats->bytes_peak >= stats->bytes_current,
               "alloc_stats_record_alloc: peak must track current");
}

void alloc_stats_record_free(AllocStats* stats, size_t size) {
        if (!stats) return;

        Assert(size <= stats->bytes_current,
               "alloc_stats_record_free: freed size exceeds bytes_current");
        stats->bytes_current -= size;
        stats->free_count++;
}

void alloc_stats_record_resize(AllocStats* stats,
                               size_t      old_size,
                               size_t      new_size) {
        if (!stats) return;

        Assert(old_size <= stats->bytes_current,
               "alloc_stats_record_resize: old_size exceeds bytes_current");
        stats->bytes_current = stats->bytes_current - old_size + new_size;
        stats->bytes_total += (new_size > old_size) ? (new_size - old_size) : 0;
        stats->resize_count++;
        if (stats->bytes_current > stats->bytes_peak) {
                stats->bytes_peak = stats->bytes_current;
        }
}

/* ---- Allocator --------------------------------------------------- */

Allocator allocator_make(AllocatorProc proc, void* context) {
        Assert(proc != NULL, "allocator_make: proc must not be NULL");
        Allocator a;
        a.proc    = proc;
        a.context = context;
        return a;
}

/*
 * The libc-backed allocator needs exactly one piece of state, an
 * optional AllocStats*, which is stored directly as the context
 * pointer -- no wrapper struct, so allocator_default() never
 * allocates anything of its own to track or leak.
 */
static void* _default_allocator_proc(void*         raw_ctx,
                                     AllocatorMode mode,
                                     size_t        size,
                                     size_t        old_size,
                                     void*         old_ptr,
                                     size_t        align) {
        AllocStats* stats = (AllocStats*)raw_ctx;
        Unused(align); /* malloc/realloc already satisfy max_align_t */

        switch (mode) {
                case ALLOCATOR_MODE_ALLOC: {
                        if (size == 0) return NULL;
                        void* p = malloc(size);
                        if (p) alloc_stats_record_alloc(stats, size);
                        return p;
                }
                case ALLOCATOR_MODE_FREE: {
                        Assert(old_ptr != NULL || old_size == 0,
                               "_default_allocator_proc: FREE with non-NULL "
                               "size but NULL pointer");
                        free(old_ptr);
                        alloc_stats_record_free(stats, old_size);
                        return NULL;
                }
                case ALLOCATOR_MODE_RESIZE: {
                        void* p = realloc(old_ptr, size);
                        /* realloc(non-NULL, >0) failing leaves the
                           original block valid per the C standard, so
                           a failed resize must not record one. */
                        if (p) alloc_stats_record_resize(stats, old_size, size);
                        return p;
                }
                case ALLOCATOR_MODE_FREE_ALL: {
                        Assert(false,
                               "_default_allocator_proc: FREE_ALL is not "
                               "supported by the libc-backed allocator");
                        return NULL;
                }
                case ALLOCATOR_MODE_QUERY_STATS:
                        return (void*)stats;
        }

        Assert(false, "_default_allocator_proc: unreachable mode");
        return NULL;
}

Allocator allocator_default(AllocStats* stats) {
        return allocator_make(_default_allocator_proc, (void*)stats);
}

void* allocator_alloc(Allocator a, size_t size, size_t align) {
        Assert(a.proc != NULL, "allocator_alloc: allocator.proc must not be NULL");
        if (size == 0) return NULL;
        return a.proc(a.context, ALLOCATOR_MODE_ALLOC, size, 0, NULL, align);
}

void* allocator_resize(
    Allocator a, void* old_ptr, size_t old_size, size_t new_size, size_t align) {
        Assert(a.proc != NULL, "allocator_resize: allocator.proc must not be NULL");
        AssertImplies(old_ptr == NULL,
                      old_size == 0,
                      "allocator_resize: NULL old_ptr implies old_size == 0");
        return a.proc(
            a.context, ALLOCATOR_MODE_RESIZE, new_size, old_size, old_ptr, align);
}

void allocator_free(Allocator a, void* ptr, size_t size) {
        Assert(a.proc != NULL, "allocator_free: allocator.proc must not be NULL");
        AssertImplies(ptr == NULL,
                      size == 0,
                      "allocator_free: NULL ptr implies size == 0");
        a.proc(a.context, ALLOCATOR_MODE_FREE, 0, size, ptr, 0);
}

void allocator_free_all(Allocator a) {
        Assert(a.proc != NULL, "allocator_free_all: allocator.proc must not be NULL");
        a.proc(a.context, ALLOCATOR_MODE_FREE_ALL, 0, 0, NULL, 0);
}

AllocStats* allocator_query_stats(Allocator a) {
        Assert(a.proc != NULL, "allocator_query_stats: allocator.proc must not be NULL");
        return (AllocStats*)a.proc(a.context, ALLOCATOR_MODE_QUERY_STATS, 0, 0, NULL, 0);
}

static BARE_THREAD_LOCAL Allocator _allocator_stack[BARE_MAX_ALLOCATOR_STACK];
static BARE_THREAD_LOCAL int       _allocator_stack_top = -1;

void allocator_context_push(Allocator a) {
        Assert(a.proc != NULL, "allocator_context_push: allocator.proc must not be NULL");
        Assert(_allocator_stack_top + 1 < BARE_MAX_ALLOCATOR_STACK,
               "allocator_context_push: context stack overflow");
        _allocator_stack_top++;
        _allocator_stack[_allocator_stack_top] = a;
}

void allocator_context_pop(void) {
        Assert(_allocator_stack_top >= 0,
               "allocator_context_pop: context stack underflow "
               "(unbalanced push/pop)");
        _allocator_stack_top--;
}

Allocator allocator_context_current(void) {
        if (_allocator_stack_top < 0) return allocator_default(NULL);
        return _allocator_stack[_allocator_stack_top];
}

/* ---- Arena -------------------------------------------------------- */

Arena arena_from_buf(void* buf, size_t cap) {
        Assert(buf != NULL, "arena_from_buf: buf must not be NULL");
        Assert(cap > 0, "arena_from_buf: cap must be positive");

        Arena a;
        memset(&a, 0, sizeof a);
        a.buf = (uint8_t*)buf;
        a.cap = cap;
        return a;
}

Arena* arena_new(size_t cap) {
        Assert(cap > 0, "arena_new: cap must be positive");

        Arena* a = (Arena*)malloc(sizeof(Arena) + cap);
        Assert(a != NULL, "arena_new: out of memory");
        memset(a, 0, sizeof(Arena));
        a->buf   = (uint8_t*)(a + 1);
        a->cap   = cap;
        a->owned = 1;
        return a;
}

void* arena_push(Arena* a, size_t size, size_t align) {
        Assert(a != NULL, "arena_push: a must not be NULL");
        Assert(IsPow2(align), "arena_push: align must be a power of two");

        /*
         * Iterative, not recursive: a loop whose trip count is bounded
         * (BARE_MAX_ARENA_CHAIN) and checked is preferable to a call
         * stack whose depth is only assumed to be bounded. Each
         * iteration either satisfies the request in the current chunk
         * and returns, or creates exactly one new chunk and continues.
         */
        Arena* chunk       = a;
        size_t chain_depth = 0;
        for (;;) {
                AssertBounded(chain_depth, BARE_MAX_ARENA_CHAIN,
                              "arena_push: overflow chain exceeds the configured maximum");
                Assert(chunk->pos <= chunk->cap, "arena_push: pos must never exceed cap");

                size_t pos = AlignUp(chunk->pos, align);
                size_t end = pos + size;

                if (end <= chunk->cap) {
                        chunk->pos = end;
                        void* p    = chunk->buf + pos;
                        alloc_stats_record_alloc(chunk->stats, size);
                        return p;
                }

                if (!chunk->next) {
                        size_t new_cap = Max(chunk->cap * 2, AlignUp(size, align) + align);
                        chunk->next    = arena_new(new_cap);
                }
                chunk = chunk->next;
                chain_depth++;
        }
}

void arena_pop_to(Arena* a, size_t pos) {
        Assert(a != NULL, "arena_pop_to: a must not be NULL");
        Assert(pos <= a->pos, "arena_pop_to: saved_pos must not be ahead of current pos");

        alloc_stats_record_free(a->stats, a->pos - pos);
        a->pos = pos;
}

void arena_reset(Arena* a) {
        Assert(a != NULL, "arena_reset: a must not be NULL");

        size_t chain_depth = 0;
        Arena* cursor       = a;
        while (cursor) {
                AssertBounded(chain_depth, BARE_MAX_ARENA_CHAIN,
                              "arena_reset: overflow chain exceeds the configured maximum");
                alloc_stats_record_free(cursor->stats, cursor->pos);
                cursor->pos = 0;
                cursor       = cursor->next;
                chain_depth++;
        }
}

void arena_free(Arena* a) {
        if (!a) return; /* freeing NULL is a valid no-op, as for free() itself */

        size_t chain_depth = 0;
        Arena* cursor       = a->next;
        while (cursor) {
                AssertBounded(chain_depth, BARE_MAX_ARENA_CHAIN,
                              "arena_free: overflow chain exceeds the configured maximum");
                Arena* next = cursor->next;
                if (cursor->owned) free(cursor);
                cursor = next;
                chain_depth++;
        }
        if (a->owned) free(a);
}

Scratch scratch_begin(Arena* a) {
        Assert(a != NULL, "scratch_begin: a must not be NULL");
        Scratch s;
        s.a   = a;
        s.pos = a->pos;
        return s;
}

void scratch_end(Scratch s) {
        Assert(s.a != NULL, "scratch_end: scratch.a must not be NULL");
        arena_pop_to(s.a, s.pos);
}

void arena_set_stats(Arena* a, AllocStats* stats) {
        Assert(a != NULL, "arena_set_stats: a must not be NULL");
        a->stats = stats;
}

/*
 * allocator_arena's proc. The only allocator implementation in this
 * file that supports ALLOCATOR_MODE_FREE_ALL, since "free everything
 * at once" is exactly what arena_reset already means.
 */
static void* _arena_allocator_proc(void*         raw_ctx,
                                   AllocatorMode mode,
                                   size_t        size,
                                   size_t        old_size,
                                   void*         old_ptr,
                                   size_t        align) {
        Arena* a = (Arena*)raw_ctx;
        Assert(a != NULL, "_arena_allocator_proc: context (Arena*) must not be NULL");

        size_t real_align = align ? align : AlignOfType(void*);

        switch (mode) {
                case ALLOCATOR_MODE_ALLOC:
                        return size ? arena_push(a, size, real_align) : NULL;
                case ALLOCATOR_MODE_FREE:
                        /* Deliberate no-op: an arena has no notion of one
                           block's individual lifetime. */
                        Unused(old_ptr);
                        Unused(old_size);
                        return NULL;
                case ALLOCATOR_MODE_RESIZE: {
                        void* new_ptr = arena_push(a, size, real_align);
                        if (old_ptr != NULL && old_size > 0) {
                                memcpy(new_ptr, old_ptr, Min(old_size, size));
                        }
                        return new_ptr;
                }
                case ALLOCATOR_MODE_FREE_ALL:
                        arena_reset(a);
                        return NULL;
                case ALLOCATOR_MODE_QUERY_STATS:
                        return (void*)a->stats;
        }

        Assert(false, "_arena_allocator_proc: unreachable mode");
        return NULL;
}

Allocator allocator_arena(Arena* a) {
        Assert(a != NULL, "allocator_arena: a must not be NULL");
        return allocator_make(_arena_allocator_proc, a);
}

/* ---- DynArray ------------------------------------------------------
 *
 * DynArray has no implementation of its own: every operation
 * (da_reserve, da_append, da_append_many, da_free, da_at, da_last,
 * da_clear, da_is_empty) is a macro defined in the declarations
 * section above, expanding directly to plain struct member access so
 * the compiler can infer the element type without this file ever
 * naming it. _bare_bounds_check, the one helper they share, is a
 * `static inline` function defined alongside them for the same
 * reason Slice's bounds-checking macros use it too: it is small
 * enough that `static inline` already gives every translation unit
 * its own correctly-inlined copy with no separate implementation
 * needed here.
 */



void* _slice_make(Arena* a, size_t stride, size_t elem_align, size_t cap) {
        Assert(a != NULL, "_slice_make: a must not be NULL");
        Assert(stride > 0, "_slice_make: stride must be positive");

        if (elem_align < 1) elem_align = 1;
        if (elem_align > 16) elem_align = 16;
        Assert(elem_align >= 1, "_slice_make: clamped elem_align must be >= 1");
        Assert(elem_align <= 16, "_slice_make: clamped elem_align must be <= 16");

        size_t total = SLICE_HDR_OFFSET + stride * cap;
        /* Negative space: stride * cap overflowing size_t would make
           `total` smaller than the real requirement, handing the
           caller a buffer that looks valid but is not big enough. */
        Assert(cap == 0 || (total - SLICE_HDR_OFFSET) / cap == stride,
               "_slice_make: stride*cap must not overflow size_t");

        uint8_t* raw = (uint8_t*)arena_push(a, total, 16);
        Assert(raw != NULL, "_slice_make: arena_push must succeed");
        memset(raw, 0, total);

        SliceHdr* hdr   = (SliceHdr*)raw;
        hdr->arena      = a;
        hdr->len        = 0;
        hdr->cap        = cap;
        hdr->stride     = stride;
        hdr->elem_align = elem_align;

        void* user_ptr = raw + SLICE_HDR_OFFSET;
        Assert(slice_hdr(user_ptr) == hdr,
               "_slice_make: shadow header must be recoverable from the user pointer");
        Assert(slice_len(user_ptr) == 0, "_slice_make: a fresh slice has length 0");
        return user_ptr;
}

/*
 * _slice_grow is the rare-path helper for _slice_push_raw: when the
 * current backing storage is full, it allocates a new, larger backing
 * store and copies the existing elements across. Extracted so the
 * common (room-available) path in _slice_push_raw stays short.
 */
static void* _slice_grow(void* s, SliceHdr* hdr) {
        Assert(s != NULL, "_slice_grow: s must not be NULL");
        Assert(hdr != NULL, "_slice_grow: hdr must not be NULL");
        Assert(hdr->len >= hdr->cap, "_slice_grow: must only be called when full");

        size_t new_cap = hdr->cap ? hdr->cap * 2 : 4;
        void*  new_s =
            _slice_make(hdr->arena, hdr->stride, hdr->elem_align, new_cap);

        memcpy(new_s, s, hdr->len * hdr->stride);
        slice_hdr(new_s)->len = hdr->len;

        Assert(slice_len(new_s) == hdr->len,
               "_slice_grow: grown slice must preserve length");
        Assert(slice_cap(new_s) == new_cap,
               "_slice_grow: grown slice must have the new capacity");
        return new_s;
}

void* _slice_push_raw(void* s, const void* elem) {
        Assert(s != NULL, "_slice_push_raw: s must not be NULL");
        Assert(elem != NULL, "_slice_push_raw: elem must not be NULL");

        SliceHdr* hdr = slice_hdr(s);
        Assert(hdr->len <= hdr->cap, "_slice_push_raw: len must not exceed cap");

        if (hdr->len >= hdr->cap) {
                s   = _slice_grow(s, hdr);
                hdr = slice_hdr(s);
        }

        memcpy((uint8_t*)s + hdr->len * hdr->stride, elem, hdr->stride);
        hdr->len++;

        Assert(hdr->len <= hdr->cap,
               "_slice_push_raw: len must not exceed cap after push");
        return s;
}

bool _slice_contains_raw(const void* s,
                         const void* val,
                         size_t      len,
                         size_t      stride) {
        Assert(stride > 0, "_slice_contains_raw: stride must be positive");
        AssertImplies(len > 0, s != NULL,
                      "_slice_contains_raw: a non-empty slice must have a "
                      "non-NULL backing pointer");
        Assert(val != NULL, "_slice_contains_raw: val must not be NULL");

        const uint8_t* p = (const uint8_t*)s;
        size_t         i;
        for (i = 0; i < len; i++, p += stride) {
                AssertBounded(i, len, "_slice_contains_raw: loop must stay within len");
                if (memcmp(p, val, stride) == 0) return true;
        }
        return false;
}

/* ---------------------------------------------------------------
 *  Str
 * --------------------------------------------------------------- */

bool str_eq(Str a, Str b) {
        /* Positive space: empty strings (len == 0) are valid input
           with ptr possibly NULL (str_null()); equality must still be
           well-defined for them without dereferencing ptr. */
        if (a.len != b.len) return false;
        if (a.len == 0) return true;

        Assert(a.ptr != NULL, "str_eq: non-empty a must have a non-NULL ptr");
        Assert(b.ptr != NULL, "str_eq: non-empty b must have a non-NULL ptr");
        return a.ptr == b.ptr || memcmp(a.ptr, b.ptr, a.len) == 0;
}

bool str_has_prefix(Str s, Str pfx) {
        if (pfx.len == 0) return true;
        if (s.len < pfx.len) return false;

        Assert(s.ptr != NULL, "str_has_prefix: non-empty s must have a non-NULL ptr");
        Assert(pfx.ptr != NULL, "str_has_prefix: non-empty pfx must have a non-NULL ptr");
        return memcmp(s.ptr, pfx.ptr, pfx.len) == 0;
}

bool str_has_suffix(Str s, Str sfx) {
        if (sfx.len == 0) return true;
        if (s.len < sfx.len) return false;

        Assert(s.ptr != NULL, "str_has_suffix: non-empty s must have a non-NULL ptr");
        Assert(sfx.ptr != NULL, "str_has_suffix: non-empty sfx must have a non-NULL ptr");
        return memcmp(s.ptr + (s.len - sfx.len), sfx.ptr, sfx.len) == 0;
}

ptrdiff_t str_find(Str hay, Str needle) {
        if (needle.len == 0) return 0;
        if (needle.len > hay.len) return -1;

        Assert(hay.ptr != NULL, "str_find: non-empty hay must have a non-NULL ptr");
        Assert(needle.ptr != NULL, "str_find: non-empty needle must have a non-NULL ptr");

        size_t last = hay.len - needle.len;
        size_t i;
        for (i = 0; i <= last; i++) {
                AssertBounded(i, hay.len, "str_find: loop must stay within hay.len");
                if (memcmp(hay.ptr + i, needle.ptr, needle.len) == 0) {
                        Assert((ptrdiff_t)i >= 0,
                               "str_find: a valid match index must fit in ptrdiff_t");
                        return (ptrdiff_t)i;
                }
        }
        return -1;
}

Str str_slice(Str s, size_t lo, size_t hi) {
        Assert(lo <= hi, "str_slice: lo must not exceed hi");
        Assert(hi <= s.len, "str_slice: hi must not exceed s.len");

        Str result = str_buf(s.ptr + lo, hi - lo);

        Assert(result.len == hi - lo, "str_slice: result length must equal hi - lo");
        return result;
}

static bool _char_in_set(char c, Str set) {
        Assert(set.len == 0 || set.ptr != NULL,
               "_char_in_set: a non-empty set must have a non-NULL ptr");

        size_t i;
        for (i = 0; i < set.len; ++i) {
                AssertBounded(i, set.len, "_char_in_set: loop must stay within set.len");
                if (set.ptr[i] == c) return true;
        }
        return false;
}

Str str_trim_left(Str s, Str cutset) {
        while (s.len > 0 && _char_in_set(s.ptr[0], cutset)) {
                s.ptr++;
                s.len--;
        }
        Assert(s.len == 0 || !_char_in_set(s.ptr[0], cutset),
               "str_trim_left: result must not start with a cutset character");
        return s;
}

Str str_trim_right(Str s, Str cutset) {
        while (s.len > 0 && _char_in_set(s.ptr[s.len - 1], cutset)) s.len--;
        Assert(s.len == 0 || !_char_in_set(s.ptr[s.len - 1], cutset),
               "str_trim_right: result must not end with a cutset character");
        return s;
}

Str str_trim(Str s, Str cutset) {
        return str_trim_left(str_trim_right(s, cutset), cutset);
}

Str str_next_token(Str* s, Str sep) {
        Assert(s != NULL, "str_next_token: s must not be NULL");
        Assert(!str_is_empty(sep), "str_next_token: separator must not be empty");

        if (str_is_empty(*s)) return str_null();

        ptrdiff_t pos = str_find(*s, sep);
        if (pos < 0) {
                Str tok = *s;
                s->ptr += s->len;
                s->len = 0;
                Assert(str_is_empty(*s), "str_next_token: s must be empty once exhausted");
                return tok;
        }

        Str tok = str_slice(*s, 0, (size_t)pos);
        s->ptr += (size_t)pos + sep.len;
        s->len -= (size_t)pos + sep.len;

        Assert(tok.len == (size_t)pos, "str_next_token: token length must match the split point");
        return tok;
}

char* str_to_cstr(Arena* a, Str s) {
        Assert(a != NULL, "str_to_cstr: a must not be NULL");

        char* buf = arena_push_array(a, char, s.len + 1);
        Assert(buf != NULL, "str_to_cstr: arena_push_array must succeed");

        if (s.len > 0) {
                Assert(s.ptr != NULL, "str_to_cstr: non-empty s must have a non-NULL ptr");
                memcpy(buf, s.ptr, s.len);
        }
        buf[s.len] = '\0';

        Assert(buf[s.len] == '\0', "str_to_cstr: result must be NUL-terminated");
        return buf;
}

Str str_clone(Arena* a, Str s) {
        Assert(a != NULL, "str_clone: a must not be NULL");

        char* buf = arena_push_array(a, char, s.len ? s.len : 1);
        Assert(buf != NULL, "str_clone: arena_push_array must succeed");

        if (s.len > 0) {
                Assert(s.ptr != NULL, "str_clone: non-empty s must have a non-NULL ptr");
                memcpy(buf, s.ptr, s.len);
        }

        Str result = str_buf(buf, s.len);
        Assert(result.len == s.len, "str_clone: cloned length must match the source");
        return result;
}

Str str_fmt(Arena* a, const char* fmt, ...) {
        Assert(a != NULL, "str_fmt: a must not be NULL");
        Assert(fmt != NULL, "str_fmt: fmt must not be NULL");

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
        Assert(buf != NULL, "str_fmt: arena_push_array must succeed");
        vsnprintf(buf, (size_t)n + 1, fmt, ap2);
        va_end(ap2);

        Assert(buf[n] == '\0', "str_fmt: result must be NUL-terminated");
        return str_buf(buf, (size_t)n);
}

static size_t _str_ws_len(Str s) {
        size_t n = 0;
        while (n < s.len && (unsigned char)s.ptr[n] <= ' ') n++;
        Assert(n <= s.len, "_str_ws_len: result must not exceed s.len");
        return n;
}

Str str_consume_while(Str* s, int (*pred)(int)) {
        Assert(s != NULL, "str_consume_while: s must not be NULL");
        Assert(pred != NULL, "str_consume_while: pred must not be NULL");

        const char* start = s->ptr;
        while (s->len > 0 && pred((unsigned char)s->ptr[0])) {
                s->ptr++;
                s->len--;
        }

        Str result = str_buf(start, (size_t)(s->ptr - start));
        Assert(s->len == 0 || !pred((unsigned char)s->ptr[0]),
               "str_consume_while: remainder must not start with a matching character");
        return result;
}

Str str_consume_until(Str* s, int (*pred)(int)) {
        Assert(s != NULL, "str_consume_until: s must not be NULL");
        Assert(pred != NULL, "str_consume_until: pred must not be NULL");

        const char* start = s->ptr;
        while (s->len > 0 && !pred((unsigned char)s->ptr[0])) {
                s->ptr++;
                s->len--;
        }

        Str result = str_buf(start, (size_t)(s->ptr - start));
        Assert(s->len == 0 || pred((unsigned char)s->ptr[0]),
               "str_consume_until: remainder must start with a matching character "
               "(or be empty)");
        return result;
}

/*
 * _str_digit_value returns the numeric value of a single digit
 * character in the given base, or -1 if the character is not a valid
 * digit in that base. Extracted so str_consume_i64/u64 read as "scan
 * digits, accumulate" without inlining the base-10-vs-16 branching
 * three times over.
 */
static int _str_digit_value(char c, int base) {
        Assert(base == 10 || base == 16, "_str_digit_value: base must be 10 or 16");

        if (c >= '0' && c <= '9') return c - '0';
        if (base == 16 && c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (base == 16 && c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
}

bool str_consume_i64(Str* s, int64_t* out) {
        Assert(s != NULL, "str_consume_i64: s must not be NULL");
        Assert(out != NULL, "str_consume_i64: out must not be NULL");

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
                AssertBounded(i, r.len, "str_consume_i64: loop must stay within r.len");
                int d = _str_digit_value(r.ptr[i], base);
                if (d < 0) break;
                val = val * base + d;
                i++;
        }
        if (i == start) return false;

        *out           = neg ? -val : val;
        size_t advance = ws + i;
        s->ptr += advance;
        s->len -= advance;

        Assert(s->ptr != NULL || s->len == 0,
               "str_consume_i64: a non-NULL-checked advance must keep s consistent");
        return true;
}

bool str_consume_u64(Str* s, uint64_t* out) {
        Assert(s != NULL, "str_consume_u64: s must not be NULL");
        Assert(out != NULL, "str_consume_u64: out must not be NULL");

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
                AssertBounded(i, r.len, "str_consume_u64: loop must stay within r.len");
                int d = _str_digit_value(r.ptr[i], base);
                if (d < 0) break;
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
        Assert(s != NULL, "str_consume_f64: s must not be NULL");
        Assert(out != NULL, "str_consume_f64: out must not be NULL");

        size_t ws = _str_ws_len(*s);
        Str    r  = str_buf(s->ptr + ws, s->len - ws);
        if (!r.len) return false;

        size_t n = 0;
        while (n < r.len) {
                AssertBounded(n, r.len, "str_consume_f64: scan loop must stay within r.len");
                char c = r.ptr[n];
                bool is_float_char =
                    (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' ||
                    c == 'e' || c == 'E' || c == 'n' || c == 'N' || c == 'a' ||
                    c == 'A' || c == 'f' || c == 'F' || c == 'i' || c == 'I';
                if (!is_float_char) break;
                n++;
        }
        if (n == 0) return false;

        char  tmp[64];
        char* buf;
        char* heap = NULL;
        if (n < sizeof(tmp)) {
                memcpy(tmp, r.ptr, n);
                tmp[n] = '\0';
                buf    = tmp;
        } else {
                heap = (char*)malloc(n + 1);
                Assert(heap != NULL,
                       "str_consume_f64: out-of-memory on the rare long-literal path");
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
        Assert(s != NULL, "str_consume_bool: s must not be NULL");
        Assert(out != NULL, "str_consume_bool: out must not be NULL");

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
        Assert(out != NULL, "str_to_i64: out must not be NULL");
        return str_consume_i64(&s, out);
}
bool str_to_u64(Str s, uint64_t* out) {
        Assert(out != NULL, "str_to_u64: out must not be NULL");
        return str_consume_u64(&s, out);
}
bool str_to_f64(Str s, double* out) {
        Assert(out != NULL, "str_to_f64: out must not be NULL");
        return str_consume_f64(&s, out);
}
bool str_to_bool(Str s, bool* out) {
        Assert(out != NULL, "str_to_bool: out must not be NULL");
        return str_consume_bool(&s, out);
}

/* ---------------------------------------------------------------
 *  Map
 * --------------------------------------------------------------- */

uint64_t map_hash_bytes(const void* key, size_t sz) {
        /* Positive space: a zero-length key (e.g. an empty Str whose
           ptr is NULL, per str_null()) is valid input and must hash
           deterministically without dereferencing key at all. Only a
           non-empty key is required to have a real pointer. */
        Assert(sz == 0 || key != NULL,
               "map_hash_bytes: a non-empty key must not be NULL");

        const uint8_t* p = (const uint8_t*)key;
        uint64_t       h = UINT64_C(14695981039346656037);
        size_t         i;
        for (i = 0; i < sz; ++i) {
                AssertBounded(i, sz, "map_hash_bytes: loop must stay within sz");
                h = (h ^ (uint64_t)p[i]) * UINT64_C(1099511628211);
        }

        /* Negative space: 0 is reserved as the "empty bucket" sentinel
           in BucketHdr.hash, so a real hash must never collide with it. */
        Assert(h != 0 || sz > 0,
               "map_hash_bytes: result must not be the empty-bucket sentinel "
               "(forced below if it is)");
        return h ? h : UINT64_C(1);
}

bool map_eq_bytes(const void* a, const void* b, size_t sz) {
        Assert(a != NULL, "map_eq_bytes: a must not be NULL");
        Assert(b != NULL, "map_eq_bytes: b must not be NULL");
        return memcmp(a, b, sz) == 0;
}

size_t _map_bkt_stride(size_t key_sz, size_t val_sz, size_t val_align) {
        Assert(key_sz > 0, "_map_bkt_stride: key_sz must be positive");
        Assert(val_align >= 1, "_map_bkt_stride: val_align must be at least 1");

        size_t val_off = AlignUp(sizeof(BucketHdr) + key_sz, val_align);
        size_t stride  = AlignUp(val_off + val_sz, 8);

        Assert(stride >= sizeof(BucketHdr),
               "_map_bkt_stride: stride must be large enough for the bucket header");
        return stride;
}

static uint8_t* _map_bkt(Map* m, size_t i) {
        Assert(m != NULL, "_map_bkt: m must not be NULL");
        MapHdr* h = map_hdr(m);
        Assert(i < h->cap, "_map_bkt: i must be within cap");

        return (uint8_t*)m +
               i * _map_bkt_stride(h->key_sz, h->val_sz, h->val_align);
}

static void* _bkt_key(uint8_t* b) {
        Assert(b != NULL, "_bkt_key: b must not be NULL");
        return b + sizeof(BucketHdr);
}

static void* _bkt_val(uint8_t* b, size_t key_sz, size_t val_align) {
        Assert(b != NULL, "_bkt_val: b must not be NULL");
        Assert(val_align >= 1, "_bkt_val: val_align must be at least 1");
        return b + AlignUp(sizeof(BucketHdr) + key_sz, val_align);
}

static size_t _next_pow2(size_t n) {
        size_t p = 1;
        if (IsPow2(n)) return n;
        while (p < n) {
                AssertBounded(p,
                              (size_t)1 << 63,
                              "_next_pow2: must not run past the largest "
                              "representable power of two");
                p <<= 1;
        }
        Assert(IsPow2(p), "_next_pow2: result must be a power of two");
        Assert(p >= n, "_next_pow2: result must be at least n");
        return p;
}

Map* _map_make(Arena* a,
              size_t key_sz,
              size_t val_sz,
              size_t val_align,
              size_t cap,
              uint64_t (*hash_fn)(const void*, size_t),
              bool (*eq_fn)(const void*, const void*, size_t)) {
        Assert(a != NULL, "_map_make: a must not be NULL");
        Assert(key_sz > 0, "_map_make: key_sz must be positive");
        Assert(hash_fn != NULL, "_map_make: hash_fn must not be NULL");
        Assert(eq_fn != NULL, "_map_make: eq_fn must not be NULL");

        if (val_align < 1) val_align = 1;
        cap              = _next_pow2(Max(cap, 8));
        size_t   bstride = _map_bkt_stride(key_sz, val_sz, val_align);
        size_t   total   = MAP_HDR_OFFSET + cap * bstride;
        Assert(cap == 0 || (total - MAP_HDR_OFFSET) / cap == bstride,
               "_map_make: cap*bstride must not overflow size_t");

        uint8_t* raw = (uint8_t*)arena_push(a, total, 8);
        Assert(raw != NULL, "_map_make: arena_push must succeed");
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

        Map* m = (Map*)(raw + MAP_HDR_OFFSET);
        Assert(map_hdr(m) == hdr, "_map_make: shadow header must be recoverable");
        Assert(map_len(m) == 0, "_map_make: a fresh map has length 0");
        Assert(IsPow2(map_cap(m)), "_map_make: cap must be a power of two");
        return m;
}

void* _map_get(Map* m, const void* key) {
        Assert(m != NULL, "_map_get: m must not be NULL");
        Assert(key != NULL, "_map_get: key must not be NULL");

        MapHdr*  hdr = map_hdr(m);
        uint64_t h   = hdr->hash_fn(key, hdr->key_sz);
        Assert(h != 0, "_map_get: a real hash must never equal the empty sentinel");
        size_t   idx = (size_t)(h & (hdr->cap - 1));

        uint32_t psl;
        for (psl = 0;; ++psl) {
                AssertBounded(psl, hdr->cap, "_map_get: probe sequence must not exceed cap");
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash == 0) return NULL;
                if (bh->psl < psl) return NULL;
                if (bh->hash == h && hdr->eq_fn(_bkt_key(b), key, hdr->key_sz)) {
                        return _bkt_val(b, hdr->key_sz, hdr->val_align);
                }
                idx = (idx + 1) & (hdr->cap - 1);
        }
}

/*
 * _map_bucket_init fills a freshly-zeroed scratch bucket (`tmp`, of
 * exactly `bstride` bytes) with the BucketHdr and copied key/value for
 * a new entry. Extracted out of _map_insert so the Robin Hood
 * swap-walk below it reads as pure control flow with no setup mixed
 * in (push ifs up, push fors down).
 */
static void _map_bucket_init(uint8_t*    tmp,
                             MapHdr*     hdr,
                             uint64_t    hash,
                             const void* key,
                             const void* val) {
        Assert(tmp != NULL, "_map_bucket_init: tmp must not be NULL");
        Assert(hdr != NULL, "_map_bucket_init: hdr must not be NULL");
        Assert(hash != 0, "_map_bucket_init: hash must not be the empty sentinel");

        BucketHdr* tmph = (BucketHdr*)tmp;
        tmph->hash      = hash;
        tmph->psl       = 0;
        memcpy(_bkt_key(tmp), key, hdr->key_sz);
        memcpy(_bkt_val(tmp, hdr->key_sz, hdr->val_align), val, hdr->val_sz);

        Assert(tmph->hash == hash, "_map_bucket_init: hash must round-trip");
        Assert(tmph->psl == 0, "_map_bucket_init: a freshly placed entry starts at psl 0");
}

static void _map_insert(Map*        m,
                        uint64_t    hash,
                        const void* key,
                        const void* val) {
        Assert(m != NULL, "_map_insert: m must not be NULL");
        Assert(key != NULL, "_map_insert: key must not be NULL");
        Assert(val != NULL, "_map_insert: val must not be NULL");
        Assert(hash != 0, "_map_insert: hash must not be the empty sentinel");

        MapHdr* hdr     = map_hdr(m);
        size_t  bstride = _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);
        size_t  idx     = (size_t)(hash & (hdr->cap - 1));

        uint8_t* tmp = (uint8_t*)malloc(bstride * 2);
        Assert(tmp != NULL, "_map_insert: out of memory for scratch bucket");
        uint8_t* swap = tmp + bstride;
        memset(tmp, 0, bstride);
        _map_bucket_init(tmp, hdr, hash, key, val);
        BucketHdr* tmph = (BucketHdr*)tmp;

        uint32_t probe;
        for (probe = 0;; ++probe) {
                AssertBounded(probe, hdr->cap, "_map_insert: probe sequence must not exceed cap");
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;

                if (bh->hash == 0) {
                        memcpy(b, tmp, bstride);
                        hdr->len++;
                        free(tmp);
                        return;
                }
                if (bh->hash == tmph->hash &&
                    hdr->eq_fn(_bkt_key(b), _bkt_key(tmp), hdr->key_sz)) {
                        memcpy(_bkt_val(b, hdr->key_sz, hdr->val_align),
                               _bkt_val(tmp, hdr->key_sz, hdr->val_align),
                               hdr->val_sz);
                        free(tmp);
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

/*
 * _map_grow_if_needed is the rare-path helper for _map_set: when the
 * load factor crosses 75%, it builds a new, larger backing table and
 * re-inserts every live bucket into it, then hands the new table back
 * through *mp. Extracted so _map_set's common path (insert into an
 * already-sized table) stays short.
 */
static void _map_grow_if_needed(Map** mp) {
        Assert(mp != NULL, "_map_grow_if_needed: mp must not be NULL");
        Assert(*mp != NULL, "_map_grow_if_needed: *mp must not be NULL");

        MapHdr* hdr = map_hdr(*mp);
        if (hdr->len * 4 < hdr->cap * 3) return;

        Arena* a     = hdr->arena;
        Map*   new_m = _map_make(a,
                                 hdr->key_sz,
                                 hdr->val_sz,
                                 hdr->val_align,
                                 hdr->cap * 2,
                                 hdr->hash_fn,
                                 hdr->eq_fn);

        size_t i;
        for (i = 0; i < hdr->cap; ++i) {
                AssertBounded(i,
                              hdr->cap,
                              "_map_grow_if_needed: rehash loop must stay within old cap");
                uint8_t*   b  = _map_bkt(*mp, i);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0) {
                        _map_insert(new_m,
                                   bh->hash,
                                   _bkt_key(b),
                                   _bkt_val(b, hdr->key_sz, hdr->val_align));
                }
        }

        *mp = new_m;
        Assert(map_len(*mp) == hdr->len,
               "_map_grow_if_needed: rehashed map must preserve the entry count");
}

void _map_set(Map** mp, const void* key, const void* val) {
        Assert(mp != NULL, "_map_set: mp must not be NULL");
        Assert(*mp != NULL, "_map_set: *mp must not be NULL");
        Assert(key != NULL, "_map_set: key must not be NULL");
        Assert(val != NULL, "_map_set: val must not be NULL");

        size_t len_before = map_len(*mp);
        _map_grow_if_needed(mp);

        MapHdr*  hdr = map_hdr(*mp);
        uint64_t h   = hdr->hash_fn(key, hdr->key_sz);
        Assert(h != 0, "_map_set: hash_fn must never return the empty sentinel");
        _map_insert(*mp, h, key, val);

        /* Postcondition: either we updated an existing key (len
           unchanged) or we inserted a new one (len grew by exactly
           one); len can never shrink as a result of _map_set. */
        Assert(map_len(*mp) == len_before || map_len(*mp) == len_before + 1,
               "_map_set: len must grow by exactly 0 or 1");
}

bool _map_del(Map* m, const void* key) {
        Assert(m != NULL, "_map_del: m must not be NULL");
        Assert(key != NULL, "_map_del: key must not be NULL");

        MapHdr*  hdr = map_hdr(m);
        uint64_t h   = hdr->hash_fn(key, hdr->key_sz);
        size_t   idx = (size_t)(h & (hdr->cap - 1));
        size_t   bstride =
            _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);

        uint32_t psl;
        for (psl = 0;; ++psl) {
                AssertBounded(psl, hdr->cap, "_map_del: probe sequence must not exceed cap");
                uint8_t*   b  = _map_bkt(m, idx);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash == 0) return false;
                if (bh->psl < psl) return false;

                if (bh->hash != h) {
                        idx = (idx + 1) & (hdr->cap - 1);
                        continue;
                }
                if (!hdr->eq_fn(_bkt_key(b), key, hdr->key_sz)) {
                        idx = (idx + 1) & (hdr->cap - 1);
                        continue;
                }

                size_t backshift_steps = 0;
                for (;;) {
                        AssertBounded(backshift_steps, hdr->cap,
                                      "_map_del: backshift loop must not exceed cap");
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
                        backshift_steps++;
                }
                hdr->len--;
                return true;
        }
}

bool map_next(Map* m, MapIter* it) {
        Assert(m != NULL, "map_next: m must not be NULL");
        Assert(it != NULL, "map_next: it must not be NULL");

        MapHdr* hdr = map_hdr(m);
        Assert(it->i <= hdr->cap, "map_next: iterator index must not exceed cap");

        while (it->i < hdr->cap) {
                AssertBounded(it->i, hdr->cap, "map_next: scan loop must stay within cap");
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
        Assert(m != NULL, "map_clone: m must not be NULL");
        Assert(a != NULL, "map_clone: a must not be NULL");

        MapHdr* hdr = map_hdr(m);
        size_t  bstride =
            _map_bkt_stride(hdr->key_sz, hdr->val_sz, hdr->val_align);
        size_t   total = MAP_HDR_OFFSET + hdr->cap * bstride;
        uint8_t* raw   = (uint8_t*)arena_push(a, total, 8);
        Assert(raw != NULL, "map_clone: arena_push must succeed");

        memcpy(raw, (uint8_t*)m - MAP_HDR_OFFSET, total);
        Map* new_m            = (Map*)(raw + MAP_HDR_OFFSET);
        map_hdr(new_m)->arena = a;

        Assert(map_len(new_m) == map_len(m),
               "map_clone: clone must preserve the entry count");
        Assert(map_cap(new_m) == map_cap(m),
               "map_clone: clone must preserve the capacity");
        return new_m;
}

void* _map_keys(Map* m, Arena* a, size_t key_sz) {
        Assert(m != NULL, "_map_keys: m must not be NULL");
        Assert(a != NULL, "_map_keys: a must not be NULL");

        MapHdr* hdr = map_hdr(m);
        Assert(hdr->key_sz == key_sz,
               "_map_keys: key_sz must match the map's actual key size");

        void*  keys = _slice_make(a, key_sz, 8, hdr->len ? hdr->len : 1);
        size_t i;
        for (i = 0; i < hdr->cap; i++) {
                AssertBounded(i, hdr->cap, "_map_keys: loop must stay within cap");
                uint8_t*   b  = _map_bkt(m, i);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0) keys = _slice_push_raw(keys, _bkt_key(b));
        }

        Assert(slice_len(keys) == hdr->len,
               "_map_keys: result length must equal the map's entry count");
        return keys;
}

void* _map_values(Map* m, Arena* a, size_t val_sz, size_t val_align) {
        Assert(m != NULL, "_map_values: m must not be NULL");
        Assert(a != NULL, "_map_values: a must not be NULL");

        MapHdr* hdr = map_hdr(m);
        Assert(hdr->val_sz == val_sz,
               "_map_values: val_sz must match the map's actual value size");

        void* vals = _slice_make(
            a, val_sz, val_align ? val_align : 8, hdr->len ? hdr->len : 1);
        size_t i;
        for (i = 0; i < hdr->cap; i++) {
                AssertBounded(i, hdr->cap, "_map_values: loop must stay within cap");
                uint8_t*   b  = _map_bkt(m, i);
                BucketHdr* bh = (BucketHdr*)b;
                if (bh->hash != 0) {
                        vals = _slice_push_raw(
                            vals, _bkt_val(b, hdr->key_sz, hdr->val_align));
                }
        }

        Assert(slice_len(vals) == hdr->len,
               "_map_values: result length must equal the map's entry count");
        return vals;
}

uint64_t map_hash_str(const void* key, size_t sz) {
        Assert(key != NULL, "map_hash_str: key must not be NULL");
        Unused(sz);

        const Str* s = (const Str*)key;
        Assert(s->len == 0 || s->ptr != NULL,
               "map_hash_str: a non-empty Str key must have a non-NULL ptr");
        return map_hash_bytes(s->ptr, s->len);
}

bool map_eq_str(const void* a, const void* b, size_t sz) {
        Assert(a != NULL, "map_eq_str: a must not be NULL");
        Assert(b != NULL, "map_eq_str: b must not be NULL");
        Unused(sz);
        return str_eq(*(const Str*)a, *(const Str*)b);
}
#endif /* BARESTD_IMPLEMENTATION */
