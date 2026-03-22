/*
 * barebuf.h -- Ring buffer (circular buffer)
 * ============================================
 *
 *  USAGE
 *    #define BAREBUF_IMPLEMENTATION
 *    #include "barebuf.h"
 *
 *  DEPENDS ON
 *    barestd.h  (Arena)
 *
 *  DESIGN
 *    RingBuf is a fixed-capacity byte ring backed by an arena allocation.
 *    Capacity is always rounded up to the next power of two so wrap-around
 *    is a cheap bitmask instead of a modulo.
 *
 *    NOT thread-safe on its own.  Wrap with baresync.h for concurrent use.
 *
 *    TypedRingBuf(T, N) declares a stack/static ring of N elements of
 *    type T with zero allocation.  N must be a power of two.
 *
 *  EXAMPLE -- byte ring
 *
 *    RingBuf* rb = ringbuf_make(arena, KB(4));
 *    const char msg[] = "hello";
 *    ringbuf_write(rb, msg, 5);
 *
 *    char tmp[5];
 *    size_t got = ringbuf_read(rb, tmp, sizeof tmp);
 *
 *  EXAMPLE -- typed ring (no arena)
 *
 *    TypedRingBuf(int, 8) q = {0};
 *    int v = 42;
 *    typed_ringbuf_push(&q, v);
 *    int out;
 *    typed_ringbuf_pop(&q, &out);   // out == 42
 */

#ifndef BAREBUF_H
#define BAREBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "barestd.h"

/* ================================================================
 *  RingBuf -- arena-backed byte ring
 * ================================================================ */

typedef struct {
        uint8_t* buf;
        size_t   cap;  /* always a power of two */
        size_t   head; /* read  cursor (absolute, masked on access) */
        size_t   tail; /* write cursor (absolute, masked on access) */
} RingBuf;

/* Allocate a ring with at least cap bytes (rounded up to pow2, min 16). */
RingBuf* ringbuf_make(Arena* a, size_t cap);

/* Bytes currently stored. */
size_t ringbuf_len(const RingBuf* rb);
/* Free space available for writing. */
size_t ringbuf_space(const RingBuf* rb);

bool ringbuf_is_empty(const RingBuf* rb);
bool ringbuf_is_full(const RingBuf* rb);

/* Write up to n bytes.  Returns bytes actually written. */
size_t ringbuf_write(RingBuf* rb, const void* src, size_t n);
/* Read  up to n bytes.  Returns bytes actually read. */
size_t ringbuf_read(RingBuf* rb, void* dst, size_t n);
/* Peek without consuming. */
size_t ringbuf_peek(const RingBuf* rb, void* dst, size_t n);
/* Discard up to n bytes.  Returns bytes discarded. */
size_t ringbuf_discard(RingBuf* rb, size_t n);
/* Reset to empty. */
void ringbuf_clear(RingBuf* rb);

/* ================================================================
 *  TypedRingBuf -- stack/static ring, no arena needed
 *
 *  TypedRingBuf(T, N) expands to an anonymous struct.
 *  N must be a power of two.
 *  All operations are macros.
 * ================================================================ */

#define TypedRingBuf(T, N)      \
        struct {                \
                T      _buf[N]; \
                size_t _head;   \
                size_t _tail;   \
        }

#define typed_ringbuf_cap(rb) (sizeof((rb)->_buf) / sizeof((rb)->_buf[0]))

#define typed_ringbuf_len(rb) ((rb)->_tail - (rb)->_head)

#define typed_ringbuf_is_empty(rb) (typed_ringbuf_len(rb) == 0)
#define typed_ringbuf_is_full(rb) \
        (typed_ringbuf_len(rb) == typed_ringbuf_cap(rb))

/* Push one element.  Returns true on success, false if full. */
#define typed_ringbuf_push(rb, val)                                       \
        (typed_ringbuf_is_full(rb)                                        \
             ? 0                                                          \
             : ((rb)->_buf[(rb)->_tail++ & (typed_ringbuf_cap(rb) - 1)] = \
                    (val),                                                \
                1))

/* Pop one element into *out.  Returns true on success, false if empty. */
#define typed_ringbuf_pop(rb, out)                                           \
        (typed_ringbuf_is_empty(rb)                                          \
             ? 0                                                             \
             : (*(out) =                                                     \
                    (rb)->_buf[(rb)->_head++ & (typed_ringbuf_cap(rb) - 1)], \
                1))

/* Peek at the front element without consuming. */
#define typed_ringbuf_peek_front(rb, out)                                  \
        (typed_ringbuf_is_empty(rb)                                        \
             ? 0                                                           \
             : (*(out) =                                                   \
                    (rb)->_buf[(rb)->_head & (typed_ringbuf_cap(rb) - 1)], \
                1))

#define typed_ringbuf_clear(rb) ((void)((rb)->_head = (rb)->_tail = 0))

/* ================================================================
 *  IMPLEMENTATION
 * ================================================================ */
#ifdef BAREBUF_IMPLEMENTATION

#        include <string.h>

static size_t _rb_next_pow2(size_t n) {
        size_t p = 16;
        while (p < n) p <<= 1;
        return p;
}

RingBuf* ringbuf_make(Arena* a, size_t cap) {
        cap         = _rb_next_pow2(cap);
        RingBuf* rb = arena_push_type(a, RingBuf);
        rb->buf     = arena_push_array(a, uint8_t, cap);
        rb->cap     = cap;
        rb->head    = 0;
        rb->tail    = 0;
        return rb;
}

size_t ringbuf_len(const RingBuf* rb) {
        return rb->tail - rb->head;
}
size_t ringbuf_space(const RingBuf* rb) {
        return rb->cap - ringbuf_len(rb);
}
bool ringbuf_is_empty(const RingBuf* rb) {
        return rb->tail == rb->head;
}
bool ringbuf_is_full(const RingBuf* rb) {
        return ringbuf_len(rb) == rb->cap;
}

size_t ringbuf_write(RingBuf* rb, const void* src, size_t n) {
        const uint8_t* s     = (const uint8_t*)src;
        size_t         avail = ringbuf_space(rb);
        size_t         i;
        if (n > avail) n = avail;
        for (i = 0; i < n; i++) rb->buf[(rb->tail + i) & (rb->cap - 1)] = s[i];
        rb->tail += n;
        return n;
}

size_t ringbuf_read(RingBuf* rb, void* dst, size_t n) {
        uint8_t* d    = (uint8_t*)dst;
        size_t   have = ringbuf_len(rb);
        size_t   i;
        if (n > have) n = have;
        for (i = 0; i < n; i++) d[i] = rb->buf[(rb->head + i) & (rb->cap - 1)];
        rb->head += n;
        return n;
}

size_t ringbuf_peek(const RingBuf* rb, void* dst, size_t n) {
        uint8_t* d    = (uint8_t*)dst;
        size_t   have = ringbuf_len(rb);
        size_t   i;
        if (n > have) n = have;
        for (i = 0; i < n; i++) d[i] = rb->buf[(rb->head + i) & (rb->cap - 1)];
        return n;
}

size_t ringbuf_discard(RingBuf* rb, size_t n) {
        size_t have = ringbuf_len(rb);
        if (n > have) n = have;
        rb->head += n;
        return n;
}

void ringbuf_clear(RingBuf* rb) {
        rb->head = rb->tail = 0;
}

#endif /* BAREBUF_IMPLEMENTATION */
#endif /* BAREBUF_H */
