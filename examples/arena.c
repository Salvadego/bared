#define BARESTD_IMPLEMENTATION
#include <stdio.h>

#include "barestd.h"

int main(void) {
        /* --- 1. Stack-backed arena (no heap) --- */
        uint8_t backing[1024];
        Arena   stack_a = arena_from_buf(backing, sizeof backing);

        int* nums = arena_push_array(&stack_a, int, 4);
        nums[0]   = 10;
        nums[1]   = 20;
        nums[2]   = 30;
        nums[3]   = 40;
        printf("stack arena: nums[2]=%d  pos=%zu\n", nums[2], stack_a.pos);

        /* --- 2. Heap-backed arena --- */
        Arena* a = arena_new(KB(64));

        typedef struct {
                float x, y;
        } Point;
        Point* p = arena_push_type(a, Point);
        p->x     = 1.5f;
        p->y     = 2.5f;
        printf("heap arena: point=(%.1f, %.1f)\n", p->x, p->y);

        /* --- 3. Scratch scope: rewinds on scratch_end --- */
        Scratch sc  = scratch_begin(a);
        char*   tmp = arena_push_array(a, char, 256);
        snprintf(tmp, 256, "temporary at pos %zu", a->pos);
        printf("scratch alloc: \"%s\"\n", tmp);
        scratch_end(sc);
        printf("after scratch_end: pos=%zu\n", a->pos);

        /* --- 4. Nested scratches --- */
        Scratch outer = scratch_begin(a);
        arena_push_array(a, int, 8);
        {
                Scratch inner = scratch_begin(a);
                arena_push_array(a, int, 4);
                printf("inner scratch pos: %zu\n", a->pos);
                scratch_end(inner);
        }
        printf("after inner scratch_end: pos=%zu\n", a->pos);
        scratch_end(outer);
        printf("after outer scratch_end: pos=%zu\n", a->pos);

        /* --- 5. arena_push_type and arena_push_array macros --- */
        double* d = arena_push_type(a, double);
        *d        = 3.14159;
        printf("push_type double: %.5f\n", *d);

        char* buf = arena_push_array(a, char, 32);
        snprintf(buf, 32, "hello arena");
        printf("push_array char[32]: \"%s\"\n", buf);

        /* --- 6. Overflow chunk: arena grows automatically --- */
        Arena* small = arena_new(64);
        for (int i = 0; i < 20; i++) {
                int* slot = arena_push_type(small, int);
                *slot     = i * i;
        }
        printf("overflow chain: small->next=%s\n", small->next ? "yes" : "no");

        /* --- 7. arena_reset: rewind without freeing memory --- */
        arena_push_array(a, char, 100);
        size_t pos_before_reset = a->pos;
        arena_reset(a);
        printf("arena_reset: pos %zu -> %zu\n", pos_before_reset, a->pos);

        /* --- 8. arena_pop_to: rewind to a saved position --- */
        arena_push_array(a, char, 50);
        size_t saved = a->pos;
        arena_push_array(a, char, 50);
        arena_pop_to(a, saved);
        printf("arena_pop_to: pos back to %zu\n", a->pos);

        arena_free(a);
        arena_free(small);
        printf("done.\n");
        return 0;
}
