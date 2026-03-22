#define BARESTD_IMPLEMENTATION
#define BAREBUF_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "barebuf.h"

int main(void) {
        Arena* a = arena_new(KB(4));

        /* --- RingBuf (arena-backed byte ring) --- */

        /* 1. make and inspect */
        RingBuf* rb = ringbuf_make(a, 16);
        printf("cap=%zu  len=%zu  full=%d  empty=%d\n",
               rb->cap,
               ringbuf_len(rb),
               ringbuf_is_full(rb),
               ringbuf_is_empty(rb));

        /* 2. ringbuf_write / ringbuf_len / ringbuf_space */
        const char msg[]   = "Hello!";
        size_t     written = ringbuf_write(rb, msg, 6);
        printf("written=%zu  len=%zu  space=%zu\n",
               written,
               ringbuf_len(rb),
               ringbuf_space(rb));

        /* 3. ringbuf_peek (non-consuming) */
        char peek[8];
        memset(peek, 0, sizeof peek);
        ringbuf_peek(rb, peek, 3);
        printf(
            "peek 3 bytes: \"%.3s\"  len still=%zu\n", peek, ringbuf_len(rb));

        /* 4. ringbuf_read (consuming) */
        char dst[8];
        memset(dst, 0, sizeof dst);
        size_t nr = ringbuf_read(rb, dst, 4);
        printf("read %zu: \"%.4s\"  remaining=%zu\n", nr, dst, ringbuf_len(rb));

        /* 5. ringbuf_discard */
        size_t discarded = ringbuf_discard(rb, 1);
        printf(
            "discard 1: discarded=%zu  len=%zu\n", discarded, ringbuf_len(rb));

        /* 6. ringbuf_clear */
        ringbuf_clear(rb);
        printf("after clear: len=%zu  empty=%d\n",
               ringbuf_len(rb),
               ringbuf_is_empty(rb));

        /* 7. Write-to-full, partial write */
        RingBuf* small_rb = ringbuf_make(a, 16); /* cap rounds to 16 */
        for (int i = 0; i < 16; i++) {
                char c = (char)('a' + i);
                ringbuf_write(small_rb, &c, 1);
        }
        printf("full ring: full=%d\n", ringbuf_is_full(small_rb));
        char   extra         = 'Z';
        size_t extra_written = ringbuf_write(small_rb, &extra, 1);
        printf("write when full: written=%zu  (expect 0)\n", extra_written);

        /* 8. Wrap-around: read some, then write past the physical end */
        char tmp4[4];
        ringbuf_read(small_rb, tmp4, 4); /* consume 4 from front */
        char wrap = 'W';
        ringbuf_write(small_rb, &wrap, 1); /* wrap-around write */
        printf("after wrap-around write: len=%zu\n", ringbuf_len(small_rb));

        /* --- TypedRingBuf (stack/static, no arena) --- */

        /* 9. Basic push / pop */
        TypedRingBuf(int, 8) q;
        memset(&q, 0, sizeof q);
        printf("\nTypedRingBuf cap=%zu  empty=%d\n",
               typed_ringbuf_cap(&q),
               typed_ringbuf_is_empty(&q));

        for (int i = 1; i <= 4; i++) typed_ringbuf_push(&q, i * 10);
        printf("pushed 4: len=%zu\n", typed_ringbuf_len(&q));

        /* 10. peek_front (non-consuming) */
        int front = 0;
        typed_ringbuf_peek_front(&q, &front);
        printf("peek_front=%d  len still=%zu\n", front, typed_ringbuf_len(&q));

        /* 11. pop */
        int popped = 0;
        typed_ringbuf_pop(&q, &popped);
        printf("pop=%d  len now=%zu\n", popped, typed_ringbuf_len(&q));

        /* 12. full detection -- separate the calls to avoid sequence-point UB
         */
        TypedRingBuf(char, 4) small_q;
        memset(&small_q, 0, sizeof small_q);
        for (int i = 0; i < 4; i++)
                typed_ringbuf_push(&small_q, (char)('A' + i));
        int is_full    = typed_ringbuf_is_full(&small_q);
        int push_again = typed_ringbuf_push(&small_q, 'Z');
        printf("small: full=%d  push-when-full=%d  (expect 1,0)\n",
               is_full,
               push_again);

        /* 13. typed_ringbuf_clear */
        typed_ringbuf_clear(&q);
        printf("after clear: len=%zu\n", typed_ringbuf_len(&q));

        arena_free(a);
        printf("done.\n");
        return 0;
}
