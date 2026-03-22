#define BARESTD_IMPLEMENTATION
#define BAREPOOL_IMPLEMENTATION
#include <stdio.h>

#include "barepool.h"

int main(void) {
        Arena* a = arena_new(KB(16));

        typedef struct {
                int   id;
                float x, y;
        } Node;
        Pool* pool = pool_make(a, sizeof(Node), 8);

        /* --- 1. Initial state --- */
        printf("cap=%zu  used=%zu  full=%d  empty=%d\n",
               pool_cap(pool),
               pool_used(pool),
               pool_is_full(pool),
               pool_is_empty(pool));

        /* --- 2. pool_alloc --- */
        Node* n1 = (Node*)pool_alloc(pool);
        Node* n2 = (Node*)pool_alloc(pool);
        Node* n3 = (Node*)pool_alloc(pool);
        n1->id   = 1;
        n1->x    = 0.1f;
        n1->y    = 0.2f;
        n2->id   = 2;
        n2->x    = 1.0f;
        n2->y    = 2.0f;
        n3->id   = 3;
        n3->x    = 9.9f;
        n3->y    = 8.8f;
        printf("after 3 allocs: used=%zu\n", pool_used(pool));
        printf("n1->id=%d  n2->x=%.1f  n3->y=%.1f\n", n1->id, n2->x, n3->y);

        /* --- 3. pool_free: returns slot to free list --- */
        pool_free(pool, n2);
        printf("after free n2: used=%zu\n", pool_used(pool));

        /* --- 4. Reallocate (gets n2's slot back) --- */
        Node* reused = (Node*)pool_alloc(pool);
        printf("reused slot non-null: %d\n", reused != NULL);

        /* --- 5. pool_is_full / alloc-when-full --- */
        while (!pool_is_full(pool)) pool_alloc(pool);
        printf("pool full: full=%d\n", pool_is_full(pool));
        Node* over = (Node*)pool_alloc(pool);
        printf("alloc when full returns NULL: %d\n", over == NULL);

        /* --- 6. pool_reset: returns all objects at once --- */
        pool_reset(pool);
        printf("after reset: used=%zu  empty=%d\n",
               pool_used(pool),
               pool_is_empty(pool));

        /* --- 7. Minimum stride: always >= sizeof(void*) --- */
        Pool*  tiny = pool_make(a, 1, 4); /* 1-byte objects */
        void*  t1   = pool_alloc(tiny);
        void*  t2   = pool_alloc(tiny);
        size_t dist = (size_t)((uint8_t*)t2 - (uint8_t*)t1);
        printf("tiny pool slot distance=%zu >= ptr_size=%zu: %d\n",
               dist,
               sizeof(void*),
               dist >= sizeof(void*));

        /* --- 8. Large object pool --- */
        typedef struct {
                char data[256];
                int  id;
        } BigObj;
        Pool*   big = pool_make(a, sizeof(BigObj), 4);
        BigObj* b1  = (BigObj*)pool_alloc(big);
        b1->id      = 42;
        printf("big pool alloc: id=%d  cap=%zu\n", b1->id, pool_cap(big));

        /* --- 9. pool_free NULL is safe --- */
        pool_free(pool, NULL);
        printf("pool_free(NULL) is safe\n");

        arena_free(a);
        printf("done.\n");
        return 0;
}
