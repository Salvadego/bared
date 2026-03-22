#define BARESTD_IMPLEMENTATION
#include <stdio.h>

#include "barestd.h"

int main(void) {
        Arena* a = arena_new(KB(64));

        /* --- 1. int -> int --- */
        Map* m = map_make(a, int, int, 16);
        for (int k = 0; k < 8; k++) {
                int v = k * k;
                map_set(m, &k, &v);
        }
        printf("len=%zu  cap=%zu\n", map_len(m), map_cap(m));

        int  key = 5;
        int* got = (int*)map_get(m, &key);
        printf("map[5] = %d  (expect 25)\n", got ? *got : -1);

        /* --- 2. Update existing key --- */
        int newval = 999;
        map_set(m, &key, &newval);
        got = (int*)map_get(m, &key);
        printf("after update map[5] = %d\n", got ? *got : -1);

        /* --- 3. Delete --- */
        map_del(m, &key);
        got = (int*)map_get(m, &key);
        printf("after del map[5] = %s\n", got ? "found" : "NULL");
        printf("len after del = %zu\n", map_len(m));

        /* --- 4. Iterate with MapIter --- */
        MapIter it = {0};
        printf("entries: ");
        while (map_next(m, &it)) {
                printf("%d->%d ", iter_key(it, int), iter_val(it, int));
        }
        printf("\n");

        /* --- 5. Str keys (map_make_str_key) --- */
        Map* sm = map_make_str_key(a, int, 16);
        Str  ka = str_lit("alpha");
        Str  kb = str_lit("beta");
        Str  kc = str_lit("gamma");
        int  va = 1, vb = 2, vc = 3;
        map_set(sm, &ka, &va);
        map_set(sm, &kb, &vb);
        map_set(sm, &kc, &vc);
        int* ra = (int*)map_get(sm, &ka);
        int* rb = (int*)map_get(sm, &kb);
        printf("str map[\"alpha\"]=%d  [\"beta\"]=%d\n",
               ra ? *ra : -1,
               rb ? *rb : -1);

        /* --- 6. map_keys and map_values as slices --- */
        Slice(int) keys = map_keys(m, a, int);
        printf("keys slice len=%zu\n", slice_len(keys));
        Slice(int) vals = map_values(m, a, int);
        printf("vals slice len=%zu\n", slice_len(vals));

        /* --- 7. map_clear --- */
        map_clear(m);
        printf("after clear: len=%zu\n", map_len(m));

        /* --- 8. map_clone --- */
        int ck = 42, cv = 99;
        map_set(m, &ck, &cv);
        Map* clone = map_clone(m, a);
        int* cv2   = (int*)map_get(clone, &ck);
        printf("cloned map[42] = %d\n", cv2 ? *cv2 : -1);

        /* --- 9. Auto-rehash when load > 75% --- */
        Map* big = map_make(a, int, int, 4);
        for (int i = 0; i < 20; i++) {
                int v = i;
                map_set(big, &i, &v);
        }
        printf("rehashed: len=%zu  cap=%zu\n", map_len(big), map_cap(big));

        /* --- 10. Custom hash and eq functions --- */
        Map* cm  = _map_make(a,
                             sizeof(int),
                             sizeof(int),
                             AlignOfType(int),
                             16,
                             map_hash_bytes,
                             map_eq_bytes);
        int  ck2 = 7, cv3 = 777;
        map_set(cm, &ck2, &cv3);
        int* found = (int*)map_get(cm, &ck2);
        printf("custom fn map[7] = %d\n", found ? *found : -1);

        /* --- 11. Struct value --- */
        typedef struct {
                float x, y;
        } Vec2;
        Map* vm  = map_make(a, int, Vec2, 8);
        Vec2 pos = {1.5f, 2.5f};
        int  vid = 10;
        map_set(vm, &vid, &pos);
        Vec2* vgot = (Vec2*)map_get(vm, &vid);
        if (vgot) printf("struct val: (%.1f, %.1f)\n", vgot->x, vgot->y);

        arena_free(a);
        printf("done.\n");
        return 0;
}
