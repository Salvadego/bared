#define BARESTD_IMPLEMENTATION
#include <stdio.h>
#include <string.h>

#include "barestd.h"

int main(void) {
        Arena* a = arena_new(KB(64));

        /* --- 1. slice_make, slice_push, slice_len, slice_cap --- */
        Slice(int) nums = slice_make(a, int, 4);
        for (int i = 0; i < 6; i++) slice_push(nums, int, i * 10);
        printf("len=%zu  cap=%zu\n", slice_len(nums), slice_cap(nums));

        /* --- 2. slice_at (bounds-checked) and slice_last --- */
        printf("nums[3]=%d  last=%d\n", slice_at(nums, 3), slice_last(nums));

        /* --- 3. slice_put (lvalue form, no intermediate variable) --- */
        Slice(float) fs = slice_make(a, float, 2);
        float fv        = 3.14f;
        slice_put(fs, fv);
        fv = 2.71f;
        slice_put(fs, fv);
        printf("fs[0]=%.2f  fs[1]=%.2f\n", fs[0], fs[1]);

        /* --- 4. slice_remove: shifts tail left, O(n) --- */
        Slice(int) del = slice_make(a, int, 8);
        for (int i = 0; i < 5; i++) slice_push(del, int, i);
        slice_remove(del, 2);
        printf(
            "after remove[2]: %d %d %d %d\n", del[0], del[1], del[2], del[3]);

        /* --- 5. slice_contains (linear scan) --- */
        int needle = 30;
        printf("contains 30: %d\n", slice_contains(nums, needle));
        needle = 99;
        printf("contains 99: %d\n", slice_contains(nums, needle));

        /* --- 6. slice_pop --- */
        size_t before = slice_len(nums);
        slice_pop(nums);
        printf("after pop: len %zu -> %zu\n", before, slice_len(nums));

        /* --- 7. slice_clear --- */
        slice_clear(nums);
        printf("after clear: len=%zu\n", slice_len(nums));

        /* --- 8. Typed struct slice --- */
        typedef struct {
                int  id;
                char name[16];
        } Record;
        Slice(Record) recs = slice_make(a, Record, 4);
        Record r;
        r.id = 1;
        strncpy(r.name, "alice", 15);
        slice_put(recs, r);
        r.id = 2;
        strncpy(r.name, "bob", 15);
        slice_put(recs, r);
        printf("recs[0].name=%s  recs[1].id=%d\n", recs[0].name, recs[1].id);

        /* --- 9. Auto-grow beyond initial capacity --- */
        Slice(int) grow = slice_make(a, int, 2);
        for (int i = 0; i < 20; i++) slice_push(grow, int, i);
        printf("grow: len=%zu  cap=%zu  (started at 2)\n",
               slice_len(grow),
               slice_cap(grow));

        /* --- 10. Pointer slice --- */
        Slice(char*) strs = slice_make(a, char*, 4);
        const char* s1    = "hello";
        const char* s2    = "world";
        slice_put(strs, s1);
        slice_put(strs, s2);
        printf("strs[0]=%s  strs[1]=%s\n", strs[0], strs[1]);

        /* --- 11. slice_stride --- */
        printf("stride of int slice: %zu (expect %zu)\n",
               slice_stride(grow),
               sizeof(int));

        arena_free(a);
        printf("done.\n");
        return 0;
}
