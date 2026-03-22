/*
 * barepool.h -- Fixed-size object pool allocator
 * ================================================
 *
 *  USAGE
 *    #define BAREPOOL_IMPLEMENTATION
 *    #include "barepool.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena)
 *
 *  DESIGN
 *    A Pool pre-allocates N slots of exactly stride bytes from an arena.
 *    Free slots are linked via an intrusive singly-linked free list stored
 *    in the first sizeof(void*) bytes of each free slot.
 *    Both alloc and free are O(1).
 *
 *    Unlike the arena, individual objects CAN be returned to the pool for
 *    reuse within the same lifetime.  The pool itself still lives in the
 *    arena and is freed when the arena is freed.
 *
 *    Useful for:
 *      - Network connection objects (barenet)
 *      - Request / event objects    (bareproc)
 *      - Any fixed-size struct created and destroyed at high frequency
 *
 *  EXAMPLE
 *
 *    typedef struct { int x, y; } Point;
 *
 *    Pool* pool = pool_make(arena, sizeof(Point), 64);
 *
 *    Point* p = (Point*)pool_alloc(pool);   // O(1)
 *    p->x = 1; p->y = 2;
 *    pool_free(pool, p);                    // O(1), returns to pool
 *
 *    printf("used=%zu cap=%zu\n", pool_used(pool), pool_cap(pool));
 */

#ifndef BAREPOOL_H
#define BAREPOOL_H

#include <stdbool.h>
#include <stddef.h>

#include "barestd.h"

typedef struct {
        void*    free_list; /* head of intrusive free list       */
        uint8_t* base;      /* raw slab from arena               */
        size_t   stride;    /* bytes per slot (>= sizeof(void*)) */
        size_t   cap;       /* total slot count                  */
        size_t   used;      /* currently allocated slot count    */
} Pool;

/* Allocate a pool of cap slots each of obj_sz bytes.
   obj_sz is rounded up to pointer alignment. */
Pool* pool_make(Arena* a, size_t obj_sz, size_t cap);

/* Allocate one object.  Returns NULL if the pool is full. */
void* pool_alloc(Pool* p);

/* Return an object to the pool.  ptr must come from pool_alloc
   on this same pool. */
void pool_free(Pool* p, void* ptr);

/* Return all objects at once without freeing the pool itself. */
void pool_reset(Pool* p);

size_t pool_used(const Pool* p);
size_t pool_cap(const Pool* p);
bool   pool_is_full(const Pool* p);
bool   pool_is_empty(const Pool* p);

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREPOOL_IMPLEMENTATION

Pool* pool_make(Arena* a, size_t obj_sz, size_t cap) {
        /* Each slot must hold at least one pointer for the free list link. */
        size_t min_stride = sizeof(void*);
        size_t stride     = obj_sz < min_stride ? min_stride : obj_sz;
        /* Align stride to pointer size. */
        stride = AlignUp(stride, sizeof(void*));

        Pool*    p   = arena_push_type(a, Pool);
        uint8_t* buf = (uint8_t*)arena_push(a, stride * cap, sizeof(void*));

        p->base   = buf;
        p->stride = stride;
        p->cap    = cap;
        p->used   = 0;

        /* Thread all slots into the free list. */
        size_t i;
        for (i = 0; i + 1 < cap; i++) {
                void** slot = (void**)(buf + i * stride);
                *slot       = buf + (i + 1) * stride;
        }
        *(void**)(buf + (cap - 1) * stride) = NULL;
        p->free_list                        = buf;
        return p;
}

void* pool_alloc(Pool* p) {
        if (!p->free_list) return NULL;
        void* obj    = p->free_list;
        p->free_list = *(void**)obj;
        p->used++;
        return obj;
}

void pool_free(Pool* p, void* ptr) {
        if (!ptr) return;
        *(void**)ptr = p->free_list;
        p->free_list = ptr;
        p->used--;
}

void pool_reset(Pool* p) {
        size_t i;
        for (i = 0; i + 1 < p->cap; i++) {
                void** slot = (void**)(p->base + i * p->stride);
                *slot       = p->base + (i + 1) * p->stride;
        }
        *(void**)(p->base + (p->cap - 1) * p->stride) = NULL;
        p->free_list                                  = p->base;
        p->used                                       = 0;
}

size_t pool_used(const Pool* p) {
        return p->used;
}
size_t pool_cap(const Pool* p) {
        return p->cap;
}
bool pool_is_full(const Pool* p) {
        return p->used == p->cap;
}
bool pool_is_empty(const Pool* p) {
        return p->used == 0;
}

#endif /* BAREPOOL_IMPLEMENTATION */
#endif /* BAREPOOL_H */
